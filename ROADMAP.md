# tam-edit C11 migration roadmap

**Last update:** 2026-11-13
**Owner:** Fred
**Status:** version 1.0.3, Milestone 5 complete: parity gate, stabilization, fuzzing, and packaging
**Target branch:** `c11` in `greenermoose/tam-edit`
**Starting point:** the Go tam-edit fork at commit `85beac1e` (micro 2.0.15 lineage)

This roadmap describes how tam-edit moves from Go to C11 and links only
`libtam` and libc. It is milestone-based, not date-based. The Go version stays
usable on `master` until the C version reaches the 1.0.0 parity gate.

## Status

**2026-11-13 — version 1.0.0 (Milestone 5 exit: parity gate, stabilization & packaging).**
Phase 7 completes the C11 migration and satisfies the Section 10 definition of done:
- Parity audit & fixes (`docs/PARITY_AUDIT.md`, `src/editor.c`, `src/actions.c`, `src/main.c`):
  complete audit of commands, settings, default keybindings, and CLI flags with
  intentional v1 omissions documented; fixed `editor_raw_insert` trailing-newline
  handling, `editor_textfilter` trailing-newline trimming, `Ctrl-g` help binding,
  and `+LINE[:COL]` startup jump.
- Ported corpus & stress tests (`tests/ported_buffer_fixtures.h`, `tests/test_milestone5.c`):
  57 ported VSCode/micro buffer edit cases and stress tests for 50,000-line files,
  20,000-byte UTF-8 softwrapped lines, 220 multi-cursors, and 16 splits across 4 tabs.
- Fuzzing (`tests/fuzz/`, `make fuzz`): ASan+UBSan fuzz harnesses and seed corpora
  for `tam_regex`, INI/JSON5 config, flat-YAML syntax overrides, and `tam_key`/input.
- Packaging & cleanup: man page (`docs/tam-edit.1`), desktop entry
  (`data/tam-edit.desktop`), `install`/`uninstall` Makefile targets, and Go
  reference tree pruned from `c11` (retained on `master`).
- Benchmarks (`tools/bench_compare.py`): binary size `1.44 MB` (`<= 5 MB`),
  80×24 startup `3.25 ms` (`<= 25 ms`), 80×24 keystroke `0.05 ms` (`<= 16 ms`),
  10k-line keystroke `0.03 ms` (`<= 50 ms`), 10k-line `VmHWM` `4.81 MB` (`<= 15 MB`).

**2026-11-13 — version 0.4.0 (Milestone 4 exit: full editing, search, encodings, shell & multi-cursor).**
Phase 6 delivers the full non-plugin micro feature set in C11 while linking only
libc and libtam 0.7.0:
- Regex captures & search/replace (`src/regex.{h,c}`, `src/search.{h,c}`):
  `(...)` and `(?P<name>...)` capture groups, `$0`/`$1`/`${1}`/`$name`/`${name}`/`$$`
  replacement templates, `ignorecase`, `incsearch` with live cursor preview and
  `Esc` restore, `hlsearch` match highlighting, literal find (`Alt-F`), and
  interactive `replace` / grouped-undo `replaceall`.
- Encodings & file lifecycle (`src/encoding.{h,c}`, `src/buffer.{h,c}`):
  glibc `iconv` decoding/encoding (no `-liconv`), BOM (`UTF-8`, `UTF-16LE`,
  `UTF-16BE`) + UTF-8 + configured encoding + `windows-1252`/`iso-8859-1`
  fallback detection, strict save on `EILSEQ`, `reopen`, `mkparents`,
  `rmtrailingws`, `eofnewline`, `retab`, and external `reload` (`prompt`/`auto`/`disabled`).
- Shell execution & completion (`src/shell.{h,c}`, `src/complete.{h,c}`,
  `src/main.c`): non-blocking background `run` via `poll()` with 30s timeout,
  bounded synchronous `textfilter`, foreground TTY suspend/restore (`run -i` /
  `ShellMode`), and `Tab`/`Shift-Tab` completion for commands, options, values,
  encodings, colorschemes, filetypes, help topics, and filesystem paths.
