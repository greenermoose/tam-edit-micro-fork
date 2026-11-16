#include "cursor.h"
#include "undo.h"

#include <tam.h>
#include <tam_uni.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool is_word_char(uint32_t cp)
{
    if (cp == '_') return true;
    if (cp < 0x80) return isalnum((unsigned char)cp);
    return true; /* non-ASCII code points treated as word/identifier characters */
}

static bool is_space_char(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n';
}

static uint32_t char_at(const char *line, size_t len, size_t rune_idx)
{
    size_t i = 0;
    size_t cur = 0;
    while (i < len) {
        uint32_t cp = 0;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        if (used == 0) break;
        if (cur == rune_idx) return cp;
        cur++;
        i += used;
    }
    return 0;
}

static size_t cur_line_runes(const editor_t *ed)
{
    if (ed->doc.count == 0) return 0;
    if (ed->cx_line >= ed->doc.count) return 0;
    return edit_line_runes(ed->doc.lines[ed->cx_line],
                           ed->doc.lens[ed->cx_line]);
}

static void clamp_one(const editor_t *ed, size_t *line, size_t *col)
{
    if (ed->doc.count == 0) return;
    if (*line >= ed->doc.count) *line = ed->doc.count - 1;
    size_t runes = edit_line_runes(ed->doc.lines[*line], ed->doc.lens[*line]);
    if (*col > runes) *col = runes;
}

void cursor_clamp(editor_t *ed)
{
    if (ed->doc.count == 0) return;
    clamp_one(ed, &ed->cx_line, &ed->cx_col);
    for (size_t i = 0; i < ed->extra_count; i++) {
        clamp_one(ed, &ed->extra_cursors[i].cx_line, &ed->extra_cursors[i].cx_col);
    }
}

static void update_selection(editor_t *ed, bool select)
{
    if (select) {
        if (!ed->sel.active) {
            ed->sel.active = true;
            ed->sel.anchor = (loc_t){ed->cx_line, ed->cx_col};
        }
        ed->sel.cursor = (loc_t){ed->cx_line, ed->cx_col};
    } else {
        ed->sel.active = false;
    }
}

static int softwrap_text_cols(const editor_t *ed)
{
    int view_w = (ed->active_view != NULL && ed->active_view->w > 1)
                 ? ed->active_view->w : 80;
    int gw = 0;
    if (ed->config.ruler) {
        gw = 4;
        if (ed->doc.count >= 1000) gw = 5;
        if (ed->doc.count >= 10000) gw = 6;
        if (gw >= view_w / 2) gw = 0;
    }
    int width = view_w - gw;
    return width < 1 ? 1 : width;
}

static void update_goal_col(editor_t *ed)
{
    if (ed->active_view != NULL && ed->active_view->softwrap &&
        ed->cx_line < ed->doc.count) {
        int width = softwrap_text_cols(ed);
        int tabsize = ed->config.tabsize > 0 ? ed->config.tabsize : 4;
        int vx = 0;
        edit_softwrap_loc_to_vloc(ed->doc.lines[ed->cx_line],
                                  ed->doc.lens[ed->cx_line],
                                  ed->cx_col, width, tabsize,
                                  ed->active_view->wordwrap, NULL, &vx);
        ed->goal_col = (size_t)vx;
    } else {
        ed->goal_col = ed->cx_col;
    }
}

