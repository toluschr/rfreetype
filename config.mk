CC ?= gcc
TOPDIR ?= $(or $(shell git rev-parse --show-superproject-working-tree),$(shell git rev-parse --show-toplevel))

CFLAGS += -I $(TOPDIR)/third_party
CFLAGS += -I /usr/include/freetype2

LDFLAGS += -lfreetype -lGL -lraylib