- Multi-cursor & editing parity (`src/editor.{h,c}`, `src/cursor.c`,
  `src/actions.c`, `src/prompt.c`, `src/render.c`, `data/help_data.{h,c}`):
  `SpawnMultiCursor` (`Alt-N`), `SkipMultiCursor` (`Alt-M`),
  `RemoveMultiCursor` (`Alt-X`), `RemoveAllMultiCursors` (`Esc`),
  `SpawnMultiCursorUp`/`Down` (`Alt-Shift-Up`/`Down`),
  `SpawnMultiCursorSelect` (`Alt-C`), `Ctrl-LeftClick` cursor toggle,
  `MoveLinesUp`/`Down`, `DeleteLine`, `Duplicate`, `ToggleOverwriteMode`,
  `scrollmargin`, `tabmovement`, `smartpaste`, `bind`/`unbind`/`show`/`showkey`/`reset`,
  `tabmove`/`tabswitch`, and embedded `help` topics.
- Verification: all unit tests (Editor, Milestone 1–4) pass under `-O2` and
  `-fsanitize=address,undefined` with zero warnings or leaks; 8 PTY/CLI smoke
  tests pass; `make check-deps` confirms libc-only linkage; `build/tam-edit` =
  1.5 MB unstripped (`<= 5 MB`).

Not yet present: Phase 7 parity audit, fuzzing, stress tests, man page/desktop
packaging, and `v1.0.0` release gate.

**2026-11-13 — version 0.3.0 (Milestone 3 exit: syntax highlighting).**
Phase 5 delivers native UTF-8 syntax highlighting with embedded runtime tables,
user YAML override parsing, and micro-parity line-state highlighting:
- `tam_regex` engine (`src/regex.h`, `src/regex.c`): standalone UTF-8 Thompson-NFA /
  Pike-VM regex module (specced for future extraction to `libtam`), supporting
  literals, classes, anchors, `(?:...)` non-capturing groups, alternation, greedy
  and lazy quantifiers, bounded `{m,n}`, `\b`/`\B`, `\d`/`\D`/`\w`/`\W`/`\s`/`\S`,
  POSIX classes (`alnum`, `alpha`, `digit`, `graph`, `space`, `xdigit`), inline
  `(?i)`/`(?-i)`/`(?i:...)` flags, and `\p{Sm}` Unicode math symbols, with
  guaranteed linear-time matching.
- Embedded runtime data (`tools/gen_runtime_data.go`, `data/`): maintainer-only
  Go converter compiles all 158 `runtime/syntax/*.yaml` definitions and all 25
  `runtime/colorschemes/*.micro` schemes into committed C tables (`make` does
  not invoke Go).
- Syntax engine & flat-YAML user overrides (`src/syntax.h`, `src/syntax.c`):
  micro-parity filetype detection (extension/filename, header regex, signature
  disambiguation with `detectlimit`), transitive `include` resolution, region
  state machine (`start`, `end`, `skip`, `limit-group`, nested regions),
  O(1) colorscheme `group_cache[512]`, 128-slot viewport line cache, incremental
  `buffer_syntax_on_edit` convergence, and runtime loading of user overrides
  from `~/.config/tam-edit/syntax/*.yaml` (`TAM_EDIT_CONFIG_HOME`).
- Runtime commands & settings: `syntax` and `detectlimit` settings, `set` /
  `setlocal` / `toggle` / `togglelocal` support for `syntax` and `filetype`,
  statusline filetype indicator, and `--dump-syntax FILE` CLI inspection.
- Verification & performance: 32 C unit tests passing under both `-O2` and
  `-fsanitize=address,undefined` with zero leaks; 15-file / 83,628-rune
  differential parity suite against Go `pkg/highlight` with 0 mismatches;
  7 PTY smoke tests passing; libc-only linkage verified. Measured on a 10,000-line
  C file: `build/tam-edit` = 1,012 KB unstripped (`<= 5 MB`), cold PTY startup to
  first painted frame = 4.79 ms (`<= 25 ms`), interactive `VmHWM` = 5,152 kB
  (~5.0 MB, `<= 15 MB`), full 10k-line EOF cold scan + viewport highlight = 82.66 ms
  (< 0.5 ms per 50-line viewport scroll, `<= 50 ms`).

Not yet present: shell execution, file operations/completion, regex search/replace,
and legacy encodings (Milestone 4 / Phase 6).

