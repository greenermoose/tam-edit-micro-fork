# tam-edit cursor movement review (001)

## Scope / where this lives

Core cursor/selection/movement code is concentrated in:
- `tam-edit/src/cursor.c` / `tam-edit/src/cursor.h` — cursor motion, word motion, selection range/text/delete, goal-column updates
- `tam-edit/src/editor.c` / `tam-edit/src/editor.h` — low-level doc ops, insert/delete/edit dispatch, multi-cursor application/merge/spawn, undo/redo integration, view sync
- `tam-edit/src/actions.c` — key/action dispatch wiring (moves, selects, delete-word-left, multi-cursor commands)
- `tam-edit/src/render.c` — scroll clamping and hardware-cursor placement (reads/mutates view scroll state during draw)
- `tam-edit/arch/tam-edit.md` — stated design: dynamic line array, ref-counted buffers, binary split tree, rune-column cursor model, softwrap helpers, bottom-to-top multi-cursor edits

Architecture summary relevant to cursor movement:
- **Document**: `edit_doc_t { char **lines; size_t *lens; count; cap; }` (dynamic array of heap NUL-terminated UTF-8 lines). O(1) line lookup, O(N) line insert/delete shifts (`src/editor.c:23-98`, `arch/tam-edit.md:9-13`).
- **Cursor model**: `loc_t { line, col }` with `col` as 0-based rune index; `cursor_t` carries `goal_col` and `selection_t { anchor, cursor, active }` (`editor.h:12-29`).
- **Views/buffers**: `view_t` holds viewport/cursor/selection/extra cursors; `buffer_t` is ref-counted and shared; editor mirrors active view state via `editor_sync_to/from_active` (`editor.c:547-621`, `arch/tam-edit.md:14-19`).
- **Rendering/scrolling**: double-buffered cell grid diff; `render_paint_view` clamps `top_line/left_col` to keep cursor visible and computes hardware cursor position from view+softwrap (`render.c:224-282`, `820-850`).
- **Multi-cursor**: edits are collected, sorted, applied bottom-to-top in one undo group with downstream position adjustment; extras stored as `cursor_t[]` (`editor.c:1176-1498`, `arch/tam-edit.md:34-37`).

## Bugs / likely correctness issues

### 1. Softwrap "desired X"/goal column is corrupted by home/end/word motions (high)
- `cursor_move_single` stores `goal_col` differently depending on mode: in softwrap vertical motion it treats `goal_col` as **visual X (cells/subline x)** and restores with `edit_softwrap_vloc_to_rune(..., goal_vx, ...)` (`cursor.c:154-209`); `update_goal_col()` computes visual X in softwrap (`cursor.c:92-106`).
- But `cursor_home_single` sets `ed->goal_col = ed->cx_col` (rune column) (`cursor.c:296`), `cursor_end_single` does the same (`cursor.c:319-321`), and `cursor_word_left/right_single` set `ed->goal_col = col` (rune column) (`cursor.c:401-402`, `453-454`).
- **Effect:** after Home/End/word jump on a soft-wrapped line, the next Up/Down uses a rune index as if it were a pixel/cell X, so the cursor lands at the wrong column/subline (especially with tabs, wide chars, or wrapping). Horizontal arrow moves are fine because they call `update_goal_col(ed)` (`cursor.c:222,236,240`).
- **Fix:** have home/end/word motions (and any move that changes column) update goal via `update_goal_col(ed)` in softwrap-aware form, or store both rune-goal and visual-goal consistently.

### 2. `edit_visual_col` / `edit_visual_to_rune` ignore configured tab width (medium, correctness drift)
- `edit_visual_col` advances tabs as `EDIT_TABSTOP - (cells % EDIT_TABSTOP)` with `EDIT_TABSTOP` fixed to 8 (`editor.h:8`, `editor.c:295-314`); `edit_visual_to_rune` same (`editor.c:316-336`).
- Rendering and softwrap math use `config.tabsize`/`tabsize` (`render.c:211,331,409`; `cursor.c:96`, `editor.c:343+` softwrap helpers take `tabsize`).
- **Effect:** any feature relying on these helpers (e.g. non-wrapped horizontal scrolling/visual positioning paths that use `edit_visual_col`, prompt/cursor visuals where used, or future code) disagrees with displayed layout when `tabsize != 8`, causing cursor/scroll misalignment.
- **Fix:** make visual-column helpers accept `tabsize` (or read config/context) and use it consistently; avoid a hardcoded global tabstop for display math.

