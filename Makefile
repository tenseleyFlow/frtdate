# frtdate — bespoke date/time parser. Builds a static lib + runs the tests.
# No dependencies beyond libc. -D_GNU_SOURCE exposes strptime/timegm/localtime_r
# on glibc and musl; it is harmless on *BSD/macOS where they are default.

CC      ?= cc
CFLAGS  ?= -O2
WARN     = -std=c11 -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wshadow \
           -Wconversion -Wwrite-strings
CPPFLAGS += -D_GNU_SOURCE -I.

all: libfrtdate.a

frtdate.o: frtdate.c frtdate.h
	$(CC) $(WARN) $(CFLAGS) $(CPPFLAGS) -c -o $@ frtdate.c

libfrtdate.a: frtdate.o
	$(AR) rcs $@ $<

test: test/test
	./test/test

test/test: test/test.c frtdate.c frtdate.h
	$(CC) $(WARN) $(CFLAGS) $(CPPFLAGS) -o $@ test/test.c frtdate.c

clean:
	rm -f frtdate.o libfrtdate.a test/test

.PHONY: all test clean
