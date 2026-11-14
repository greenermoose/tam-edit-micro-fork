# tam-edit v1.0.0 Parity Audit

Audit of the C11 editor (`src/`) against the Go `micro` reference (`internal/`, `cmd/tam-edit/`) prior to pruning the Go tree on `c11`.

## 1. Commands (`internal/action/command.go` vs `src/prompt.c`)

| Command | Go Reference | C11 Status | Notes / Intentional v1 Difference |
| --- | --- | --- | --- |
| `set`, `setlocal` | Yes | Implemented | `src/prompt.c`, persisted via `tam_ini` (`settings.ini`) |
| `toggle`, `togglelocal` | Yes | Implemented | `src/prompt.c` |
| `reset` | Yes | Implemented | `src/prompt.c` |
| `show` | Yes | Implemented | `src/prompt.c` |
| `showkey` | Yes | Implemented | `src/prompt.c` |
| `bind`, `unbind` | Yes | Implemented | `src/prompt.c`, `src/config.c` |
| `quit` (`q`, `q!`) | Yes | Implemented | `src/prompt.c` |
| `goto`, `jump` | Yes | Implemented | `src/prompt.c` |
| `save` (`w`, `write`), `save-as` | Yes | Implemented | `src/prompt.c` (`save-as` added for convenience) |
| `open` (`e`), `reopen` | Yes | Implemented | `src/prompt.c` |
| `cd`, `pwd`, `ls` | Yes | Implemented | `src/prompt.c` (`ls` added for directory listing) |
| `rename`, `delete` | — | Implemented | `src/prompt.c` (Phase 6 file operations) |
| `replace`, `replaceall` | Yes | Implemented | `src/prompt.c`, `src/search.c` (`-l`, `-s`, `-a` flags) |
| `vsplit`, `hsplit`, `unsplit` | Yes | Implemented | `src/prompt.c`, `src/pane.c` |
| `tab`, `tabnext`, `tabprev`, `tabclose`, `tabmove`, `tabswitch` | Yes | Implemented | `src/prompt.c`, `src/pane.c` |
| `colorscheme` | — | Implemented | `src/prompt.c` (shortcut for `set colorscheme`) |
| `help` | Yes | Implemented | `src/prompt.c`, embedded `data/help_data.c` |
| `run` (`run -i`), `textfilter` | Yes | Implemented | `src/prompt.c`, `src/shell.c` |
| `retab` | Yes | Implemented | `src/prompt.c`, `src/editor.c` |
| `reload` | Yes | Omitted (v1) | Use `reopen` for current buffer; `reload` setting (`prompt`/`auto`/`disabled`) is implemented |
| `raw` | Yes | Omitted (v1) | Replaced by `showkey` and `--dump-syntax` |
| `memusage` | Yes | Omitted (v1) | Go runtime memory stats; not applicable to C11 |
| `eval`, `log` | Yes | Omitted (v1) | Deferred with Lua plugin runtime (Phase 8) |
| `plugin` | Yes | Omitted (v1) | Non-goal for v1 (Phase 8) |
| `term` | Yes | Omitted (v1) | Non-goal for v1 (Phase 8) |

## 2. Settings & Options (`internal/config/settings.go` vs `src/config.c`)

### Implemented in C11
`autoindent`, `clipboard` (`internal`/`external`/`terminal`), `colorscheme`, `cursorline`, `detectlimit`, `encoding`, `eofnewline`, `fileformat` (`unix`/`dos` via `setlocal`/`togglelocal`/`show`), `filetype` (via `set`/`setlocal`/`togglelocal`/`show`), `hlsearch`, `ignorecase`, `incsearch`, `mkparents`, `overwrite` (via `Insert` / `ToggleOverwriteMode`), `readonly` (via `setlocal`/`togglelocal`/`show`), `reload` (`prompt`/`auto`/`disabled`), `rmtrailingws`, `ruler`, `scrollmargin`, `scrollsync` (C11 addition for split sync), `smartpaste`, `softwrap`, `splitbottom`, `splitright`, `statusline`, `syntax`, `tabbar` (`auto`/`on`/`off`), `tabmovement`, `tabsize`, `tabstospaces`, `truecolor` (`auto`/`on`/`off`), `wordwrap`, `helpsplit` (`hsplit`/`vsplit`).

