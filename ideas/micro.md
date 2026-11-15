# Ideas from micro

## Overview

- **What it is:** A modern, intuitive, terminal-based text editor aiming to be
  a clean replacement for `nano` with familiar desktop-style keybindings,
  mouse support, splits, and syntax highlighting.
- **Developers:** Created by Zachary Yedidia (`zyedidia`) and contributors.
- **License:** MIT License (see `LICENSE-MIT` in this repository).
- **Language:** Go (using `tcell` for terminal I/O and embedded Lua for plugins).
- **Source Code:** <https://github.com/zyedidia/micro> (also mirrored at
  <https://github.com/micro-editor/micro>; tracked as the `upstream` remote in
  `tam-edit` on the archived `master` branch).

---

## Ideas We Borrow

### 1. Familiar Keybindings & Ergonomics
- Standard desktop shortcuts (`Ctrl+S` save, `Ctrl+C`/`Ctrl+X`/`Ctrl+V`
  copy/cut/paste, `Ctrl+Z`/`Ctrl+Y` undo/redo, `Ctrl+F` find, `Ctrl+Q` quit)
  alongside a `Ctrl+E` command bar.
- Multi-cursor editing, soft-wrapping, auto-indent, and tab/split management.

### 2. Buffer, Cursor & Undo/Redo Model
- Clean separation of `Buffer` (line array, undo/redo event stack, markers,
  dirty state) from `BufView` / `View` (viewport scroll, selection, cursors).
- Comprehensive edge-case behavior verified against `micro`'s 57 buffer/cursor
  corpus tests.

### 3. Syntax Highlighting & Colorschemes
- Declarative syntax rules and colorscheme files shipped in `runtime/` so
  editing C, Shell, INI, Markdown, etc. works out of the box.

### 4. Configuration & Keybinding Overrides
- User-editable `settings.json` / `bindings.json` under XDG config paths,
  allowing users to customize options and remap keys without recompiling.

---

## Ideas We Adapt

### 1. Terminal & Input Layer
- **micro:** Uses Go's `tcell` library.
- **tam-edit:** Uses `libtam`'s `tam_tty`, `tam_key`, and `tam_uni` directly
  for raw mode, SGR mouse, bracketed paste, UTF-8 display width, and Linux VT
  (`TERM=linux`) compatibility without `tcell` or `terminfo`.

### 2. Clipboard & External Integration
- **micro:** Uses Go packages / platform-specific clipboard helpers.
- **tam-edit:** Uses OSC 52 and `tam_exec` to invoke system clipboard tools
  (`wl-copy`/`wl-paste`, `xclip`) cleanly with timeouts.

---

## Ideas We Avoid

- **Go Runtime & Garbage Collection:** Eliminated in the `c11` rewrite so
  `tam-edit` links only `libc` and `libtam.a`, achieving ~1 ms startup and
  minimal RSS.
- **Embedded Lua Plugin Engine & Plugin Manager:** Excluded from `tam-edit` v1
  to keep the binary small, auditable, and free of scripting runtime
  dependencies.
- **Embedded Terminal Emulator (`term`):** Excluded from `tam-edit` v1;
  terminal multiplexing belongs in the user's window manager / multiplexer, not
  inside the text editor.