static void cursor_move_single(editor_t *ed, long dx, long dy, bool select)
{
    if (!select && ed->sel.active && dx != 0 && dy == 0) {
        loc_t start, end;
        selection_get_range(ed, &start, &end);
        if (dx < 0) {
            ed->cx_line = start.line;
            ed->cx_col  = start.col;
        } else {
            ed->cx_line = end.line;
            ed->cx_col  = end.col;
        }
        update_goal_col(ed);
        ed->sel.active = false;
        return;
    }

    if (dy == 0 && (dx == -1 || dx == 1) && ed->config.tabmovement &&
        ed->config.tabstospaces && ed->config.tabsize > 1 && ed->cx_line < ed->doc.count) {
        const char *line = ed->doc.lines[ed->cx_line];
        size_t len = ed->doc.lens[ed->cx_line];
        size_t tab = (size_t)ed->config.tabsize;
        bool in_indent = true;
        for (size_t c = 0; c < ed->cx_col && c < len; c++) {
            if (line[c] != ' ') {
                in_indent = false;
                break;
            }
        }
        if (in_indent) {
            if (dx < 0 && ed->cx_col >= tab) {
                size_t step = (ed->cx_col % tab == 0) ? tab : (ed->cx_col % tab);
                dx = -(long)step;
            } else if (dx > 0) {
                size_t step = tab - (ed->cx_col % tab);
                size_t runes = edit_line_runes(line, len);
                bool all_spaces = (ed->cx_col + step <= runes);
                for (size_t k = 0; all_spaces && k < step; k++) {
                    if (line[ed->cx_col + k] != ' ') all_spaces = false;
                }
                if (all_spaces) dx = (long)step;
            }
        }
    }

    if (dy != 0) {
        if (ed->active_view != NULL && ed->active_view->softwrap && ed->doc.count > 0) {
            int width = softwrap_text_cols(ed);
            int tabsize = ed->config.tabsize > 0 ? ed->config.tabsize : 4;
            bool wordwrap = ed->active_view->wordwrap;

            size_t cur_line_idx = ed->cx_line;
            const char *cur_line = ed->doc.lines[cur_line_idx];
            size_t cur_len = ed->doc.lens[cur_line_idx];
            int subline = 0, cur_vx = 0;
            edit_softwrap_loc_to_vloc(cur_line, cur_len, ed->cx_col, width, tabsize, wordwrap, &subline, &cur_vx);
            int goal_vx = (int)ed->goal_col;
            size_t total_sub = edit_softwrap_line_sublines(cur_line, cur_len, width, tabsize, wordwrap);

            int remaining = (dy > 0) ? dy : -dy;
            int dir = (dy > 0) ? 1 : -1;

            while (remaining > 0) {
                if (dir > 0) {
                    subline++;
                    if ((size_t)subline >= total_sub) {
                        if (cur_line_idx + 1 < ed->doc.count) {
                            cur_line_idx++;
                            subline = 0;
                            cur_line = ed->doc.lines[cur_line_idx];
                            cur_len = ed->doc.lens[cur_line_idx];
                            total_sub = edit_softwrap_line_sublines(cur_line, cur_len, width, tabsize, wordwrap);
                        } else {
                            subline = (int)total_sub - 1;
                            break;
                        }
                    }
                } else {
                    subline--;
                    if (subline < 0) {
                        if (cur_line_idx > 0) {
                            cur_line_idx--;
                            cur_line = ed->doc.lines[cur_line_idx];
                            cur_len = ed->doc.lens[cur_line_idx];
                            total_sub = edit_softwrap_line_sublines(cur_line, cur_len, width, tabsize, wordwrap);
                            subline = (total_sub > 0) ? (int)total_sub - 1 : 0;
                        } else {
                            subline = 0;
                            break;
                        }
                    }
                }
                remaining--;
            }

            ed->cx_line = cur_line_idx;
            ed->cx_col = edit_softwrap_vloc_to_rune(cur_line, cur_len, subline, goal_vx, width, tabsize, wordwrap);
            cursor_clamp(ed);
        } else {
            long line = (long)ed->cx_line + dy;
            if (line < 0) line = 0;
            if (line >= (long)ed->doc.count) line = (long)ed->doc.count - 1;
            ed->cx_line = (size_t)line;
            ed->cx_col = ed->goal_col;
            cursor_clamp(ed);
        }
    }

    if (dx != 0) {
        long col = (long)ed->cx_col + dx;
        if (col < 0) {
            if (ed->cx_line > 0) {
                ed->cx_line--;
                ed->cx_col = cur_line_runes(ed);
                update_goal_col(ed);
            } else {
                ed->cx_col = 0;
                ed->goal_col = 0;
            }
        } else {
            size_t runes = cur_line_runes(ed);
            if ((size_t)col > runes) {
                if (ed->cx_line + 1 < ed->doc.count) {
                    ed->cx_line++;
                    ed->cx_col = 0;
                    ed->goal_col = 0;
                } else {
                    ed->cx_col = runes;
                    update_goal_col(ed);
                }
            } else {
                ed->cx_col = (size_t)col;
                update_goal_col(ed);
            }
        }
    }

    update_selection(ed, select);
}

static void swap_primary_with_extra(editor_t *ed, size_t idx)
{
    cursor_t tmp = {ed->cx_line, ed->cx_col, ed->goal_col, ed->sel};
    ed->cx_line  = ed->extra_cursors[idx].cx_line;
    ed->cx_col   = ed->extra_cursors[idx].cx_col;
    ed->goal_col = ed->extra_cursors[idx].goal_col;
    ed->sel      = ed->extra_cursors[idx].sel;
    ed->extra_cursors[idx] = tmp;
}

void cursor_move(editor_t *ed, long dx, long dy, bool select)
{
    cursor_move_single(ed, dx, dy, select);
    if (ed->extra_count > 0) {
        size_t n = ed->extra_count;
        ed->extra_count = 0;
        for (size_t i = 0; i < n; i++) {
            swap_primary_with_extra(ed, i);
            cursor_move_single(ed, dx, dy, select);
            swap_primary_with_extra(ed, i);
        }
        ed->extra_count = n;
        editor_merge_cursors(ed);
    }
    editor_sync_to_active(ed);
}

