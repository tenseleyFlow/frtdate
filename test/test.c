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

	/* rejects (out of subset or malformed) */
	bad("garbage");
	bad("2 blargs ago");
	bad("ago");
	bad("March 2020");
	bad("Mar 15 2020, 14:30");
	bad("next foo");
	bad("");
	bad("25:00");

	printf("frtdate: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