### Intentional v1 Omissions (documented)
- `autosave`, `backup`, `backupdir`, `permbackup`, `savecursor`, `saveundo`: background timers / persistent state files deferred past v1 (atomic writes via `tam_fs_write_atomic` are always used).
- `autosu`: privilege escalation on save omitted for v1 security simplicity.
- `basename`, `statusformatl`, `statusformatr`, `divchars`, `divreverse`, `scrollbar`: fixed clean statusline/divider layout in `src/render.c`.
- `colorcolumn`, `diffgutter`, `hltaberrors`, `hltrailingws`, `matchbrace`, `matchbraceleft`, `matchbracestyle`, `relativeruler`, `showchars`: visual decorations deferred past v1.
- `fastdirty`, `indentchar` (deprecated in micro), `keepautoindent`, `multiopen`, `pageoverlap`, `scrollspeed`, `useprimary`: simplified or fixed defaults in C11.
- `sucmd`, `pluginchannels`, `pluginrepos`, `infobar`, `keymenu`: plugin/sudo/keymenu UI deferred.

## 3. Default Keybindings (`internal/action/defaults_other.go` vs `src/actions.c`)

- **Navigation & Selection:** `Up`, `Down`, `Left`, `Right`, `Shift+Arrows`, `Ctrl+Left/Right`, `Ctrl+Shift+Left/Right`, `Home`/`End`, `Shift+Home/End`, `Ctrl+Home/End`, `Ctrl+Shift+Home/End`, `PageUp`/`PageDown`, `Shift+PageUp/PageDown`, `Ctrl+PageUp/PageDown` — **Parity**.
- **Editing:** `Enter`, `Tab`, `Backspace`, `Delete`, `Insert` (`ToggleOverwriteMode`), `Ctrl+Backspace`/`Alt+Backspace` (`DeleteWordLeft`), `Ctrl+Z`, `Ctrl+Y`, `Ctrl+C`, `Ctrl+X`, `Ctrl+K`, `Ctrl+V`, `Ctrl+D`, `Ctrl+A`, `Alt+Up`/`Alt+Down` (`MoveLinesUp/Down`) — **Parity**.
- **Files, Search, Views, Help:** `Ctrl+S`/`F2`, `Ctrl+O`, `Ctrl+Q`/`F4`/`F10`, `Ctrl+F`/`F7`, `Alt+F` (`FindLiteral`), `Ctrl+N`, `Ctrl+P`, `Ctrl+E`, `Ctrl+L`, `Ctrl+B`, `Ctrl+W`, `Ctrl+T`, `Ctrl+R`, `Alt+W`, `Alt+.`/`Alt+,`, `F1`/`Ctrl+H` (`ToggleHelp`, `Esc` closes help) — **Parity** (`Ctrl+H` replaces micro's `Ctrl+G`).
- **Multi-Cursor & Esc:** `Alt+N`, `Alt+M`, `Alt+X`, `Alt+P`, `Alt+C`, `Alt+Shift+Up/Down`, `Ctrl+LeftClick`, `Esc` (`RemoveAllMultiCursors|Deselect`) — **Parity**.
- **Intentional differences:**
  - `F3` is bound to `FindNext` (micro binds `F3` to `Find`).
  - `Shift+Tab` (`Backtab`) outdent, `Alt+G` (`ToggleKeyMenu`), `Ctrl+U`/`Ctrl+J` (macros), `Alt+Left/Right/A/E` emacs aliases: omitted or user-bindable via `settings.ini` `[bindings]`.

## 4. CLI Flags (`cmd/tam-edit/main.go` vs `src/main.c`)

| Flag | Go Reference | C11 Status | Notes |
| --- | --- | --- | --- |
| `-v`, `--version` | `-version` | Implemented | Prints `tam-edit <ver> (libtam <ver>)` |
| `-h`, `--help` | `-help` | Implemented | Prints CLI usage |
| `+LINE[:COL]` | `+LINE:COL` | Implemented (v1.0.0) | Jumps to 1-based line (and optional column) on startup |
| `--check-config [PATH]` | — | Implemented | Validates `settings.ini` |
| `--import-micro-config [DIR]` | — | Implemented | Migrates micro JSON5 config to INI |
| `--dump-syntax FILE` | — | Implemented | Prints per-line syntax highlight spans |
| `-config-dir`, `-options`, `-clean`, `-exec`, `-debug`, `-profile`, `-plugin` | Yes | Omitted (v1) | Use `TAM_EDIT_CONFIG_HOME` for config dir; plugin/Go profiling flags N/A |