static void cursor_home_single(editor_t *ed, bool select)
{
    /* Toggle between first non-whitespace and col 0 */
    size_t runes = cur_line_runes(ed);
    const char *line = ed->doc.lines[ed->cx_line];
    size_t len = ed->doc.lens[ed->cx_line];

    size_t first_non_ws = runes;
    for (size_t i = 0; i < runes; i++) {
        uint32_t cp = char_at(line, len, i);
        if (!is_space_char(cp)) {
            first_non_ws = i;
            break;
        }
    }

    if (ed->cx_col == first_non_ws) {
        ed->cx_col = 0;
    } else {
        ed->cx_col = first_non_ws;
    }
    update_goal_col(ed);
    update_selection(ed, select);
}

void cursor_home(editor_t *ed, bool select)
{
    cursor_home_single(ed, select);
    if (ed->extra_count > 0) {
        size_t n = ed->extra_count;
        ed->extra_count = 0;
        for (size_t i = 0; i < n; i++) {
            swap_primary_with_extra(ed, i);
            cursor_home_single(ed, select);
            swap_primary_with_extra(ed, i);
        }
        ed->extra_count = n;
        editor_merge_cursors(ed);
    }
    editor_sync_to_active(ed);
}

static void cursor_end_single(editor_t *ed, bool select)
{
    ed->cx_col = cur_line_runes(ed);
    update_goal_col(ed);
    update_selection(ed, select);
}

void cursor_end(editor_t *ed, bool select)
{
    cursor_end_single(ed, select);
    if (ed->extra_count > 0) {
        size_t n = ed->extra_count;
        ed->extra_count = 0;
        for (size_t i = 0; i < n; i++) {
            swap_primary_with_extra(ed, i);
            cursor_end_single(ed, select);
            swap_primary_with_extra(ed, i);
        }
        ed->extra_count = n;
        editor_merge_cursors(ed);
    }
    editor_sync_to_active(ed);
}

void cursor_page(editor_t *ed, long pages, int text_rows, bool select)
{
    if (text_rows < 1) text_rows = 1;
    cursor_move(ed, 0, pages * text_rows, select);
}

void cursor_start(editor_t *ed, bool select)
{
    ed->extra_count = 0;
    ed->cx_line = 0;
    ed->cx_col = 0;
    ed->goal_col = 0;
    update_selection(ed, select);
    editor_sync_to_active(ed);
}

void cursor_end_doc(editor_t *ed, bool select)
{
    ed->extra_count = 0;
    if (ed->doc.count > 0) {
        ed->cx_line = ed->doc.count - 1;
        ed->cx_col = cur_line_runes(ed);
        ed->goal_col = ed->cx_col;
    }
    update_selection(ed, select);
    editor_sync_to_active(ed);
}

static void cursor_word_left_single(editor_t *ed, bool select)
{
    if (ed->cx_col == 0) {
        if (ed->cx_line > 0) {
            ed->cx_line--;
            ed->cx_col = cur_line_runes(ed);
            update_goal_col(ed);
            update_selection(ed, select);
        }
        return;
    }

    const char *line = ed->doc.lines[ed->cx_line];
    size_t len = ed->doc.lens[ed->cx_line];
    size_t col = ed->cx_col;

    /* 1. Skip spaces to the left */
    while (col > 0 && is_space_char(char_at(line, len, col - 1))) {
        col--;
    }

    /* 2. Skip word or non-word chars */
    if (col > 0) {
        bool in_word = is_word_char(char_at(line, len, col - 1));
        while (col > 0) {
            uint32_t cp = char_at(line, len, col - 1);
            if (is_space_char(cp) || is_word_char(cp) != in_word) break;
            col--;
        }
    }

    ed->cx_col = col;
    update_goal_col(ed);
    update_selection(ed, select);
}

void cursor_word_left(editor_t *ed, bool select)
{
    cursor_word_left_single(ed, select);
    if (ed->extra_count > 0) {
        size_t n = ed->extra_count;
        ed->extra_count = 0;
        for (size_t i = 0; i < n; i++) {
            swap_primary_with_extra(ed, i);
            cursor_word_left_single(ed, select);
            swap_primary_with_extra(ed, i);
        }
        ed->extra_count = n;
        editor_merge_cursors(ed);
    }
    editor_sync_to_active(ed);
}

