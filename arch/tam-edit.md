# `tam-edit` Architecture: Data Structures & Algorithms

This document describes the data structures, ownership model, and core algorithms implemented in our C11 version of `tam-edit` (`src/`).

---

## 1. Document, Buffer, Split Tree & Undo Model

### 1.1 Dynamic Line Array (`src/editor.h`, `src/editor.c`)
- **`edit_doc_t`**: `{ char **lines; size_t *lens; size_t count; size_t cap; }`.
  - Represents text as a **dynamic array of heap-allocated, NUL-terminated UTF-8 line strings** (`lines[i]`) with byte lengths cached in `lens[i]` (rather than a gap buffer, rope, or piece table).
  - Line operations (`doc_reserve`, `doc_insert_line`, `doc_remove_line`, `line_replace`) map directly to O(1) line lookup for rendering, syntax line-state caching, and per-line regex matching, with O(N_lines) array shifts only on line insertion/deletion.

### 1.2 Reference-Counted Buffers & Binary Split Tree (`src/buffer.h`, `src/pane.h`)
- **`buffer_t`**: Reference-counted (`refcount` via `buffer_ref`/`buffer_unref`) document container owning `doc`, `path`, `undo_stack`, `redo_stack`, `line_states`, `syntax_cache`, encoding metadata (`encoding`, `has_bom`), and external `mtime`. Multiple views can share one `buffer_t` with optional `scrollsync`.
- **`view_t`**: Per-pane viewport state (`buf`, `cx_line`, `cx_col`, `goal_col`, `top_line`, `left_col`, `top_subline`, `sel`, `extra_cursors`, `x`/`y`/`w`/`h`, `softwrap`, `wordwrap`).
- **`pane_node_t`**: Binary split tree (`PANE_LEAF`, `PANE_SPLIT_V`, `PANE_SPLIT_H`, `child1`, `child2`, `parent`, `ratio`). Leaf nodes own a `view_t *`; internal nodes recursively partition screen rectangles in `pane_layout()`.
- **`tab_list_t` / `tab_t`**: Dynamic array of tabs, each owning a `pane_node_t *root` and tracking `active_view`.

### 1.3 Time-Coalesced & Grouped Undo/Redo (`src/editor.h`, `src/undo.c`)
- **`undo_stack_t`**: Singly-linked stack (`head`) of **`undo_group_t`** nodes. Each group owns a dynamic array of **`delta_t`** records (`DELTA_INSERT = 1` / `DELTA_REMOVE = -1`, `line`, `col`, `text`, `len`), plus `cursor_before`, `cursor_after`, and `time_ms`.
- **Coalescing & Explicit Grouping**: `undo_record_insert()` / `undo_record_remove()` merge adjacent same-type edits within a 1000 ms `CLOCK_MONOTONIC` window unless `explicit_group` is active (`undo_begin_group()` / `undo_end_group()` with `explicit_depth` for multi-cursor and replace-all operations).
- **`undo_perform()` / `redo_perform()`**: Apply inverse deltas in reverse order (undo) or forward order (redo), restoring `cursor_before` / `cursor_after`.

---

## 2. Cursors, Selections, Softwrap & Multi-Cursor

### 2.1 Cursor & Selection (`src/cursor.c`)
- **`loc_t`**: `{ size_t line; size_t col; }` (0-based line and 0-based rune column).
- **`cursor_t`**: `{ cx_line; cx_col; goal_col; selection_t sel; }` where `selection_t` holds `{ anchor; cursor; active; }`.
- `cursor_move_single()` preserves `goal_col` across vertical moves, supports `tabmovement`, and handles `softwrap` visual subline navigation via `edit_softwrap_line_sublines()`, `edit_softwrap_loc_to_vloc()`, and `edit_softwrap_vloc_to_rune()` (`src/editor.c`).

### 2.2 Multi-Cursor Editing (`src/editor.c`)
- **`multi_edit_apply()`** collects the primary cursor and `extra_cursors` into an `mc_item_t[]` array, sorts them by document position (`mc_item_cmp`), and executes edits (`MC_OP_CODEPOINT`, `MC_OP_TEXT`, `MC_OP_NEWLINE`, `MC_OP_BACKSPACE`, `MC_OP_DELETE`) **bottom-to-top in reverse document order**.
- Each edit shifts downstream cursors (`d_lines`, `d_cols`), wraps the entire batch in `undo_begin_group()` / `undo_end_group()`, and deduplicates overlapping cursors via `editor_merge_cursors()`.

---

## 3. In-Tree Thompson-NFA / Pike-VM Regex Engine (`src/regex.h`, `src/regex.c`)