### 3. `act_delete_word_left` is not multi-cursor-safe and bypasses the edit pipeline (medium-high)
- `act_delete_word_left` computes a word boundary by calling `cursor_word_left(ed, false)` (which moves **all** cursors and merges, `actions.c:72`, `cursor.c:405-420`), then deletes only on the primary cursor/range if same line (`actions.c:73-84`) using raw record/remove/touch-like state updates.
- **Effect:** with extra cursors it relocates every cursor but deletes only once at the primary's old position; with selections spanning lines it does nothing useful; it also doesn't go through `check_writable`/multi-edit grouping semantics the way normal edits do. Behavior is inconsistent with `Backspace`/`Delete` multi-cursor handling (`editor.c:1548-1567`).
- **Fix:** implement as an edit operation (select-word-left range per cursor + delete) dispatched through the same single/multi-cursor path as other destructive edits, or at least apply per-cursor and preserve invariants.

### 4. Tab-indentation horizontal stepping mixes rune counts with byte indexing (medium, latent UTF-8 bug)
- In `cursor_move_single`, when `tabmovement`+`tabstospaces` applies for right-move it computes: `bool all_spaces = (ed->cx_col + step <= runes); for (...) if (line[ed->cx_col + k] != ' ') ...` (`cursor.c:142-149`).
- `ed->cx_col`/`runes` are rune columns but `line[...]` is indexed by that value as a **byte** offset. The function earlier checks "in indent" by scanning bytes up to `cx_col` (`cursor.c:131-136`).
- **Effect:** correct for ASCII-space indentation, but logically wrong for non-ASCII text (and fragile if assumptions change). A non-ASCII char in the prefix would desynchronize rune/byte positions; comparing rune count to a byte-based bound is inconsistent.
- **Fix:** operate in one coordinate space: check indent/padding by iterating UTF-8 runes or by using byte offsets consistently with `edit_rune_to_byte`.

### 5. `cursor_clamp` ignores the empty-document case (low-medium edge case)
- `cursor_clamp` returns immediately if `ed->doc.count == 0` (`cursor.c:54-61`), leaving `cx_line/cx_col` (and extras/selection anchors elsewhere) potentially out of range.
- Depending on invariants (`buffer_new`/load paths), the doc may normally have >=1 line, but if a zero-line state occurs, clamping won't normalize cursors.
- **Fix:** clamp to `(0,0)` when `count == 0` (and/or enforce the single-empty-line invariant centrally).

### 6. Scroll/cursor-visibility is computed as a render side effect (medium, factoring/correctness risk)
- `render_paint_view` mutates `v->top_line`/`v->left_col` to satisfy scrollmargin and keep the cursor visible (`render.c:224-282`), then `render_draw` copies active view scroll back to `ed->top_line/left_col` (`render.c:690-694`).
- **Effect:** scrolling correctness depends on draw order/frequency; logic is split between movement and rendering; off-screen operations between frames or alternate code paths can observe stale scroll. Softwrap scroll adjustment is iterative and coupled to render-time layout (`text_cols`, `tabsize`).
- **Fix:** extract `ensure_cursor_visible(view, config, text_area)` and call it after cursor/viewport-affecting changes (and still allow render to be pure w.r.t. logical state except cell output).

### 7. Multi-cursor downstream adjustment is complex and can leave secondary cursor metadata stale (medium)
- `multi_edit_apply` shifts `cx_line/cx_col` (and sometimes `goal_col`) for cursors after each bottom-to-top edit, with special cases for line insert/delete and same-line column delta (`editor.c:1449-1477`).
- It does **not** generally adjust `sel.anchor/cursor` inside `items[j].c.sel` for shifted extras, nor normalize `goal_col` in all branches (some paths set `goal_col = cx_col` even when softwrap visual-goal semantics matter).
- **Effect:** after grouped multi-edits, extra-cursor selections (if active) can point at wrong offsets; vertical movement after such edits may use a degraded/stale goal column for affected extras.
- **Fix:** define a single "translate location/selection by edit delta" helper applied uniformly to cursor and selection endpoints (and recompute/mark goal appropriately), covered by tests for multi-cursor + selection + newline/backspace-join cases.

### 8. Word classification is ASCII-centric and not Unicode-aware (behavioral/quality)
- `is_word_char` returns true for all non-ASCII (`cursor.c:11-16`), and multi-cursor "word at cursor" uses its own `isalnum/_/>=0x80` test (`editor.c:1822-1860`). `is_space_char` only recognizes ASCII space/tab/CR/LF (`cursor.c:18-21`).
- **Effect:** Unicode punctuation, NBSP, and other separators are treated as "word" characters; word motions/selections differ from typical Unicode word boundaries. This is consistent-ish with the code's simplicity goal but is a notable movement behavior limitation.

## Data structures & algorithm considerations