**2026-11-12 — version 0.2.0 (Milestone 2 exit: views and configuration).**
Phase 4 delivers a decoupled buffer/view architecture, multi-split panes,
tabs, soft wrap, truecolor colorscheme support, native INI configuration,
and micro configuration import:
- Decoupled buffer and view architecture: document and undo stack managed in
  `buffer_t` with reference counting (`buffer_ref`/`buffer_unref`) allowing multiple
  views to share a single buffer and edit concurrently.
- Split pane tree: binary layout tree (`pane_node_t`) supporting recursive vertical
  (`vsplit`) and horizontal (`hsplit`) splits, proportional resizing, view navigation
  (`Ctrl-W` / `:nextsplit`), unsplit (`:unsplit`), and mouse hit-testing.
- Multi-tab management: tab list (`tab_list_t`), tab switching (`Alt-.`, `Alt-,`,
  `Ctrl-PageDown`, `Ctrl-PageUp`, `:tabnext`, `:tabprev`), tab creation (`Ctrl-T`,
  `:tab`), tab closing (`:tabclose`), and automatic tab bar rendering at row 0.
- Softwrap and wordwrap: visual sub-line coordinate translation
  (`edit_softwrap_loc_to_vloc`, `edit_softwrap_vloc_to_rune`, `edit_softwrap_line_sublines`),
  vertical navigation preserving visual goal column across softwrapped lines, and
  toggling (`Alt-W`, `:toggle softwrap`, `:togglelocal softwrap`).
- Colorscheme engine: full parser for micro's `color-link` format (hex `#RRGGBB`,
  16 ANSI names, 256 palette numbers, text attributes `bold`/`reverse`/`underline`/`italic`),
  hierarchical group fallback, include recursion protection, embedded `monokai` fallback,
  and truecolor ANSI escape sequences rendered to the cell grid.
- Native INI configuration: `tam_ini` reader and writer for `~/.config/tam-edit/settings.ini`
  with `[settings]` and `[bindings]`, setting validation, runtime commands (`set`,
  `setlocal`, `toggle`, `togglelocal`), and `--check-config` validation tool.
- Micro configuration importer: `--import-micro-config [DIR]` with lenient JSON5 parser
  (handling line/block comments, trailing commas, unquoted keys, single quotes) that
  migrates settings and bindings into INI format with `.bak` safety backups.
- Verification: 24 unit tests passing, ASan/UBSan clean with zero leaks, 5 PTY
  interactive smoke test scenarios passing, and dependency check confirming only libc
  is linked.

Not yet present: syntax highlighting (Milestone 3), shell execution, encodings,
and multi-cursor editing (Milestone 4).

**2026-11-12 — version 0.1.0 (Milestone 1 exit: daily-usable editor).**
Phase 3 delivers a complete daily-usable terminal editor linking only libc and
libtam 0.7.0. Features delivered:
- Double-buffered cell-grid renderer with damage tracking, wide and combining
  character support, and synchronized update framing (`tam_tty_frame_begin/end`).
- Grouped undo and redo matching micro's 1000ms threshold grouping rules.
- Visual selections with anchor and active cursors, shift-motion selections,
  and replacement on typing.
- Word motions (`WordLeft`, `WordRight`) and word deletions (`DeleteWordLeft`).
- Action registry and keybinding table matching micro's defaults (`bufdefaults`).
- Interactive prompts: command prompt (`Ctrl-E`), go-to-line (`Ctrl-L`),
  search prompt (`Ctrl-F` with `Ctrl-N` and `Ctrl-P`), save-as, and dirty-quit.
- Clipboard integration with OSC 52, external tools (`wl-copy`/`xclip`), and
  internal clipboard buffer.
- SGR mouse support (click, drag-selection, scroll wheel) and bracketed paste.
- Terminal signals: resize (`SIGWINCH`), suspend (`SIGTSTP`) and resume (`SIGCONT`).
- Verification: 18 unit tests, ASan/UBSan memory sanitization clean, PTY
  interactive smoke tests pass, and dependency checks confirm libc only.

Not yet present: splits and tabs, native INI settings and micro config importer
(Milestone 2), syntax highlighting (Milestone 3), shell execution and encodings
(Milestone 4).

