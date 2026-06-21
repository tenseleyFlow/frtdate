/* frtdate — see frtdate.h. A from-scratch reimplementation of the common
 * grammar of GNU find's date arguments; no gnulib. */
#include "frtdate.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* English month name (full or abbreviation), 1..12, or 0. find/gnulib use their
 * own English table regardless of locale, so we do too (not strptime %B). */
static int month_name(const char *w)
{
	static const char *m[] = {"january", "february", "march", "april", "may",
				  "june", "july", "august", "september", "october",
				  "november", "december"};
	size_t len = strlen(w);
	for (int i = 0; i < 12; i++) {
		if (strcmp(w, m[i]) == 0)
			return i + 1;
		if (len >= 3 && strncmp(w, m[i], 3) == 0 &&
		    (len == 3 || strncmp(w, m[i], len) == 0))
			return i + 1; /* unambiguous prefix (e.g. "sept", "mar") */
	}
	return 0;
}

/* English weekday name (full, abbreviation, or the few gnulib aliases), 0=Sun..
 * 6=Sat, or -1. */
static int weekday_name(const char *w)
{
	static const char *d[] = {"sunday", "monday", "tuesday", "wednesday",
				  "thursday", "friday", "saturday"};
	for (int i = 0; i < 7; i++)
		if (strcmp(w, d[i]) == 0 || (strlen(w) == 3 && strncmp(w, d[i], 3) == 0))
			return i;
	if (strcmp(w, "tues") == 0)
		return 2;
	if (strcmp(w, "thur") == 0 || strcmp(w, "thurs") == 0)
		return 4;
	return -1;
}

/* Spelled-out ordinal (first..twelfth), 1..12, or 0. Used as a day-of-week
 * count ("first monday"); gnulib also has "next/last/this" (handled inline). */
static int ordinal_word(const char *w)
{
	static const char *o[] = {"first", "second", "third", "fourth", "fifth",
				  "sixth", "seventh", "eighth", "ninth", "tenth",
				  "eleventh", "twelfth"};
	for (int i = 0; i < 12; i++)
		if (strcmp(w, o[i]) == 0)
			return i + 1;
	return 0;
}

/* A named timezone we honor: returns 1 and sets *off (seconds east of UTC) for
 * UTC/GMT/UT/Z. Other abbreviations (EST, PDT, ...) are deliberately out of
 * scope — gnulib's table is large and its behavior here is inconsistent (find
 * 4.10.0 rejects "EST"); numeric offsets (+0500) cover the portable need. */
static int zone_name(const char *w, int *off)
{
	if (strcmp(w, "utc") == 0 || strcmp(w, "gmt") == 0 || strcmp(w, "ut") == 0 ||
	    strcmp(w, "z") == 0) {
		*off = 0;
		return 1;
	}
	return 0;
}

/* Single-letter military timezone (gnulib military_table): A=+1h..I=+9h,
 * K=+10..M=+12, N=-1h..Y=-12h, Z=0; J = local time. A timezone with no explicit
 * clock time defaults to midnight, which is why "a week ago" (zone A) lands on
 * midnight a week ago rather than now's time of day. Returns 0 (not a military
 * letter), 1 (offset set in *off, seconds east of UTC), or 2 (J = local). */
static int military_zone(const char *w, int *off)
{
	static const int h[26] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  0 /*J*/, 10,
				  11, 12, -1, -2, -3, -4, -5, -6, -7, -8, -9,
				  -10, -11, -12, 0};
	if (w[0] < 'a' || w[0] > 'z' || w[1] != '\0')
		return 0;
	if (w[0] == 'j')
		return 2; /* local time */
	*off = h[w[0] - 'a'] * 3600;
	return 1;
}

/* A relative-date unit: which tm field it adjusts and a day-count multiplier
 * (week/fortnight fold onto tm_mday). Returns 1 if `w` names a unit. */
static int reldate_unit(const char *w, int *field, long long *mult)
{
	static const struct {
		const char *n;
		int f; /* 0=year 1=mon 2=mday 3=hour 4=min 5=sec */
		long long m;
	} u[] = {
		{"sec", 5, 1}, {"secs", 5, 1}, {"second", 5, 1}, {"seconds", 5, 1},
		{"min", 4, 1}, {"mins", 4, 1}, {"minute", 4, 1}, {"minutes", 4, 1},
		{"hour", 3, 1}, {"hours", 3, 1},
		{"day", 2, 1}, {"days", 2, 1},
		{"week", 2, 7}, {"weeks", 2, 7},
		{"fortnight", 2, 14}, {"fortnights", 2, 14},
		{"month", 1, 1}, {"months", 1, 1},
		{"year", 0, 1}, {"years", 0, 1},
	};
	for (size_t i = 0; i < sizeof u / sizeof u[0]; i++)
		if (strcmp(w, u[i].n) == 0) {
			*field = u[i].f;
			*mult = u[i].m;
			return 1;
		}
	return 0;
}