- **Text storage (good fit for current goals):** line array gives simple indexing and integrates well with per-line syntax state/cache and rendering; line insert/delete are O(L) shifts (`doc_insert_line/remove_line`), edits within a line allocate/copy a new line buffer (`line_replace`) — fine for typical editing, weaker for huge files or massive line churn.
- **Cursor addressing:** rune-column indexing is reasonable for editing semantics, but many operations pay repeated UTF-8 decode-from-start costs:
  - `char_at` decodes from byte 0 each call (`cursor.c:23-36`) and is used in loops in `cursor_home_single` and `cursor_word_left/right_single` (`cursor.c:283-289`, `386-398`, `440-453`) => worst-case O(n^2) per motion on long lines.
  - `edit_rune_to_byte` / `edit_line_runes` are linear scans; frequently recomputed.
  - **Improvement:** iterate with a moving byte/rune cursor (or cache byte offsets / maintain a small scan state) for word/home scans; consider helper APIs that advance left/right by rune without rescanning from 0.
- **Multi-cursor structure:** `cursor_t[]` with `qsort` + reverse application is the right approach for correctness; merge is O(m^2) duplicate check (`editor.c:1775-1793`) which is fine for small m. Spawning-by-search scans the whole document linearly from the search origin each time (`editor.c:1919-1936`) — acceptable for interactive counts, but can degrade on very large files with many matches.
- **Softwrap layout:** wrap calculation is recomputed from line content when needed (`edit_softwrap_*`), and render rescans lines to place cursor/scroll in softwrap mode (`render.c:256-279`, `840-847`). Correct but potentially expensive on long wrapped lines/large viewports; a cached visual-line map per visible/affected line would reduce repeated work.
- **Goal column semantics:** one `goal_col` field is overloaded (rune column in non-wrap paths vs visual X in wrap vertical restore). This is a root cause of confusion/bugs; separating logical and visual desired position would simplify invariants.

## Factoring / maintainability observations

- **Mixed responsibilities:** cursor module contains movement + selection text/delete + view sync calls; editor contains low-level mutation, high-level edit ops, multi-cursor engine, and UI-ish messages. `actions.c` also contains edit logic (`act_delete_word_left`) duplicating concepts.
- **Coordinate systems are implicit:** (line,rune), byte offset, visual cell column, and softwrap (subline,vx) are all used; helpers exist but boundaries are blurry (e.g. `edit_visual_col` fixed tabstop vs config tabsize; `goal_col` meaning varies by mode).
- **Scroll logic lives in renderer:** view clamping inside paint (`render.c`) couples correctness to drawing; better as explicit layout/viewport logic.
- **Primary/extra cursor swapping pattern:** `cursor_move/home/end/word` temporarily swap extras into the primary to reuse single-cursor routines (`cursor.c:248-272`, etc.). It works but is non-obvious and easy to get wrong if single routines gain side effects; an explicit "apply motion to cursor_t within document/view" API would be clearer.
- **State duplication:** editor flat fields mirror active view/buffer (`editor_sync_*`), increasing chances of drift when new cursor/scroll fields are added.

## Suggested improvements (prioritized)

1. **Fix goal-column correctness (highest value):** unify goal handling so Home/End/word and horizontal moves update a mode-appropriate goal; in softwrap store/restore visual X, in non-wrap store/restore the intended column (and consider preserving visual intent across wrapped/unwrapped transitions). Add tests for Up/Down after Home/End/word on wrapped lines with tabs/wide chars.
2. **Make `DeleteWordLeft` (and similar actions) go through the standard edit/multi-cursor path:** per-cursor word deletion with proper undo grouping, readonly/selection handling, and no accidental cursor-list mutation as a side effect.
3. **Normalize coordinate helpers:** parameterize visual-column/tab math by `tabsize`; provide UTF-8/rune iteration utilities that don't rescan from line start; remove byte-vs-rune mixing in tabmovement indent checks.
4. **Extract viewport/scroll assurance from rendering:** `ensure_cursor_visible` (and horizontal/softwrap equivalents) invoked on cursor/size changes; keep render focused on painting/diffing.
5. **Harden multi-cursor transforms:** centralize "apply text delta to (line,col,selection,goal)" for extras, including selection endpoints and softwrap goal recalculation where needed; add regression coverage (multi-cursor backspace join, newline insert, paste, selection edits).
6. **Performance tidy-ups (as needed):** avoid O(n^2) scans in word/home detection by walking byte offsets with remembered rune positions; cache line rune lengths/byte maps only if profiling shows it matters given current targets.
7. **Clarify/document invariants:** non-empty document invariant, meaning of `goal_col`, and when `extra_cursors` are cleared (e.g. start/end-of-doc, select-all, undo/redo currently reset extras — confirm intended UX and document/test it).

**Net assessment:** the overall design (line-array buffer, rune-based cursor, sorted bottom-to-top multi-cursor edits, separate softwrap math) is sound and simple, but there are a few concrete movement bugs around softwrap goal preservation and a couple of edge/consistency issues (delete-word-left with multicursors, tab/byte-rune mixing, scroll handled in render) worth addressing first.