**2026-11-12 — version 0.0.1 (first runnable editor).** `make` builds
`build/tam-edit`, which links only libc and libtam 0.7.0. It opens a file,
edits UTF-8 text (arrows, Home, End, PageUp, PageDown, Enter, Tab,
Backspace, Delete), saves with Ctrl-S, quits with Ctrl-Q, shows a status line
and keeps the cursor visible. CRLF files keep their line endings, a missing
file opens empty, and a modified buffer requires a second Ctrl-Q. Tests:
`make test`, `make check-deps`, and a pseudo-terminal smoke test covering
insert, save, quit, dirty-quit and UTF-8.

Not yet present: undo, search, multiple cursors, splits and tabs, syntax
highlighting, colorschemes, INI settings, mouse, clipboard and the micro
config importer. The Go tree is still on this branch and on `master`; it is
not built by the new Makefile and is removed once later phases land.

Libtam 0.7.0 supplies `tam_uni`, `tam_key` and `tam_tty`; `tam_regex`, which
Phase 1 also requires, is not there yet. Phase 0 scaffolding and the
document/editing core of Phase 2 are the parts delivered so far, so no milestone
is complete and the version stays in 0.0.x.

## 1. Goal and non-goals

**Goal.** A native C11 tam-edit that provides micro's core editing experience,
builds with a C11 compiler, `make`, `pkg-config` and `libtam`, and links no
other library. The finished binary needs only libc at run time.

"Core editing experience" means buffers, multiple cursors, selections, undo,
splits and tabs, search and replace, syntax highlighting, colorschemes,
configurable keybindings, mouse, clipboard, shell commands and file operations.

**Non-goals for v1.**

- Lua-compatible plugins and the plugin manager.
- The embedded terminal pane and its VT emulator.
- Diff view, linter integration and the bundled plugin set.
- Windows console support, a GUI, a daemon or network features.
- Full grapheme-cluster fidelity for emoji sequences; v1 handles code points,
  combining marks and wide characters.
- Terminfo-based capability discovery; v1 speaks ANSI/xterm sequences.

Deferred work is listed in section 9, not dropped.

## 2. Decisions recorded on 2026-11-12

| Decision | Choice |
| --- | --- |
| v1 scope | Core editor without plugins or the embedded terminal |
| Implementation style | Idiomatic C with micro's behavior and data, not a mechanical package translation |
| libtam boundary | libtam grows terminal, input, Unicode and regex modules; the editor, renderer and actions stay in tam-edit |
| Configuration | Native INI through `tam_ini`, plus a one-time importer for micro settings |
| Repository | C11 tree on a `c11` branch; `master` stays the Go fork for upstream sync and parity reference |
| License | GPLv3 for the linked work, retaining micro's MIT copyright and permission notices |
| Roadmap location | `~/Code/tam-edit` (the existing clone) |
| Versioning | Semver; the minor names a milestone while pre-1.0, and 1.0.0 is the parity gate. See "Milestones and versions" in section 6 |

## 3. Starting point

Measured from the Go tree before any C11 work:

| Item | Size |
| --- | --- |
| Go source | 25,265 lines, including 2,859 lines of tests in 12 files |
| `internal/action` | 7,303 lines |
| `internal/buffer` | 6,838 lines |
| `internal/config` | 2,449 lines |
| `internal/display` | 2,180 lines |
| `internal/lua` | 603 lines |
| Runtime data | Originally 210 files, including 158 syntax YAML files, 25 colorschemes, help pages and 7 Lua plugins |
| Highlight engine | 986 lines in `pkg/highlight` |
| External libraries | 18 direct and indirect modules |

The cleanup retains the Go source and tests, syntax definitions, colorschemes,
help pages and `tools/testgen.go` as porting references. It removes micro's
release automation, packaging, artwork and bundled Lua plugins. The Go embed
list matches the retained data; `mise.toml` keeps reference builds reproducible.
`master` and Git history retain the original material for comparisons.

The existing clone is `~/Code/tam-edit`. `~/Clone` does not exist. The system
`micro` (Pacman package, `/usr/bin/micro`) stays installed as the behavior
reference and as the editor used by `micro-review`.