`tam-edit` embeds a self-contained UTF-8 regex engine with no external regex library dependency:
- **Compiler**: Recursive-descent parser (`parse_alt` -> `parse_concat` -> `parse_quant` -> `parse_atom`, max depth 128) builds an `ast_node_t` array (`AST_CHAR`, `AST_ANY`, `AST_CLASS`, `AST_BOL`/`EOL`/`WB`/`NWB`, `AST_CONCAT`, `AST_ALT`, `AST_REPEAT`, `AST_CAPTURE`). `compile_node()` lowers the AST into `insn_t` bytecode (`OP_CHAR`, `OP_ANY`, `OP_CLASS`, `OP_SPLIT`, `OP_JMP`, `OP_BOL`, `OP_EOL`, `OP_WB`, `OP_NWB`, `OP_SAVE`, `OP_MATCH`) and `char_class_t` codepoint range tables (`cp_range_t`).
- **Pike-VM Execution**:
  - `vm_find_fast()` (no captures, `vm_fast_thread_t`) and `tam_regex_find_captures()` (up to `TAM_REGEX_MAX_CAPTURES = 16` groups / `NUM_SAVE_SLOTS = 30`, `vm_cap_thread_t`).
  - Steps active thread lists using generation-stamped `gen[]` arrays (`add_thread_fast` / `add_thread_cap`) to guarantee linear-time, leftmost-first matching without catastrophic backtracking.

---

## 4. Syntax Highlighting, Search & Completion

### 4.1 Incremental Line-State Syntax Engine (`src/syntax.h`, `src/syntax.c`)
- **Lazy Compilation & Detection**: `syntax_registry_t` holds user overrides and embedded YAML-parsed definitions (`data/syntax_data.c`). `syntax_detect()` matches `filename_re`, `header_re`, and `signature_re` up to `detectlimit` lines; `compile_entry()` lazily compiles regexes and resolves `include` directives with cycle detection.
- **Line-State Machine & Convergence**: `buffer_t` stores `line_states[i]` (`const syntax_region_t *` active at the end of line `i`). On edit (`buffer_syntax_on_edit()`), states are recomputed forward until `line_states[i]` converges with the previous state.
- **Skip Masking & Caching**: `find_index_skip()` masks `skip` regex matches with `'\x01'` before scanning for region `end` patterns. Rendered highlight group IDs per line are cached in a **128-slot direct-mapped cache** (`syntax_line_cache`, `SYNTAX_CACHE_SLOTS = 128`).

### 4.2 Search, Replace & Completion (`src/search.c`, `src/complete.c`)
- **`search_state_t`** compiles queries with `tam_regex_compile()` (using `escape_regex_literal()` in literal mode). Incremental search (`search_inc_begin/update/cancel`) snapshots cursor, scroll, and selection to restore on cancel.
- **`search_replace_all()`** applies replacements right-to-left per line within a single undo group, expanding `$0`, `$1`, `${name}`, and `$$` templates (`search_expand_template()`).
- **`src/complete.c`** builds Tab/Shift-Tab completion lists over `k_commands[]`, `k_encodings[]`, option values, and filesystem paths.

---

## 5. Double-Buffered Renderer, Colorschemes, Shell & Encoding

- **Double-Buffered Cell Diffing (`src/render.h`, `src/render.c`)**:
  - `renderer_t` maintains `front` and `back` `grid_t` arrays of `cell_t` (`codepoint`, `fg`, `bg`, `attr`, `width` for wide/combining characters).
  - `render_draw()` paints tabbar, split panes (`render_paint_view` with syntax groups, `hlsearch`, selection, multi-cursors, and ruler gutter), prompts, and keybinding footers into `back`, then diffs `back` vs `front` to emit minimal cursor moves and SGR codes inside `tam_tty_frame_begin()` / `tam_tty_frame_end()`.
- **Hierarchical Colorscheme Cache (`src/colorscheme.h`, `src/colorscheme.c`)**:
  - `colorscheme_get()` resolves dot-separated highlight groups (e.g., `constant.string` -> `constant`) and caches results in `group_cache[512]`.
- **Async Shell & Encoding (`src/shell.c`, `src/encoding.c`)**:
  - `shell_bg_start()` spawns background commands in their own process group (`setpgid`) with non-blocking pipes polled by `main.c` (`poll()`), enforcing deadlines via `kill(-pid, SIGKILL)`.
  - `encoding_decode_buffer()` tries BOM -> UTF-8 -> configured encoding -> `windows-1252` -> `iso-8859-1` via `iconv`.