static void cursor_word_right_single(editor_t *ed, bool select)
{
    size_t runes = cur_line_runes(ed);
    if (ed->cx_col >= runes) {
        if (ed->cx_line + 1 < ed->doc.count) {
            ed->cx_line++;
            ed->cx_col = 0;
            update_goal_col(ed);
            update_selection(ed, select);
        }
        return;
    }

    const char *line = ed->doc.lines[ed->cx_line];
    size_t len = ed->doc.lens[ed->cx_line];
    size_t col = ed->cx_col;

    /* 1. Skip word or non-word chars */
    bool in_word = is_word_char(char_at(line, len, col));
    while (col < runes) {
        uint32_t cp = char_at(line, len, col);
        if (is_space_char(cp) || is_word_char(cp) != in_word) break;
        col++;
    }

    /* 2. Skip trailing spaces */
    while (col < runes && is_space_char(char_at(line, len, col))) {
        col++;
    }

    ed->cx_col = col;
    update_goal_col(ed);
    update_selection(ed, select);
}

void cursor_word_right(editor_t *ed, bool select)
{
    cursor_word_right_single(ed, select);
    if (ed->extra_count > 0) {
        size_t n = ed->extra_count;
        ed->extra_count = 0;
        for (size_t i = 0; i < n; i++) {
            swap_primary_with_extra(ed, i);
            cursor_word_right_single(ed, select);
            swap_primary_with_extra(ed, i);
        }
        ed->extra_count = n;
        editor_merge_cursors(ed);
    }
    editor_sync_to_active(ed);
}

void cursor_select_all(editor_t *ed)
{
    if (ed->doc.count == 0) return;
    ed->extra_count = 0;
    ed->sel.active = true;
    ed->sel.anchor = (loc_t){0, 0};
    ed->cx_line    = ed->doc.count - 1;
    ed->cx_col     = cur_line_runes(ed);
    ed->goal_col   = ed->cx_col;
    ed->sel.cursor = (loc_t){ed->cx_line, ed->cx_col};
    editor_sync_to_active(ed);
}

void cursor_deselect(editor_t *ed)
{
    ed->sel.active = false;
    for (size_t i = 0; i < ed->extra_count; i++) {
        ed->extra_cursors[i].sel.active = false;
    }
    editor_sync_to_active(ed);
}

bool selection_get_range(const editor_t *ed, loc_t *start, loc_t *end)
{
    if (!ed->sel.active) return false;

    loc_t a = ed->sel.anchor;
    loc_t b = ed->sel.cursor;

    if (a.line < b.line || (a.line == b.line && a.col <= b.col)) {
        if (start != NULL) *start = a;
        if (end   != NULL) *end   = b;
    } else {
        if (start != NULL) *start = b;
        if (end   != NULL) *end   = a;
    }
    return true;
}

char *selection_get_text(const editor_t *ed, size_t *out_len)
{
    loc_t start, end;
    if (!selection_get_range(ed, &start, &end)) {
        if (out_len != NULL) *out_len = 0;
        return NULL;
    }

    tam_str_t s = tam_str_new("");
    if (s == NULL) return NULL;

    const char *sep = ed->crlf ? "\r\n" : "\n";
    size_t sep_len = strlen(sep);

    for (size_t line = start.line; line <= end.line; line++) {
        const char *l_str = ed->doc.lines[line];
        size_t l_len = ed->doc.lens[line];

        size_t col_start = (line == start.line) ? start.col : 0;
        size_t col_end   = (line == end.line)   ? end.col   : edit_line_runes(l_str, l_len);

        size_t byte_start = edit_rune_to_byte(l_str, l_len, col_start);
        size_t byte_end   = edit_rune_to_byte(l_str, l_len, col_end);

        if (line > start.line) {
            s = tam_str_cat_len(s, sep, sep_len);
        }
        if (byte_end > byte_start) {
            s = tam_str_cat_len(s, l_str + byte_start, byte_end - byte_start);
        }
    }

    size_t len = tam_str_len(s);
    char *ret = malloc(len + 1);
    if (ret != NULL) {
        memcpy(ret, s, len);
        ret[len] = '\0';
    }
    tam_str_free(s);

    if (out_len != NULL) *out_len = len;
    return ret;
}

bool selection_delete(editor_t *ed)
{
    loc_t start, end;
    if (!selection_get_range(ed, &start, &end)) return false;

    if (start.line == end.line && start.col == end.col) {
        ed->sel.active = false;
        return false;
    }

    size_t len = 0;
    char *deleted = selection_get_text(ed, &len);
    if (deleted == NULL) {
        ed->sel.active = false;
        return false;
    }

    undo_record_remove(ed, start.line, start.col, deleted, len);
    undo_clear_redo(ed);

    editor_raw_remove_str(ed, start.line, start.col, deleted, len);
    free(deleted);

    ed->cx_line = start.line;
    ed->cx_col  = start.col;
    ed->goal_col = ed->cx_col;
    cursor_clamp(ed);
    ed->sel.active = false;
    ed->dirty = true;
    editor_sync_to_active(ed);
    return true;
}
