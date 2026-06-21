/* frtdate standalone tests. Deterministic under TZ=UTC0 (no DST), so calendar
 * arithmetic via mktime equals timegm and the expected epochs are exact. */
#include "frtdate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int checks, fails;
static struct timespec NOW;

/* Build a UTC epoch from broken-down fields (matches mktime under TZ=UTC0). */
static long long ymd(int y, int mo, int d, int h, int mi, int s)
{
	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = y - 1900;
	t.tm_mon = mo - 1;
	t.tm_mday = d;
	t.tm_hour = h;
	t.tm_min = mi;
	t.tm_sec = s;
	return (long long)timegm(&t);
}

static void eq(const char *s, long long want)
{
	struct timespec out;
	checks++;
	if (frt_datetime_parse(s, NOW, &out) != 0) {
		printf("FAIL [%s]: rejected, want %lld\n", s, want);
		fails++;
		return;
	}
	if ((long long)out.tv_sec != want) {
		printf("FAIL [%s]: got %lld want %lld (off %+lld)\n", s,
		       (long long)out.tv_sec, want, (long long)out.tv_sec - want);
		fails++;
	}
}

static void bad(const char *s)
{
	struct timespec out;
	checks++;
	if (frt_datetime_parse(s, NOW, &out) == 0) {
		printf("FAIL [%s]: accepted (%lld), want reject\n", s, (long long)out.tv_sec);
		fails++;
	}
}

/* Like eq() but also checks the nanosecond field (for fractional seconds). */
static void eqn(const char *s, long long wsec, long wnsec)
{
	struct timespec out;
	checks++;
	if (frt_datetime_parse(s, NOW, &out) != 0) {
		printf("FAIL [%s]: rejected\n", s);
		fails++;
		return;
	}
	if ((long long)out.tv_sec != wsec || out.tv_nsec != wnsec) {
		printf("FAIL [%s]: got %lld.%09ld want %lld.%09ld\n", s, (long long)out.tv_sec,
		       out.tv_nsec, wsec, wnsec);
		fails++;
	}
}

/* Verify the result lands on weekday `wd` (0=Sun) without hardcoding the date. */
static void onwday(const char *s, int wd)
{
	struct timespec out;
	checks++;
	if (frt_datetime_parse(s, NOW, &out) != 0) {
		printf("FAIL [%s]: rejected\n", s);
		fails++;
		return;
	}
	time_t tt = out.tv_sec;
	struct tm t;
	gmtime_r(&tt, &t);
	if (t.tm_wday != wd) {
		printf("FAIL [%s]: landed on wday %d want %d\n", s, t.tm_wday, wd);
		fails++;
	}
}

