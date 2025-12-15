TOPDIR ?= $(or $(shell git rev-parse --show-superproject-working-tree),$(shell git rev-parse --show-toplevel))

CC ?= gcc
CFLAGS += -isystem $(TOPDIR)/third_party/lru-cache/include
CFLAGS += -isystem $(TOPDIR)/third_party/raylib/src
CFLAGS += -isystem /usr/include/freetype2

CFLAGS += -I include

.PHONY: all
all: lib/rfreetype.o

.PHONY: clean
clean:
	-rm lib/rfreetype.o

%.o: %.c Makefile
	$(CC) $< $(CFLAGS) -c -o $@