/* The relative/named grammar (everything that isn't @EPOCH or a bare ISO date).
 * Resolves against `now`; writes the whole-second result via sec and nsec. 0/-1. */
static int parse_rel(const char *s, struct timespec now, long long *sec, long *nsec)
{
	long long off[6] = {0, 0, 0, 0, 0, 0}; /* relative: year mon mday hour min sec */
	int any = 0, have_num = 0, sign = 1;
	long long num = 0;
	/* "ago"/"hence" apply a factor to the IMMEDIATELY PRECEDING relunit only
	 * (gnulib: `relunit tAGO`), so "3 days 2 hours ago" is +3d -2h. */
	int last_field = -1;
	long long last_amt = 0;
	/* absolute components */
	int abs_mon = 0;      /* 1..12 from a month name, else 0 */
	long long freenum[4]; /* numbers not consumed by a unit (abs date day/year) */
	int nfree = 0;
	int have_time = 0, t_hour = 0, t_min = 0, t_sec = 0;
	int have_wday = 0, wday_num = 0;
	long long wday_ord = 0; /* this/bare=0, next=+1, last=-1 */
	int pend_ord = 0;       /* a next/last/this/ordinal awaiting its weekday */
	long long pend_ord_val = 0;
	int have_tz = 0, tz_off = 0; /* explicit timezone, seconds east of UTC */
	const char *p = s;

	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		unsigned char c = (unsigned char)*p;
		if (c == '+' || c == '-') {
			/* A numeric timezone offset [+-]HHMM follows a time (e.g.
			 * "06:00 +0500"); 4 digits not followed by another digit. */
			if (have_time && !have_tz && isdigit((unsigned char)p[1]) &&
			    isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]) &&
			    isdigit((unsigned char)p[4]) && !isdigit((unsigned char)p[5])) {
				int hh = (p[1] - '0') * 10 + (p[2] - '0');
				int mm = (p[3] - '0') * 10 + (p[4] - '0');
				if (hh > 23 || mm > 59)
					return -1;
				tz_off = (c == '-' ? -1 : 1) * (hh * 3600 + mm * 60);
				have_tz = 1;
				any = 1;
				p += 5;
				continue;
			}
			sign = (c == '-') ? -1 : 1;
			p++;
			continue;
		}
		if (isdigit(c)) {
			char *end;
			errno = 0;
			long long v = strtoll(p, &end, 10);
			if (errno == ERANGE)
				return -1;
			if (*end == ':') { /* HH:MM[:SS] time of day */
				if (have_time)
					return -1;
				if (have_num) { /* flush a pending free number (year) */
					if (nfree >= 4)
						return -1;
					freenum[nfree++] = num;
					have_num = 0;
				}
				long long mm, ss = 0;
				p = end + 1;
				if (!isdigit((unsigned char)*p))
					return -1;
				mm = strtoll(p, &end, 10);
				p = end;
				if (*p == ':') {
					p++;
					if (!isdigit((unsigned char)*p))
						return -1;
					ss = strtoll(p, &end, 10);
					p = end;
				}
				if (v < 0 || v > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60)
					return -1;
				t_hour = (int)v;
				t_min = (int)mm;
				t_sec = (int)ss;
				have_time = 1;
				any = 1;
				continue;
			}
			if (*end == '/') { /* numeric slash date: M/D/Y or Y/M/D */
				if (abs_mon || nfree > 0 || have_num)
					return -1; /* a date/number already pending */
				char *q = end + 1;
				if (!isdigit((unsigned char)*q))
					return -1;
				long long n2 = strtoll(q, &q, 10);
				if (*q != '/' || !isdigit((unsigned char)q[1]))
					return -1;
				q++;
				long long n3 = strtoll(q, &q, 10);
				p = q;
				int mon, day;
				long long yr;
				if (v > 31) { /* Y/M/D */
					yr = v;
					mon = (int)n2;
					day = (int)n3;
				} else { /* M/D/Y (find takes slashes this way; dashes are ISO only) */
					mon = (int)v;
					day = (int)n2;
					yr = n3;
				}
				if (yr < 100) /* 2-digit year, gnulib's split */
					yr += (yr < 69) ? 2000 : 1900;
				if (mon < 1 || mon > 12 || day < 1 || day > 31 ||
				    yr < 1000 || yr > 9999)
					return -1;
				abs_mon = mon;
				freenum[nfree++] = day;
				freenum[nfree++] = yr;
				any = 1;
				continue;
			}
			if (have_num) { /* flush the previous free number (e.g. "15 2020") */
				if (nfree >= 4)
					return -1;
				freenum[nfree++] = num;
			}
			num = v;
			have_num = 1;
			p = end;
			/* find allows a comma only right after the day in "Month DD, YYYY";
			 * a comma elsewhere is a parse error (e.g. "Mar 15 2020, 14:30"). */
			if (*p == ',' && abs_mon && v >= 1 && v <= 31)
				p++;
			continue;
		}
		if (isalpha(c)) {
			char w[16];
			size_t n = 0;
			while (isalpha((unsigned char)*p)) {
				if (n + 1 >= sizeof w)
					return -1; /* longer than any known word */
				w[n++] = (char)tolower((unsigned char)*p);
				p++;
			}
			w[n] = '\0';
			int field, mon, wd, ord = 0, zoff;
			long long mult;
			if (reldate_unit(w, &field, &mult)) {
				long long cnt;
				if (have_num) {
					cnt = sign * num;
				} else if (pend_ord) {
					cnt = pend_ord_val; /* "next week", "last month" */
					pend_ord = 0;
				} else {
					cnt = 1; /* a bare unit means one ("week ago" = 1 week ago) */
				}
				long long amt = cnt * mult;
				off[field] += amt;
				last_field = field;
				last_amt = amt;
				have_num = 0;
				sign = 1;
				any = 1;
			} else if (strcmp(w, "ago") == 0 || strcmp(w, "hence") == 0) {
				if (last_field < 0)
					return -1;
				if (strcmp(w, "ago") == 0)
					off[last_field] -= 2 * last_amt;
				last_field = -1;
				any = 1;
			} else if (strcmp(w, "now") == 0 || strcmp(w, "today") == 0) {
				last_field = 2;
				last_amt = 0;
				any = 1;
			} else if (strcmp(w, "yesterday") == 0) {
				off[2] -= 1;
				last_field = 2;
				last_amt = -1;
				any = 1;
			} else if (strcmp(w, "tomorrow") == 0) {
				off[2] += 1;
				last_field = 2;
				last_amt = 1;
				any = 1;
			} else if (strcmp(w, "next") == 0 || strcmp(w, "last") == 0 ||
				   strcmp(w, "this") == 0 || (ord = ordinal_word(w)) > 0) {
				/* a day-of-week ordinal: next/last/this, or first..twelfth */
				if (pend_ord || have_num)
					return -1;
				pend_ord = 1;
				pend_ord_val = (strcmp(w, "next") == 0)   ? 1
					       : (strcmp(w, "last") == 0) ? -1
					       : (strcmp(w, "this") == 0) ? 0
									  : ord;
			} else if (strcmp(w, "am") == 0 || strcmp(w, "pm") == 0) {
				int pm = (w[0] == 'p');
				int h;
				if (have_time) {
					h = t_hour;
				} else if (have_num && num >= 1 && num <= 12) {
					h = (int)num; /* bare hour, e.g. "6am" */
					t_min = t_sec = 0;
					have_num = 0;
					have_time = 1;
				} else {
					return -1;
				}
				if (h < 1 || h > 12) /* 12-hour clock */
					return -1;
				if (pm && h != 12)
					h += 12;
				else if (!pm && h == 12)
					h = 0;
				t_hour = h;
				any = 1;
			} else if (zone_name(w, &zoff)) {
				if (have_tz)
					return -1;
				tz_off = zoff;
				have_tz = 1;
				any = 1;
			} else if ((wd = weekday_name(w)) >= 0) {
				if (have_wday || have_num)
					return -1;
				have_wday = 1;
				wday_num = wd;
				wday_ord = pend_ord ? pend_ord_val : 0;
				pend_ord = 0;
				any = 1;
				if (*p == ',') /* find allows "Monday," (gnulib tDAY ',') */
					p++;
			} else if ((mon = month_name(w)) > 0) {
				if (abs_mon || pend_ord)
					return -1;
				abs_mon = mon;
				any = 1;
			} else {
				int mz = military_zone(w, &zoff); /* single-letter zone */
				if (mz == 0)
					return -1; /* out of subset */
				if (mz == 1) { /* a real offset zone; J (mz==2) means local: no-op */
					if (have_tz)
						return -1; /* a second zone */
					tz_off = zoff;
					have_tz = 1;
				}
				any = 1;
			}
			continue;
		}
		return -1; /* unexpected character */
	}
	if (pend_ord)
		return -1; /* next/last/this with no weekday */
	if (have_num) { /* a trailing free number (abs-date day/year) */
		if (nfree >= 4)
			return -1;
		freenum[nfree++] = num;
		have_num = 0;
	}
	if (!any)
		return -1;

	/* Plain now/today with nothing else: origin verbatim (keep sub-second). */
	int has_abs = abs_mon || have_wday || have_time || have_tz || nfree > 0;
	int has_rel = 0;
	for (int k = 0; k < 6; k++)
		has_rel |= (off[k] != 0);
	if (!has_abs && !has_rel) {
		*sec = now.tv_sec;
		*nsec = now.tv_nsec;
		return 0;
	}

	/* A month-name date needs a day (1..31); a 4-digit free number is the year
	 * (else the current year). Bare free numbers (no month name) are unsupported. */
	int abs_year = 0, abs_mday = 0, have_date = 0;
	if (abs_mon) {
		for (int k = 0; k < nfree; k++) {
			if (freenum[k] >= 1 && freenum[k] <= 31 && !abs_mday)
				abs_mday = (int)freenum[k];
			else if (freenum[k] >= 1000 && freenum[k] <= 9999 && !abs_year)
				abs_year = (int)freenum[k];
			else
				return -1; /* unrecognized (2-digit year, >31 non-year, dup) */
		}
		if (!abs_mday)
			return -1; /* month with no day */
		have_date = 1;
	} else if (nfree > 0) {
		return -1; /* numbers with no month/unit anchor */
	}

	time_t base = (time_t)now.tv_sec;
	struct tm tm;
	if (!localtime_r(&base, &tm))
		return -1;

	/* Time of day: explicit time wins; an absolute date or weekday otherwise
	 * resets to midnight; a purely relative spec keeps now's time of day. */
	if (have_time) {
		tm.tm_hour = t_hour;
		tm.tm_min = t_min;
		tm.tm_sec = t_sec;
	} else if (have_date || have_wday) {
		tm.tm_hour = tm.tm_min = tm.tm_sec = 0; /* an absolute date/weekday with no time = midnight */
	}
	/* A bare timezone keeps now's wall-clock fields (no midnight); the zone only
	 * reinterprets them below (timegm − offset), so "a week ago" = now's time of
	 * day, a week back, read as zone A. */
	if (have_date) {
		tm.tm_mon = abs_mon - 1;
		tm.tm_mday = abs_mday;
		if (abs_year)
			tm.tm_year = abs_year - 1900;
	}
	tm.tm_year += (int)off[0];
	tm.tm_mon += (int)off[1];
	tm.tm_mday += (int)off[2];
	tm.tm_hour += (int)off[3];
	tm.tm_min += (int)off[4];
	tm.tm_sec += (int)off[5];
	tm.tm_isdst = -1;

	if (have_wday) {
		if (mktime(&tm) == (time_t)-1) /* normalize to populate tm_wday */
			return -1;
		/* gnulib: dayincr = (ord - (ord>0 && wday!=target))*7 + (target-wday+7)%7 */
		long long ord = wday_ord - ((wday_ord > 0 && tm.tm_wday != wday_num) ? 1 : 0);
		tm.tm_mday += (int)(ord * 7 + ((wday_num - tm.tm_wday + 7) % 7));
		tm.tm_isdst = -1;
	}

	time_t t;
	if (have_tz) {
		/* Fields are a wall clock in the given zone: interpret as UTC, then
		 * shift by the zone's offset to get the actual instant. */
		t = timegm(&tm);
		if (t == (time_t)-1)
			return -1;
		t -= tz_off;
	} else {
		t = mktime(&tm);
		if (t == (time_t)-1)
			return -1;
	}
	*sec = t;
	*nsec = 0;
	return 0;
}

