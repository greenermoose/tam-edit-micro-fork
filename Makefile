CC ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
override CFLAGS += -std=c11 -g -Isrc -Idata -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
	-DTAM_EDIT_VERSION=\"$(VERSION)\"
LDFLAGS ?=

# Single source of the version. See "Milestones and versions" in ROADMAP.md.
VERSION := 1.0.0
PREFIX ?= $(HOME)/.local/dev

# Look for libtam in $(PREFIX)/lib/pkgconfig if not already in system search path
export PKG_CONFIG_PATH := $(PREFIX)/lib/pkgconfig$(if $(PKG_CONFIG_PATH),:$(PKG_CONFIG_PATH))

# libtam is the only library dependency.
LIBTAM_CFLAGS := $(shell $(PKG_CONFIG) --cflags libtam 2>/dev/null)
LIBTAM_LIBS   := $(shell $(PKG_CONFIG) --libs libtam 2>/dev/null)

BUILD = build
BIN = $(BUILD)/tam-edit
TEST_BIN = $(BUILD)/test_editor
TEST_ASAN_BIN = $(BUILD)/test_asan

CORE_SRCS = src/editor.c src/cursor.c src/undo.c src/actions.c src/prompt.c src/clipboard.c src/render.c src/buffer.c src/pane.c src/config.c src/colorscheme.c src/regex.c src/syntax.c src/encoding.c src/shell.c src/search.c src/complete.c
DATA_SRCS = data/syntax_data.c data/colorschemes_data.c data/help_data.c
SRCS = src/main.c $(CORE_SRCS)
OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))
DATA_OBJS = $(patsubst data/%.c,$(BUILD)/data_%.o,$(DATA_SRCS))
TEST_SRCS = $(CORE_SRCS) $(DATA_SRCS) tests/test_editor.c tests/test_milestone1.c tests/test_milestone2.c tests/test_milestone3.c tests/test_milestone4.c tests/test_milestone5.c

.PHONY: all test test-asan test-pty fuzz clean install uninstall check-deps gen-data

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c Makefile | $(BUILD)
	@test -n "$(LIBTAM_LIBS)" || { \
		echo "tam-edit: libtam not found; add its pkg-config directory to PKG_CONFIG_PATH" >&2; \
		exit 1; }
	$(CC) $(CFLAGS) $(LIBTAM_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/data_%.o: data/%.c Makefile | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BIN): $(OBJS) $(DATA_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(DATA_OBJS) $(LIBTAM_LIBS) -o $@

gen-data:
	go run ./tools/gen_runtime_data.go

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

FUZZ_CFLAGS = $(CFLAGS) -fsanitize=address,undefined $(LIBTAM_CFLAGS)
fuzz: $(CORE_SRCS) $(DATA_SRCS) | $(BUILD)
	$(CC) $(FUZZ_CFLAGS) tests/fuzz/fuzz_regex.c $(CORE_SRCS) $(DATA_SRCS) $(LIBTAM_LIBS) -o $(BUILD)/fuzz_regex
	$(CC) $(FUZZ_CFLAGS) tests/fuzz/fuzz_config.c $(CORE_SRCS) $(DATA_SRCS) $(LIBTAM_LIBS) -o $(BUILD)/fuzz_config
	$(CC) $(FUZZ_CFLAGS) tests/fuzz/fuzz_syntax.c $(CORE_SRCS) $(DATA_SRCS) $(LIBTAM_LIBS) -o $(BUILD)/fuzz_syntax
	$(CC) $(FUZZ_CFLAGS) tests/fuzz/fuzz_input.c $(CORE_SRCS) $(DATA_SRCS) $(LIBTAM_LIBS) -o $(BUILD)/fuzz_input
	$(abspath $(BUILD)/fuzz_regex) tests/fuzz/seeds/regex
	$(abspath $(BUILD)/fuzz_config) tests/fuzz/seeds/config
	$(abspath $(BUILD)/fuzz_syntax) tests/fuzz/seeds/syntax
	$(abspath $(BUILD)/fuzz_input) tests/fuzz/seeds/input
	@echo "tam-edit: all 4 fuzz harnesses passed (ASan+UBSan)"

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
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/man/man1 $(DESTDIR)$(PREFIX)/share/applications
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/tam-edit
	install -m 644 docs/tam-edit.1 $(DESTDIR)$(PREFIX)/share/man/man1/tam-edit.1
	install -m 644 data/tam-edit.desktop $(DESTDIR)$(PREFIX)/share/applications/tam-edit.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tam-edit \
		$(DESTDIR)$(PREFIX)/share/man/man1/tam-edit.1 \
		$(DESTDIR)$(PREFIX)/share/applications/tam-edit.desktop

-include $(OBJS:.o=.d) $(DATA_OBJS:.o=.d)