`libtam` is at 0.6.0 and provides strings, subprocess execution, filesystem
helpers, paths, INI parsing, record parsing, SHA-256, filesystem walking,
logging and the bundle guard. It does not yet provide terminal raw mode,
escape-sequence decoding, Unicode width, a regex engine, an editor model or a
renderer. The `tam` pager implements its own input and drawing but links ncurses
for terminfo, which tam-edit will not do.

## 4. Target architecture

### Repository layout on `c11`

```text
src/
  main.c          startup, command line, top-level loop
  app.c           modes, event dispatch, suspend and resume
  document.c      line array, loading, saving, encoding, line endings
  cursor.c        cursors, selections, motions, marks
  undo.c          grouped undo and redo
  buffer.c        buffer and view binding, options, file metadata
  pane.c          split tree, tabs, layout geometry
  render.c        cell grid, damage tracking, terminal flush
  draw.c          text, gutter, statusline, tabbar, infobar, prompts
  input.c         keybinding table, key dispatch, mouse
  actions.c       command registry and completion
  cmd_edit.c      insertion, deletion, selection, indentation
  cmd_file.c      open, save, save-as, reopen, file operations
  cmd_view.c      splits, tabs, scrolling, wrapping
  cmd_search.c    search and replace
  cmd_shell.c     shell commands, filters, quoting
  config.c        INI settings, keybindings, importer
  colorscheme.c   color-link parsing and style lookup
  syntax.c        highlighting state machine and detection
  clipboard.c     OSC 52 and external clipboard helpers
  util.c          shared helpers
include/
data/             generated C tables and embedded runtime files
tools/            maintainer converters and parity tools
tests/            unit, integration, golden-screen and fuzz seeds
ROADMAP.md
LICENSE           GPLv3, with micro's MIT notice preserved
README.md
Makefile
```

The build is a plain Makefile using `pkg-config libtam`, following the pattern
already used by `tam`. `make`, `make test` (ASan and UBSan), `make install`,
`make uninstall` and `make check-deps` are required targets. No build step runs
Go, Python or a data converter; generated data is committed.

### libtam additions

A new libtam minor release (0.7.0 in this plan) carries:

- **`tam_tty`**: raw mode with guaranteed restore, window size, `SIGWINCH`,
  alternate screen, cursor visibility, mouse, bracketed paste and focus
  reporting, suspend and resume, title changes, OSC 52 clipboard output,
  capability detection from `TERM`, `COLORTERM` and `NO_COLOR`. ANSI and xterm
  sequences only; no terminfo.
- **`tam_key`**: byte-stream decoder for UTF-8 text, control keys, CSI and SS3
  sequences, modifiers and SGR mouse events, with an escape timeout and a
  replay queue.
- **`tam_uni`**: UTF-8 decode and encode, display width, combining marks,
  truncation and padding to a cell count.
- **`tam_regex`**: a small Thompson-NFA engine covering the dialect the syntax
  files actually use: literals, classes, anchors, groups, alternation, greedy
  and lazy quantifiers, `\b`, `\d`, `\w`, `\s`, and inline `(?i)` flags.
  Backreferences and lookaround are out of scope; an audit found no syntax file
  that genuinely needs them.

These modules are separate archives or objects so `tam` can adopt them without
pulling in editor code. Each ships with tests, sanitizer coverage and fuzzing.

### Editor architecture

- **Document:** array of lines, each a growable byte buffer. Positions use a
  line number and a rune column, matching micro's semantics and its test
  vectors. Splits and tabs share one document through reference counting.
- **Undo:** micro's event-handler model, grouping adjacent insertions and
  deletions by time and operation type.
- **Buffer and view:** a buffer owns cursors, options, marks and file
  metadata; a view owns scroll position and geometry. Multiple views may share
  a buffer, with optional scroll synchronization.
- **Renderer:** a cell grid in memory, one cell per terminal column, with a
  damage diff against the previous frame. Wide characters occupy two cells;
  combining marks attach to the preceding cell. Colors are 8, 16, 256 or
  truecolor depending on detected capability.
- **Event loop:** `poll` on the terminal, a self-pipe for redraw requests and
  signals, and timers for message expiry and prompt behavior. No threads are
  required for v1.
- **Actions:** a registry of named commands with argument completers, shared by
  keybindings, the command prompt and the test driver.
