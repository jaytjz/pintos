# Standalone host build for the threads/fixed-point.h unit test.
# Not wired into the tests/threads kernel suite.
#
# Compiled binaries do not travel between environments (a WSL host and the
# Docker container have different glibc), so `check` always recompiles from
# source rather than depending on a possibly-stale binary.

SRCDIR = ../..
CC     = gcc
CFLAGS = -std=gnu99 -Wall -Wextra -g -I$(SRCDIR)

check: test-fixed-point.c $(SRCDIR)/threads/fixed-point.h
	$(CC) $(CFLAGS) -o test-fixed-point test-fixed-point.c
	./test-fixed-point

clean:
	rm -f test-fixed-point

.PHONY: check clean
