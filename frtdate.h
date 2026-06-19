#ifndef FRTDATE_H
#define FRTDATE_H

/*
 * frtdate — a bespoke date/time string parser modeled on GNU find's
 * -newerXt/-newermt argument (which find delegates to gnulib parse_datetime).
 * No gnulib, no dependencies beyond libc. Single translation unit.
 *
 * Supported grammar (all relative forms resolve against `now`):
 *   @EPOCH                            seconds since the Unix epoch
 *   YYYY-MM-DD[ HH:MM:SS]             ISO 8601, local time
 *   now | today | yesterday | tomorrow
 *   [now] [+|-] N UNIT ... [ago|hence]   UNIT = sec..year (+plurals, fortnight)
 *   [next|last|this] WEEKDAY          day-of-week (gnulib's ordinal rule)
 *   Month D[, Y] | D Month [Y]        month-name date (year optional)
 *   HH:MM[:SS]                        time of day (today at that time)
 *   and combinations: "yesterday 10:00", "Mar 15 2020 14:30", "next fri 9:00".
 *
 * Calendar arithmetic uses tm + mktime, so month/year offsets and DST match
 * find. Month and weekday names are English regardless of locale (as find does).
 */

#include <time.h>

/* Parse `s` into *out. `now` is the reference instant for relative forms.
 * Returns 0 on success (out written), -1 if the string is not in the grammar. */
int frt_datetime_parse(const char *s, struct timespec now, struct timespec *out);

#endif /* FRTDATE_H */