- **Syntax:** micro's line-state engine, fed by generated tables from the 158
  YAML definitions. Highlighting is incremental per visible line and cancelled
  when the viewport moves.

## 5. Dependency elimination

| Go dependency | Used for | C11 replacement | Phase |
| --- | --- | --- | --- |
| `micro-editor/tcell` | Terminal, input, rendering | `tam_tty`, `tam_key`, `tam-edit` renderer | 1–3 |
| Go `regexp` | Syntax and search | `tam_regex` | 1, 5 |
| `go-runewidth`, `uniseg` | Character width | `tam_uni` | 1 |
| `yaml.v2` | Syntax definitions | Maintainer converter; generated C tables | 5 |
| `micro-editor/json5` | Settings, keybindings | INI via `tam_ini`; minimal JSON5 reader only for import | 4 |
| `zyedidia/clipper` | Clipboard | OSC 52 plus optional external helpers through `tam_exec` | 3 |
| `go-shellquote` | Shell argument splitting | In-tree splitter | 6 |
| `x/text` encodings, `htmlindex` | Charset detection | UTF-8 and UTF-16 first; `iconv` from libc for others | 6 |
| `zyedidia/glob` | File matching | libc `fnmatch` plus `**` handling | 4 |
| `blang/semver` | Version checks | Drop or a small compare helper | 6 |
| `sergi/go-diff` | Diff view | Deferred with the diff plugin | 8 |
| `micro-editor/terminal` | Embedded terminal | Deferred; VT parser later | 8 |
| `gopher-lua`, `gopher-luar` | Plugins | Deferred; see section 9 | 8 |
| `testify` | Tests | In-tree C test harness | 0 |
| Go standard library | Buffers, maps, JSON, exec, regex | libtam plus in-tree C | all |

The build check is simple: `ldd tam-edit` must list libc only, and
`make check-deps` asserts it.

## 6. Phases

Effort is relative: **S** days, **M** weeks, **L** several weeks, **XL** a
month or more of focused work. These are sizing hints, not promises.

### Milestones and versions

tam-edit uses semantic versioning, `MAJOR.MINOR.PATCH`. Before 1.0.0 the minor
number names a milestone, and a milestone is the exit of a phase below. Phases
may land out of order; the version changes only when a milestone's exit
criteria hold.

| Milestone | Phases | Version | Exit |
| --- | --- | --- | --- |
| — | 0–2 | 0.0.x | Scaffolding, libtam foundation and the document core. Today 0.0.1. |
| 1 | 3 | 0.1.0 | Daily-usable editor: open, edit, save, quit, no sanitizer findings. |
| 2 | 4 | 0.2.0 | Views and configuration: splits, tabs, INI, colorschemes, micro import. |
| 3 | 5 | 0.3.0 | Syntax highlighting for the 158 definitions within the latency target. |
| 4 | 6 | 0.4.0 | Full non-plugin feature set: shell, file operations, search, encodings. |
| 5 | 7 | 1.0.0 | Parity gate: audit, fuzzing, packaging, `c11` becomes the default branch. |

Rules:

- `PATCH` increments for fixes and small improvements inside a milestone.
- `MINOR` increments only at a milestone exit, up to 0.4.0.
- `1.0.0` is reserved for the parity gate. Nothing reaches 1.0 before the
  definition of done in section 10 holds and the Go tree is no longer needed.
- After 1.0.0, `MINOR` adds a capability (an extension from Phase 8) and
  `PATCH` fixes defects.
- Release candidates use `-rc.N`, as in `0.1.0-rc.1`. No branch, toolchain or
  build-count suffix is part of a version; the old `0.1.0-c11` form is dropped.
- The Makefile's `VERSION` is the single source. It is compiled into the binary,
  and `tam-edit --version` prints it beside the libtam version.
- Releases are tagged `vX.Y.Z` on `c11`, later on the default branch. Tags
  inherited from upstream micro (`v1.x`, `v2.0.x`, `nightly`) describe the Go
  tree and are not tam-edit releases.

### Phase 0 — Branch, baseline, and scaffolding (S)

- Create `c11` from `master`; leave `master` untouched as the Go reference.
- Record the decisions in this roadmap on `c11`.
- Measure Go micro: cold and warm startup, RSS with small and large files,
  keystroke latency, binary size. These become the comparison baseline.