/* Read exactly n decimal digits into *out, advancing *pp. Returns 1 on success. */
static int rd_digits(const char **pp, int n, int *out)
{
	const char *p = *pp;
	int v = 0;
	for (int i = 0; i < n; i++) {
		if (!isdigit((unsigned char)p[i]))
			return 0;
		v = v * 10 + (p[i] - '0');
	}
	*pp = p + n;
	*out = v;
	return 1;
}

/* ISO-8601-ish absolute timestamp:
 *   (YYYY-MM-DD | YYYYMMDD) [( |T)HH:MM[:SS[.frac]]] [ [ ](Z | +HH[:]MM | -HH[:]MM)]
 * Local time unless a zone is given (then timegm minus the offset). 0/-1. */
static int parse_iso(const char *s, long long *sec, long *nsec)
{
	const char *p = s;
	int y, mo, d;
	if (!rd_digits(&p, 4, &y))
		return -1;
	if (*p == '-') {
		p++;
		if (!rd_digits(&p, 2, &mo) || *p != '-')
			return -1;
		p++;
		if (!rd_digits(&p, 2, &d))
			return -1;
	} else if (rd_digits(&p, 4, &mo)) { /* compact YYYYMMDD: mo holds MMDD */
		d = mo % 100;
		mo = mo / 100;
	} else {
		return -1; /* a bare year is not an ISO date here */
	}
	if (mo < 1 || mo > 12 || d < 1 || d > 31)
		return -1;

	int hh = 0, mi = 0, ss = 0, have_tz = 0;
	long frac = 0, tz = 0;

	/* optional time, separated by a space or T */
	if (*p == ' ' || *p == 'T' || *p == 't') {
		const char *q = p + 1;
		int h, m;
		if (rd_digits(&q, 2, &h) && *q == ':') {
			q++;
			if (!rd_digits(&q, 2, &m))
				return -1;
			hh = h;
			mi = m;
			if (*q == ':') {
				q++;
				if (!rd_digits(&q, 2, &ss))
					return -1;
				if (*q == '.') { /* fractional seconds -> ns */
					q++;
					if (!isdigit((unsigned char)*q))
						return -1;
					long mult = 100000000L;
					while (isdigit((unsigned char)*q)) {
						if (mult)
							frac += (*q - '0') * mult;
						mult /= 10;
						q++;
					}
				}
			}
			p = q; /* consumed a time; else leave p for the tz check */
		}
	}
	if (hh > 23 || mi > 59 || ss > 60)
		return -1;

	/* optional timezone */
	while (*p == ' ')
		p++;
	if (*p == 'Z' || *p == 'z') {
		have_tz = 1;
		p++;
	} else if (*p == '+' || *p == '-') {
		int sgn = (*p == '-') ? -1 : 1;
		int th, tmn;
		p++;
		if (!rd_digits(&p, 2, &th))
			return -1;
		if (*p == ':')
			p++;
		if (!rd_digits(&p, 2, &tmn) || th > 23 || tmn > 59)
			return -1;
		tz = sgn * (th * 3600 + tmn * 60);
		have_tz = 1;
	} else if (isalpha((unsigned char)*p) && p[1] == '\0') {
		char z[2] = {(char)tolower((unsigned char)*p), '\0'};
		int zoff, mz = military_zone(z, &zoff);
		if (mz == 0)
			return -1;
		if (mz == 1) { /* J (mz==2) = local: leave have_tz 0, use mktime */
			tz = zoff;
			have_tz = 1;
		}
		p++;
	}
	if (*p != '\0')
		return -1; /* trailing junk */

	struct tm tm;
	memset(&tm, 0, sizeof tm);
	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = hh;
	tm.tm_min = mi;
	tm.tm_sec = ss;
	tm.tm_isdst = -1;
	time_t t;
	if (have_tz) {
		t = timegm(&tm);
		if (t == (time_t)-1)
			return -1;
		t -= tz;
	} else {
		t = mktime(&tm);
		if (t == (time_t)-1)
			return -1;
	}
	*sec = t;
	*nsec = frac;
	return 0;
}

int frt_datetime_parse(const char *s, struct timespec now, struct timespec *out)
{
	if (!s)
		return -1;
	long long sec;
	long nsec = 0;

	if (s[0] == '@') { /* @EPOCH */
		char *end;
		errno = 0;
		long long v = strtoll(s + 1, &end, 10);
		if (end == s + 1 || *end != '\0' || errno == ERANGE)
			return -1;
		out->tv_sec = (time_t)v;
		out->tv_nsec = 0;
		return 0;
	}

	if (parse_iso(s, &sec, &nsec) == 0) { /* ISO 8601 and its variants */
		out->tv_sec = (time_t)sec;
		out->tv_nsec = nsec;
		return 0;
	}
	if (parse_rel(s, now, &sec, &nsec) != 0) /* named / relative grammar */
		return -1;
	out->tv_sec = (time_t)sec;
	out->tv_nsec = nsec;
	return 0;
}