int main(void)
{
	setenv("TZ", "UTC0", 1);
	tzset();
	NOW.tv_sec = (time_t)ymd(2021, 6, 15, 12, 0, 0); /* Tue 2021-06-15 12:00 UTC */
	NOW.tv_nsec = 0;

	/* @epoch */
	eq("@1000000000", 1000000000);
	eq("@0", 0);
	bad("@");
	bad("@12x");

	/* ISO 8601 */
	eq("2020-01-01", ymd(2020, 1, 1, 0, 0, 0));
	eq("2020-01-01 00:00:00", ymd(2020, 1, 1, 0, 0, 0));
	eq("1970-01-01 00:00:01", 1);

	/* now / today */
	eq("now", NOW.tv_sec);
	eq("today", NOW.tv_sec);

	/* relative (UTC: day = 86400) */
	eq("yesterday", NOW.tv_sec - 86400);
	eq("tomorrow", NOW.tv_sec + 86400);
	eq("2 days ago", NOW.tv_sec - 2 * 86400);
	eq("2 days", NOW.tv_sec + 2 * 86400);
	eq("now-12 hours", NOW.tv_sec - 12 * 3600);
	eq("3 days 2 hours ago", NOW.tv_sec + 3 * 86400 - 2 * 3600);
	eq("1 week ago", NOW.tv_sec - 7 * 86400);
	eq("1 fortnight ago", NOW.tv_sec - 14 * 86400);
	eq("30 minutes ago", NOW.tv_sec - 30 * 60);
	eq("5 seconds ago", NOW.tv_sec - 5);

	/* calendar month / year */
	eq("1 month ago", ymd(2021, 5, 15, 12, 0, 0));
	eq("1 year ago", ymd(2020, 6, 15, 12, 0, 0));

	/* month-name absolute dates (midnight) */
	eq("March 15 2020", ymd(2020, 3, 15, 0, 0, 0));
	eq("Mar 15 2020", ymd(2020, 3, 15, 0, 0, 0));
	eq("15 March 2020", ymd(2020, 3, 15, 0, 0, 0));
	eq("March 15, 2020", ymd(2020, 3, 15, 0, 0, 0));
	eq("MARCH 15 2020", ymd(2020, 3, 15, 0, 0, 0));
	eq("sept 1 2019", ymd(2019, 9, 1, 0, 0, 0));
	eq("Mar 15 2020 14:30", ymd(2020, 3, 15, 14, 30, 0));
	eq("March 15 2020 14:30:05", ymd(2020, 3, 15, 14, 30, 5));
	eq("March 15", ymd(2021, 3, 15, 0, 0, 0)); /* year defaults to NOW's */

	/* time of day */
	eq("00:00", ymd(2021, 6, 15, 0, 0, 0));
	eq("yesterday 10:00", ymd(2021, 6, 14, 10, 0, 0));

	/* day of week (NOW is Tuesday) */
	onwday("monday", 1);
	onwday("tuesday", 2);
	onwday("next friday", 5);
	onwday("last sunday", 0);
	onwday("this wednesday", 3);
	onwday("first monday", 1);   /* spelled-out ordinal weekday */
	onwday("third tuesday", 2);

	/* numeric slash dates (M/D/Y and Y/M/D; 2-digit year; optional time) */
	eq("3/15/2020", ymd(2020, 3, 15, 0, 0, 0));
	eq("03/15/2020", ymd(2020, 3, 15, 0, 0, 0));
	eq("2020/03/15", ymd(2020, 3, 15, 0, 0, 0));
	eq("3/15/20", ymd(2020, 3, 15, 0, 0, 0));
	eq("2020/3/15 12:00", ymd(2020, 3, 15, 12, 0, 0));

	/* AM/PM (12am = 00:00, 12pm = 12:00, else +12 for pm) */
	eq("March 15 2020 6am", ymd(2020, 3, 15, 6, 0, 0));
	eq("March 15 2020 6pm", ymd(2020, 3, 15, 18, 0, 0));
	eq("March 15 2020 2:30pm", ymd(2020, 3, 15, 14, 30, 0));
	eq("March 15 2020 12am", ymd(2020, 3, 15, 0, 0, 0));
	eq("March 15 2020 12pm", ymd(2020, 3, 15, 12, 0, 0));

	/* timezones (offset applied; UTC/GMT = 0). NOW's TZ is UTC0 here, so a
	 * tz-less time at 06:00 also lands at 06:00 UTC — the offsets distinguish. */
	eq("March 15 2020 06:00 UTC", ymd(2020, 3, 15, 6, 0, 0));
	eq("March 15 2020 06:00 GMT", ymd(2020, 3, 15, 6, 0, 0));
	eq("March 15 2020 06:00 +0500", ymd(2020, 3, 15, 1, 0, 0));   /* 06:00 in +0500 = 01:00 UTC */
	eq("March 15 2020 06:00 -0730", ymd(2020, 3, 15, 13, 30, 0)); /* 06:00 in -0730 = 13:30 UTC */

	/* rejects (out of subset or malformed) */
	bad("garbage");
	bad("2 blargs ago");
	bad("ago");
	bad("March 2020");
	bad("Mar 15 2020, 14:30");
	bad("next foo");
	bad("");
	bad("25:00");
	bad("3-15-2020");   /* dashes are ISO-only (Y-M-D); M-D-Y not accepted */
	bad("15.03.2020");  /* dotted dates not accepted */
	bad("3rd");         /* bare ordinal */
	bad("March 3rd 2020"); /* ordinal day-of-month (find rejects too) */
	bad("March 15 2020 06:00 EST"); /* named zone beyond UTC/GMT */
	bad("March 15 2020 13pm");      /* invalid 12-hour clock value */

	/* ISO 8601 variants: T separator, no-seconds, compact, fractional, tz */
	eq("2020-03-15T12:00:00", ymd(2020, 3, 15, 12, 0, 0));
	eq("2020-03-15T18:00", ymd(2020, 3, 15, 18, 0, 0));
	eq("20200315", ymd(2020, 3, 15, 0, 0, 0));
	eq("20200315 18:00", ymd(2020, 3, 15, 18, 0, 0));
	eqn("2020-03-15 12:00:00.5", ymd(2020, 3, 15, 12, 0, 0), 500000000L);
	eq("2020-03-15T06:00:00Z", ymd(2020, 3, 15, 6, 0, 0));
	eq("2020-03-15T06:00:00+05:00", ymd(2020, 3, 15, 1, 0, 0));  /* 06:00 +05 = 01 UTC */
	eq("2020-03-15 06:00 -05:00", ymd(2020, 3, 15, 11, 0, 0));   /* 06:00 -05 = 11 UTC */

	/* next/last/this + UNIT, and bare UNIT = 1 (NOW = Tue 2021-06-15 12:00) */
	eq("next week", ymd(2021, 6, 22, 12, 0, 0));
	eq("last week", ymd(2021, 6, 8, 12, 0, 0));
	eq("this week", NOW.tv_sec);
	eq("last month", ymd(2021, 5, 15, 12, 0, 0));
	eq("next year", ymd(2022, 6, 15, 12, 0, 0));
	eq("week ago", ymd(2021, 6, 8, 12, 0, 0));
	eq("day ago", ymd(2021, 6, 14, 12, 0, 0));
	eq("hour ago", NOW.tv_sec - 3600);

	/* single letters are gnulib military timezones: A=+1h..Z=0, J=local.
	 * A bare zone keeps now's wall-clock fields (no midnight), reinterpreted via
	 * the zone — so "a week ago" (zone A) is "1 week ago" read as UTC+1. */
	eq("a week ago", ymd(2021, 6, 8, 11, 0, 0)); /* 12:00 -7d, as +1 -> 11:00 UTC */
	eq("z week ago", ymd(2021, 6, 8, 12, 0, 0)); /* Z = UTC */
	eq("n week ago", ymd(2021, 6, 8, 13, 0, 0)); /* N = -1h */
	eq("j week ago", ymd(2021, 6, 8, 12, 0, 0)); /* J = local (UTC0 here) */
	eq("March 15 2020 12:00 A", ymd(2020, 3, 15, 11, 0, 0)); /* 12:00 +1 = 11 UTC */
	eq("2020-03-15 12:00 Z", ymd(2020, 3, 15, 12, 0, 0));
	bad("an hour ago"); /* find rejects "an" (two letters, not a military zone) */
	bad("ab week ago"); /* a two-letter non-word is not a zone */
	bad("2020");        /* find reads a bare 4-digit as HH:MM; we don't */

	printf("frtdate: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