- Scaffold `src/`, `include/`, `tests/`, `tools/`, the Makefile and the license
  files. Confirm `pkg-config libtam` works from the development prefix.
- Prove the data pipeline with one syntax file and one colorscheme.
- Build the headless test driver skeleton and the C test harness.

**Exit:** `make` produces a binary that prints its version, handles `--help`,
creates `~/.config/tam-edit`, and exits cleanly. `make test` runs under ASan and
UBSan. Baseline numbers are recorded in this file or a linked note.

### Phase 1 — libtam terminal foundation (L)

Deliver `tam_tty`, `tam_key`, `tam_uni` and `tam_regex` as described in section
4, with unit tests, fuzzing and a small demonstration program that switches to
raw mode, reads keys and paints a cell grid.

**Exit:** libtam's suite passes under ASan and UBSan; the four modules are
documented in libtam's headers and docs; a development build is installed under
the user prefix for tam-edit to link against.

### Phase 2 — Document and editing core (L)

- Load and save UTF-8, LF and CRLF files, BOM handling, permissions and atomic
  writes through `tam_fs_write_atomic`.
- Line array, insertion, deletion, splitting and joining.
- Cursors, selections, multiple cursors, marks and visual x memory.
- Undo and redo with micro's grouping rules.
- Port micro's generated buffer tests. `tools/testgen.go` already generates
  them from VSCode model tests; extend it to emit C fixtures and commit the
  output.

**Exit:** a headless driver applies scripted edits and dumps document and
cursor state. Ported tests and fuzz invariants pass. No leaks under valgrind.

### Phase 3 — Screen, input and a usable editor (XL)

- Cell-grid renderer with damage tracking, wide and combining characters,
  colors, cursor placement and synchronized updates where supported.
- Event loop with resize, suspend and resume, mouse and bracketed paste.
- Action registry, keybinding table and command prompt.
- Basic actions: insert, newline, backspace, delete, arrows, word and line
  motions, page motions, home and end, selection, copy, cut, paste, undo,
  redo, save, quit, open, go-to-line, search and quit prompts.
- Statusline, infobar and prompt line.
- Clipboard through OSC 52 and external helpers.

**Exit:** tam-edit opens a file, edits it interactively, saves it and quits,
with no leaks or sanitizer findings. This is the first daily-usable build.

### Phase 4 — Views and configuration (M–L)

- Split tree, tabs, tab bar, scrolling, soft wrap and scroll synchronization.
- INI settings and keybindings through `tam_ini`, with validation and
  `--check-config`.
- Colorscheme parsing for micro's `color-link` format; reuse the 25 existing
  files.
- One-time `--import-micro-config` from `~/.config/micro` (JSON5 subset,
  comments, trailing commas and single quotes accepted), reporting any setting
  it cannot map.

**Exit:** the documented settings and keybindings work; import produces a valid
configuration from Fred's existing micro setup; split and tab scenarios pass.

### Phase 5 — Syntax highlighting (L)

- Maintainer converter from micro's YAML subset to C tables. Generated tables
  and runtime data are committed; the normal build does not run the converter.
- Port micro's highlighting state machine, including regions, `start`, `end`,
  `skip`, nested rules, `limit` and filetype detection by filename and header.
- Embed the 158 syntax definitions and 25 colorschemes, with an override
  directory under `~/.config/tam-edit`.
- Highlight only visible lines, incrementally, with a bounded per-line budget.

**Exit:** a corpus with representative files per language produces the same
highlight groups as micro; highlighting a 10,000-line file stays within the
latency target while scrolling.

### Phase 6 — Shell, file operations, search and encodings (M–L)

- Shell command execution through `tam_exec` for non-interactive commands, and
  a terminal-restoring path for interactive commands, matching micro's
  `TermMessage` behavior.
- Shell quoting and argument splitting without a shell where possible.
- File operations: open with completion, save-as, reopen, rename, delete and
  directory listing.
- Search and replace with literal and regex modes, incremental search and
  match highlighting.
- UTF-16 and `iconv`-based encodings, with detection order documented.

**Exit:** the non-plugin feature checklist from micro's documentation is
complete; each item has a manual test and, where practical, an automated one.

### Phase 7 — Stabilization and v1 (M)

