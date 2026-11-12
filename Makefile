CC ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
override CFLAGS += -std=c11 -g -Isrc -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
	-DTAM_EDIT_VERSION=\"$(VERSION)\"
LDFLAGS ?=

# libtam is the only library dependency. It is found with pkg-config; add
# <prefix>/lib/pkgconfig to PKG_CONFIG_PATH for a prefix outside pkg-config's
# default search path.
LIBTAM_CFLAGS := $(shell $(PKG_CONFIG) --cflags libtam 2>/dev/null)
LIBTAM_LIBS   := $(shell $(PKG_CONFIG) --libs libtam 2>/dev/null)

# Single source of the version. See "Milestones and versions" in ROADMAP.md.
VERSION := 0.1.0
PREFIX ?= $(HOME)/.local/dev

BUILD = build
BIN = $(BUILD)/tam-edit
TEST_BIN = $(BUILD)/test_editor
TEST_ASAN_BIN = $(BUILD)/test_asan

CORE_SRCS = src/editor.c src/cursor.c src/undo.c src/actions.c src/prompt.c src/clipboard.c src/render.c
SRCS = src/main.c $(CORE_SRCS)
OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
TEST_SRCS = $(CORE_SRCS) tests/test_editor.c tests/test_milestone1.c

.PHONY: all test test-asan test-pty clean install uninstall check-deps

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c Makefile | $(BUILD)
	@test -n "$(LIBTAM_LIBS)" || { \
		echo "tam-edit: libtam not found; add its pkg-config directory to PKG_CONFIG_PATH" >&2; \
		exit 1; }
	$(CC) $(CFLAGS) $(LIBTAM_CFLAGS) -MMD -MP -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LIBTAM_LIBS) -o $@

$(TEST_BIN): $(TEST_SRCS) src/editor.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) $(LIBTAM_CFLAGS) $(TEST_SRCS) $(LIBTAM_LIBS) -o $@

$(TEST_ASAN_BIN): $(TEST_SRCS) src/editor.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(LDFLAGS) $(LIBTAM_CFLAGS) $(TEST_SRCS) $(LIBTAM_LIBS) -o $@

test: $(TEST_BIN)
	$(abspath $(TEST_BIN))

test-asan: $(TEST_ASAN_BIN)
	$(abspath $(TEST_ASAN_BIN))

test-pty: $(BIN)
	python3 tests/test_pty_smoke.py

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
