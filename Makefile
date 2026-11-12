CC ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
override CFLAGS += -std=c11 -g -Isrc -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS ?=

# libtam is the only library dependency. It is found with pkg-config; add
# <prefix>/lib/pkgconfig to PKG_CONFIG_PATH for a prefix outside pkg-config's
# default search path.
LIBTAM_CFLAGS := $(shell $(PKG_CONFIG) --cflags libtam 2>/dev/null)
LIBTAM_LIBS   := $(shell $(PKG_CONFIG) --libs libtam 2>/dev/null)

VERSION := 0.1.0-c11
PREFIX ?= $(HOME)/.local/dev

BUILD = build
BIN = $(BUILD)/tam-edit
TEST_BIN = $(BUILD)/test_editor
SRCS = src/main.c src/editor.c src/render.c
OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
TEST_SRCS = src/editor.c tests/test_editor.c

.PHONY: all test clean install uninstall check-deps

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	@test -n "$(LIBTAM_LIBS)" || { \
		echo "tam-edit: libtam not found; add its pkg-config directory to PKG_CONFIG_PATH" >&2; \
		exit 1; }
	$(CC) $(CFLAGS) $(LIBTAM_CFLAGS) -MMD -MP -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LIBTAM_LIBS) -o $@

$(TEST_BIN): $(TEST_SRCS) src/editor.h | $(BUILD)
	$(CC) $(CFLAGS) $(LIBTAM_CFLAGS) $(TEST_SRCS) $(LIBTAM_LIBS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

# The finished binary must not pull in a shared libtam, ncurses or terminfo.
check-deps: $(BIN)
	@if ldd $(BIN) | grep -Eq 'libtam|libncurses|libtinfo|libtcell'; then \
		echo "tam-edit: unexpected shared dependency" >&2; \
		ldd $(BIN) >&2; \
		exit 1; \
	fi
	@echo "tam-edit: only the C library is linked"

clean:
	rm -rf $(BUILD)

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/tam-edit

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tam-edit

-include $(OBJS:.o=.d)