- Parity audit against micro's commands, settings, options and default
  keybindings; record intentional differences.
- Fuzzing, ASan and UBSan clean; valgrind clean on the main workflows.
- Large-file, long-line, many-cursor and many-split stress tests.
- Packaging: a man page and desktop file of tam-edit's own, install and
  uninstall targets, pkg-config if useful.
- GPLv3 `LICENSE`, retained MIT notices, README, acknowledgements and AI
  provenance if the repository is treated as a public Tamlinux project.
- With Fred's approval, make `c11` the default branch and mark `master` as the
  archived Go reference.

**Exit:** v1.0.0 tag; the dependency check passes; the Go tree is no longer
required to build, test or use tam-edit.

### Phase 8 — After v1 (XL, optional)

Choose one extension path and implement it behind a design note:

- Lua-compatible plugins, either by embedding a Lua-compatible interpreter in
  libtam or by defining a C plugin ABI.
- Embedded terminal pane and VT parser.
- Diff view, linter integration and the bundled plugin set.
- Terminfo reader for unusual terminals and the Linux console.
- Full grapheme-cluster segmentation and emoji sequences.
- Additional platforms; Linux remains the target.

## 7. Testing and parity

Three layers, all run by `make test` where practical:

1. **Unit tests.** C test harness for strings, document operations, config,
   key decoding, regex and syntax. Run under ASan and UBSan.
2. **Ported micro tests.** Extend `tools/testgen.go` to emit C fixtures for the
   VSCode model tests, so editing semantics are checked against the same corpus
   micro uses. Commit generated fixtures; regenerating them needs Go but normal
   builds do not.
3. **Differential and golden-screen tests.** A headless mode accepts a script
   of keys and commands, then dumps the document and cell grid. The same
   scripts run against the Go reference through tcell's simulation screen (or a
   fixed-size pty). Normalized output is compared. Golden files live in
   `tests/golden/`.

Fuzzing covers the key decoder, regex engine, config importer and syntax
converter output. Every fuzz target ships with a seed corpus.

## 8. Performance and quality targets

Measure Go micro first, then hold the C version to these relative goals:

| Measure | Target |
| --- | --- |
| Cold and warm startup | At or below Go micro; aim for under 25 ms to first paint |
| RSS, 10,000-line file | Below Go micro, target under 15 MB |
| Keystroke to paint, 80×24 | 16 ms or less |
| Keystroke to paint, 10,000-line file with syntax | 50 ms or less |
| Binary with embedded runtime data | 5 MB or less |
| Leaks and undefined behavior | None in the main workflows |

Quality rules: no unbounded allocations in rendering or highlighting, no
blocking calls in the event loop, and every external command has a deadline.

## 9. Open decisions and deferred work

- **libtam release ownership.** The four new modules need a version, docs and
  tests in libtam before Phase 3 can finish. This roadmap assumes 0.7.0.
- **Terminal capability approach.** ANSI/xterm only for v1. Revisit terminfo if
  the Linux console or unusual terminals prove inadequate.
- **Regex dialect.** Confirm that no syntax or user search pattern needs
  backreferences or lookaround. Extend `tam_regex` only with evidence.
- **Converter language.** The syntax converter is maintainer tooling. Python 3
  is acceptable because it never runs during a normal build; a C converter can
  replace it later if desired.
- **Plugin path after v1.** Bundled Lua, a new C ABI, or no scripting at all.
  This is deliberately unresolved.
- **Public provenance.** Settled on 2026-11-17: tam-edit carries
  `AI_PROVENANCE.md` and `work/` records like every Tamlinux repository;
  the earlier `docs/ai/` records are in `work/history/`.

## 10. Definition of done

The migration is complete when all of the following hold:

- `make` succeeds with a C11 compiler, `make`, `pkg-config` and libtam only.
- `ldd tam-edit` reports libc alone; `make check-deps` enforces it.
- Core editing works without Lua, ncurses, a VT emulator or any external
  program.
- The ported buffer tests, differential tests and golden screens pass.
- Startup, memory and latency targets are met and recorded.
- License and notices are correct: GPLv3 for the work, MIT notices retained.
- The Go tree remains available on `master` for reference but is not needed to
  build, test or run tam-edit.
