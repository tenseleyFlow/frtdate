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
- `YYYY-MM-DD` / `YYYY-MM-DD HH:MM:SS` — ISO 8601, local time.
- `now`, `today`, `yesterday`, `tomorrow`.
- `[now] [+|-] N UNIT ... [ago|hence]` — `UNIT` is sec/min/hour/day/week/
  fortnight/month/year (and plurals). `ago`/`hence` bind to the preceding unit,
  so `3 days 2 hours ago` is `+3d −2h` (matching gnulib).
- `[next|last|this|first..twelfth] WEEKDAY` — day of week, resolved with
  gnulib's ordinal rule.
- `Month D[, Y]` / `D Month [Y]` — month-name dates (English, case-insensitive,
  abbreviations; year defaults to the current year).
- `M/D/Y` / `Y/M/D` — numeric slash dates (2- or 4-digit year, optional time).
- `HH:MM[:SS]`, `Nam`/`Npm`/`N:MMpm` — time of day (today), 12-hour clock.
- Timezones: numeric offsets (`+0500`, `-0730`) and `UTC`/`GMT`/`UT`/`Z`.
- Combinations: `yesterday 10:00`, `Mar 15 2020 2:30pm`, `next fri 9:00`,
  `3/15/2020 06:00 +0500`.

Offsets use `tm` + `mktime`, so month/year arithmetic and DST behave like find;
an explicit timezone resolves via `timegm` minus the offset. Month and weekday
names are English regardless of locale, as find's are.

Returns -1 (matching find, which rejects most of these too): named timezones
beyond UTC/GMT (`EST`, `PDT`), dashed/dotted numeric dates (`3-15-2020`,
`15.03.2020`), ordinal days of month (`March 3rd`), 2-digit years in month-name
dates (`March 15 20`), and the rest of gnulib's long tail.

## Build

```sh
make        # libfrtdate.a
make test   # build and run the unit tests
```

Needs a C11 compiler. `_GNU_SOURCE` is set by the Makefile to expose
`strptime`/`timegm`/`localtime_r` on glibc and musl.

## License

MIT. See [LICENSE](LICENSE).
