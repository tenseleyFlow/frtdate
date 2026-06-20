# frtdate

A small, dependency-free C library that parses date/time strings the way GNU
`find`'s `-newerXt` / `-newermt` arguments do — without gnulib. find delegates
those to gnulib's `parse_datetime`; frtdate reimplements the common slice of that
grammar from scratch in one translation unit (`frtdate.c`, libc only).

Built for [ferret](https://github.com/tenseleyFlow/ferret) (a find clone), but
self-contained and usable on its own.

## API

```c
#include "frtdate.h"

struct timespec now, out;
clock_gettime(CLOCK_REALTIME, &now);
if (frt_datetime_parse("2 weeks ago", now, &out) == 0)
        /* out.tv_sec / out.tv_nsec hold the resolved instant */;
```

Returns 0 on success, -1 if the string is not in the supported grammar.

## Grammar

- `@EPOCH` — seconds since the Unix epoch.
- ISO 8601: `YYYY-MM-DD` or compact `YYYYMMDD`; optional ` `/`T` then
  `HH:MM[:SS[.frac]]`; optional `Z` or `±HH[:]MM` offset. Local time unless a
  zone is given.
- `now`, `today`, `yesterday`, `tomorrow`.
- `[now] [+|-] N UNIT ... [ago|hence]` — `UNIT` is sec/min/hour/day/week/
  fortnight/month/year (and plurals); a bare unit means one (`week ago`).
  `ago`/`hence` bind to the preceding unit, so `3 days 2 hours ago` is `+3d −2h`.
- `[next|last|this] UNIT` — `next week`, `last month`, `this year`.
- `[next|last|this|first..twelfth] WEEKDAY` — day of week, gnulib's ordinal rule.
- `Month D[, Y]` / `D Month [Y]` — month-name dates (English, case-insensitive,
  abbreviations; year defaults to the current year).
- `M/D/Y` / `Y/M/D` — numeric slash dates (2- or 4-digit year, optional time).
- `HH:MM[:SS]`, `Nam`/`Npm`/`N:MMpm` — time of day (today), 12-hour clock.
- Timezones: numeric offsets (`+0500`, `+05:00`, `-0730`) and `UTC`/`GMT`/`UT`/`Z`.
- Combinations: `yesterday 10:00`, `Mar 15 2020 2:30pm`, `next fri 9:00`,
  `2020-03-15T06:00:00+05:00`.

Offsets use `tm` + `mktime`, so month/year arithmetic and DST behave like find;
an explicit timezone resolves via `timegm` minus the offset. Month and weekday
names are English regardless of locale, as find's are.

Returns -1 (matching find, which rejects most of these too): named timezones
beyond UTC/GMT (`EST`, `PDT` — gnulib's full table interacts with the local zone,
not portably reproducible), dashed/dotted numeric dates, ordinal days of month
(`March 3rd`), 2-digit years in month-name dates (`March 15 20`), a bare 4-digit
number as a clock time (`2020` → 20:20), and the `a`/`an` article (`a week ago`,
which gnulib resolves to midnight). These are the quirky tail; the common forms
are all covered.

## Build

```sh
make        # libfrtdate.a
make test   # build and run the unit tests
```

Needs a C11 compiler. `_GNU_SOURCE` is set by the Makefile to expose
`strptime`/`timegm`/`localtime_r` on glibc and musl.

## License

MIT. See [LICENSE](LICENSE).
