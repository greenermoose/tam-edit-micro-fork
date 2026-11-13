#include "editor.h"
#include "buffer.h"
#include "cursor.h"
#include "syntax.h"
#include "undo.h"

#include <tam.h>
#include <tam_uni.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* -------------------------------------------------------------------------- */
/* Document storage                                                           */
/* -------------------------------------------------------------------------- */

static bool doc_reserve(edit_doc_t *d, size_t need)
{
    if (need <= d->cap) return true;

    size_t cap = d->cap ? d->cap : 32;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) return false;
        cap *= 2;
    }

    char **lines = realloc(d->lines, cap * sizeof(*lines));
    if (lines == NULL) return false;
    d->lines = lines;

    size_t *lens = realloc(d->lens, cap * sizeof(*lens));
    if (lens == NULL) return false;
    d->lens = lens;

    d->cap = cap;
    return true;
}

static bool doc_insert_line(edit_doc_t *d, size_t at,
                            const char *data, size_t len)
{
    if (at > d->count) return false;
    if (!doc_reserve(d, d->count + 1)) return false;

    char *copy = malloc(len + 1);
    if (copy == NULL) return false;
    if (len > 0) memcpy(copy, data, len);
    copy[len] = '\0';

    memmove(d->lines + at + 1, d->lines + at,
            (d->count - at) * sizeof(*d->lines));
    memmove(d->lens + at + 1, d->lens + at,
            (d->count - at) * sizeof(*d->lens));

    d->lines[at] = copy;
    d->lens[at] = len;
    d->count++;
    return true;
}

static void doc_remove_line(edit_doc_t *d, size_t at)
{
    if (at >= d->count) return;
    free(d->lines[at]);
    memmove(d->lines + at, d->lines + at + 1,
            (d->count - at - 1) * sizeof(*d->lines));
    memmove(d->lens + at, d->lens + at + 1,
            (d->count - at - 1) * sizeof(*d->lens));
    d->count--;
}

static bool line_replace(char **line, size_t *len, size_t at, size_t remove,
                         const char *ins, size_t ins_len)
{
    if (*len < at + remove) return false;
    if (ins_len > (size_t)-1 - (*len - remove) - 1) return false;

    size_t new_len = *len - remove + ins_len;
    char *buf = malloc(new_len + 1);
    if (buf == NULL) return false;

    if (at > 0) memcpy(buf, *line, at);
    if (ins_len > 0) memcpy(buf + at, ins, ins_len);
    memcpy(buf + at + ins_len, *line + at + remove,
           *len - at - remove);
    buf[new_len] = '\0';

    free(*line);
    *line = buf;
    *len = new_len;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Low-level Raw Operations for Undo/Redo Engine                              */
/* -------------------------------------------------------------------------- */

static void notify_syntax_edit(editor_t *ed, size_t start_line, size_t old_total_lines)
{
    if (ed->active_view && ed->active_view->buf) {
        size_t new_total = ed->doc.count;
        size_t old_span = 1;
        size_t new_span = 1;
        if (new_total > old_total_lines) {
            new_span = 1 + (new_total - old_total_lines);
        } else if (old_total_lines > new_total) {
            old_span = 1 + (old_total_lines - new_total);
        }
        buffer_syntax_on_edit(ed->active_view->buf, start_line, old_span, new_span);
    }
}

void editor_raw_insert(editor_t *ed, size_t line, size_t col,
                       const char *text, size_t len)
{
    size_t old_count = ed->doc.count;
    if (line >= ed->doc.count) {
        if (!doc_insert_line(&ed->doc, line, "", 0)) return;
    }

    char *cur = ed->doc.lines[line];
    size_t cur_len = ed->doc.lens[line];
    size_t at = edit_rune_to_byte(cur, cur_len, col);

    if (len == 1 && text[0] == '\n') {
        doc_insert_line(&ed->doc, line + 1, cur + at, cur_len - at);
        line_replace(&ed->doc.lines[line], &ed->doc.lens[line], at, cur_len - at, "", 0);
        editor_sync_to_active(ed);
        notify_syntax_edit(ed, line, old_count);
        return;
    }

    const char *nl = memchr(text, '\n', len);
    if (nl == NULL) {
        line_replace(&ed->doc.lines[line], &ed->doc.lens[line], at, 0, text, len);
    } else {
        size_t first_part_len = nl - text;
        size_t rest_offset    = first_part_len + 1;
        size_t tail_len       = cur_len - at;
        char *tail = malloc(tail_len + 1);
        if (tail != NULL) {
            memcpy(tail, cur + at, tail_len);
            tail[tail_len] = '\0';
        }

        line_replace(&ed->doc.lines[line], &ed->doc.lens[line], at, cur_len - at, text, first_part_len);

        size_t cur_l = line + 1;
        const char *p = text + rest_offset;
        size_t rem = len - rest_offset;

        while (rem > 0) {
            const char *next_nl = memchr(p, '\n', rem);
            if (next_nl == NULL) {
                size_t p_len = rem;
                size_t combined_len = p_len + (tail ? tail_len : 0);
                char *combined = malloc(combined_len + 1);
                if (combined != NULL) {
                    memcpy(combined, p, p_len);
                    if (tail != NULL && tail_len > 0) {
                        memcpy(combined + p_len, tail, tail_len);
                    }
                    combined[combined_len] = '\0';
                    doc_insert_line(&ed->doc, cur_l, combined, combined_len);
                    free(combined);
                }
                break;
            } else {
                size_t seg_len = next_nl - p;
                doc_insert_line(&ed->doc, cur_l, p, seg_len);
                cur_l++;
                p = next_nl + 1;
                rem = (text + len) - p;
            }
        }
        free(tail);
    }
    editor_sync_to_active(ed);
    notify_syntax_edit(ed, line, old_count);
}

static void raw_remove_internal(editor_t *ed, size_t line, size_t col, size_t runes)
{
    if (line >= ed->doc.count) return;

    char *cur = ed->doc.lines[line];
    size_t cur_len = ed->doc.lens[line];
    size_t line_runes = edit_line_runes(cur, cur_len);

    if (col == line_runes && runes == 1 && line + 1 < ed->doc.count) {
        char *next = ed->doc.lines[line + 1];
        size_t next_len = ed->doc.lens[line + 1];
        line_replace(&ed->doc.lines[line], &ed->doc.lens[line], cur_len, 0, next, next_len);
        doc_remove_line(&ed->doc, line + 1);
        return;
    }

    size_t from = edit_rune_to_byte(cur, cur_len, col);
    size_t to   = edit_rune_to_byte(cur, cur_len, col + runes);
    line_replace(&ed->doc.lines[line], &ed->doc.lens[line], from, to - from, "", 0);
}

void editor_raw_remove(editor_t *ed, size_t line, size_t col, size_t runes)
{
    size_t old_count = ed->doc.count;
    raw_remove_internal(ed, line, col, runes);
    editor_sync_to_active(ed);
    notify_syntax_edit(ed, line, old_count);
}

void editor_raw_remove_str(editor_t *ed, size_t line, size_t col, const char *text, size_t len)
{
    if (line >= ed->doc.count || text == NULL || len == 0) return;

    size_t old_count = ed->doc.count;
    size_t nl_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') nl_count++;
    }

    if (nl_count == 0) {
        size_t runes = edit_line_runes(text, len);
        raw_remove_internal(ed, line, col, runes);
        editor_sync_to_active(ed);
        notify_syntax_edit(ed, line, old_count);
        return;
    }

    const char *first_nl = memchr(text, '\n', len);
    size_t first_len = first_nl - text;
    size_t first_runes = edit_line_runes(text, first_len);
    if (first_runes > 0) {
        raw_remove_internal(ed, line, col, first_runes);
    }

    for (size_t i = 1; i < nl_count; i++) {
        if (line + 1 < ed->doc.count) {
            doc_remove_line(&ed->doc, line + 1);
        }
    }

    const char *last_nl = memrchr(text, '\n', len);
    size_t last_len = (text + len) - (last_nl + 1);
    size_t last_runes = edit_line_runes(last_nl + 1, last_len);
    if (line + 1 < ed->doc.count) {
        if (last_runes > 0) {
            raw_remove_internal(ed, line + 1, 0, last_runes);
        }
        size_t cur_runes = edit_line_runes(ed->doc.lines[line], ed->doc.lens[line]);
        raw_remove_internal(ed, line, cur_runes, 1);
    }
    editor_sync_to_active(ed);
    notify_syntax_edit(ed, line, old_count);
}

/* -------------------------------------------------------------------------- */
/* Rune & Visual Column Helpers                                               */
/* -------------------------------------------------------------------------- */

size_t edit_line_runes(const char *line, size_t len)
{
    size_t runes = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        (void)cp;
        if (used == 0) break;
        i += used;
        runes++;
    }
    return runes;
}

size_t edit_rune_to_byte(const char *line, size_t len, size_t col)
{
    size_t i = 0;
    while (i < len && col > 0) {
        uint32_t cp;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        (void)cp;
        if (used == 0) break;
        i += used;
        col--;
    }
    return i;
}

int edit_visual_col(const char *line, size_t len, size_t col)
{
    int cells = 0;
    size_t i = 0;
    while (i < len && col > 0) {
        uint32_t cp;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        if (used == 0) break;

        if (cp == '\t') {
            cells += EDIT_TABSTOP - (cells % EDIT_TABSTOP);
        } else {
            cells += tam_uni_width(cp);
        }

        i += used;
        col--;
    }
    return cells;
}

size_t edit_visual_to_rune(const char *line, size_t len, int cells)
{
    int visual = 0;
    size_t i = 0;
    size_t col = 0;
    while (i < len && visual < cells) {
        uint32_t cp;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        if (used == 0) break;

        int width = cp == '\t'
                        ? EDIT_TABSTOP - (visual % EDIT_TABSTOP)
                        : tam_uni_width(cp);

        if (visual + width > cells) break;
        visual += width;
        i += used;
        col++;
    }
    return col;
}

/* -------------------------------------------------------------------------- */
/* Softwrap Helpers                                                           */
/* -------------------------------------------------------------------------- */

size_t edit_softwrap_line_sublines(const char *line, size_t len, int width,
                                   int tabsize, bool wordwrap)
{
    if (width <= 1) return 1;
    if (tabsize <= 0) tabsize = 4;

    size_t sublines = 1;
    int vx = 0;
    size_t i = 0;
    size_t last_word_end = 0;

    while (i < len) {
        uint32_t cp = 0;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        if (used == 0) break;

        int cw = (cp == '\t') ? (tabsize - (vx % tabsize)) :
                 ((cp < 0x20 || cp == 0x7F) ? 1 : tam_uni_width(cp));

        if (vx + cw > width && vx > 0) {
            if (wordwrap && last_word_end > 0 && last_word_end < i) {
                i = last_word_end;
                sublines++;
                vx = 0;
                last_word_end = 0;
                continue;
            } else {
                sublines++;
                vx = 0;
                last_word_end = 0;
            }
        }

        if (isspace((unsigned char)cp) || cp == '-' || cp == '_' ||
            cp == '/' || cp == '\\') {
            last_word_end = i + used;
        }

        vx += cw;
        i += used;
    }

    return sublines;
}

void edit_softwrap_loc_to_vloc(const char *line, size_t len, size_t rune_col,
                               int width, int tabsize, bool wordwrap,
                               int *subline_out, int *vx_out)
{
    if (width <= 1) width = 80;
    if (tabsize <= 0) tabsize = 4;

    int subline = 0;
    int vx = 0;
    size_t i = 0;
    size_t cur_rune = 0;
    size_t last_word_end = 0;
    size_t last_word_rune = 0;

    while (i < len && cur_rune < rune_col) {
        uint32_t cp = 0;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        if (used == 0) break;

        int cw = (cp == '\t') ? (tabsize - (vx % tabsize)) :
                 ((cp < 0x20 || cp == 0x7F) ? 1 : tam_uni_width(cp));

        if (vx + cw > width && vx > 0) {
            if (wordwrap && last_word_end > 0 && last_word_end < i) {
                i = last_word_end;
                cur_rune = last_word_rune;
                subline++;
                vx = 0;
                last_word_end = 0;
                continue;
            } else {
                subline++;
                vx = 0;
                last_word_end = 0;
            }
        }

        if (isspace((unsigned char)cp) || cp == '-' || cp == '_' ||
            cp == '/' || cp == '\\') {
            last_word_end = i + used;
            last_word_rune = cur_rune + 1;
        }

        vx += cw;
        i += used;
        cur_rune++;
    }

    if (subline_out != NULL) *subline_out = subline;
    if (vx_out != NULL) *vx_out = vx;
}

size_t edit_softwrap_vloc_to_rune(const char *line, size_t len, int target_subline,
                                  int target_vx, int width, int tabsize,
                                  bool wordwrap)
{
    if (width <= 1) width = 80;
    if (tabsize <= 0) tabsize = 4;

    int subline = 0;
    int vx = 0;
    size_t i = 0;
    size_t cur_rune = 0;
    size_t last_word_end = 0;
    size_t last_word_rune = 0;

    while (i < len) {
        if (subline == target_subline && vx >= target_vx) {
            return cur_rune;
        }

        uint32_t cp = 0;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        if (used == 0) break;

        int cw = (cp == '\t') ? (tabsize - (vx % tabsize)) :
                 ((cp < 0x20 || cp == 0x7F) ? 1 : tam_uni_width(cp));

        if (vx + cw > width && vx > 0) {
            if (wordwrap && last_word_end > 0 && last_word_end < i) {
                i = last_word_end;
                cur_rune = last_word_rune;
                subline++;
                vx = 0;
                last_word_end = 0;
                if (subline > target_subline) return cur_rune;
                continue;
            } else {
                subline++;
                vx = 0;
                last_word_end = 0;
                if (subline > target_subline) return cur_rune;
            }
        }

        if (subline == target_subline && vx + cw > target_vx) {
            return cur_rune;
        }

        if (isspace((unsigned char)cp) || cp == '-' || cp == '_' ||
            cp == '/' || cp == '\\') {
            last_word_end = i + used;
            last_word_rune = cur_rune + 1;
        }

        vx += cw;
        i += used;
        cur_rune++;
    }

    return cur_rune;
}

static void touch(editor_t *ed)
{
    ed->dirty = true;
    ed->message[0] = '\0';
    editor_sync_to_active(ed);
}

/* -------------------------------------------------------------------------- */
/* Active View & Document Synchronization                                     */
/* -------------------------------------------------------------------------- */

void editor_sync_to_active(editor_t *ed)
{
    if (ed->active_view != NULL && ed->active_view->buf != NULL) {
        buffer_t *b = ed->active_view->buf;
        b->doc         = ed->doc;
        b->dirty       = ed->dirty;
        b->crlf        = ed->crlf;
        b->undo_stack  = ed->undo_stack;
        b->redo_stack  = ed->redo_stack;

        view_t *v = ed->active_view;
        v->cx_line     = ed->cx_line;
        v->cx_col      = ed->cx_col;
        v->goal_col    = ed->goal_col;
        v->top_line    = ed->top_line;
        v->left_col    = ed->left_col;
        v->top_subline = ed->top_subline;
        v->sel         = ed->sel;
    }
}

void editor_sync_from_active(editor_t *ed)
{
    if (ed->active_view != NULL && ed->active_view->buf != NULL) {
        buffer_t *b = ed->active_view->buf;
        ed->doc         = b->doc;
        ed->path        = b->path;
        ed->dirty       = b->dirty;
        ed->crlf        = b->crlf;
        ed->undo_stack  = b->undo_stack;
        ed->redo_stack  = b->redo_stack;

        view_t *v = ed->active_view;
        ed->cx_line     = v->cx_line;
        ed->cx_col      = v->cx_col;
        ed->goal_col    = v->goal_col;
        ed->top_line    = v->top_line;
        ed->left_col    = v->left_col;
        ed->top_subline = v->top_subline;
        ed->sel         = v->sel;
    }
}

void editor_set_active_view(editor_t *ed, view_t *v)
{
    if (v == NULL) return;
    editor_sync_to_active(ed);
    ed->active_view = v;
    if (ed->tabs.active < ed->tabs.count) {
        ed->tabs.tabs[ed->tabs.active]->active_view = v;
    }
    editor_sync_from_active(ed);
}

/* -------------------------------------------------------------------------- */
/* Public Lifecycle API                                                       */
/* -------------------------------------------------------------------------- */

void editor_init(editor_t *ed)
{
    memset(ed, 0, sizeof(*ed));
    ed->running    = true;
    ed->tty_out_fd = -1;

    config_init_defaults(&ed->config);
    colorscheme_init(&ed->colorscheme);
    config_load(&ed->config, &ed->keybindings);
    colorscheme_load(&ed->colorscheme, ed->config.colorscheme);

    tab_list_init(&ed->tabs);
    buffer_t *b = buffer_new(NULL);
    b->syntax_enabled = ed->config.syntax;
    view_t *v   = view_new(b);
    v->softwrap = ed->config.softwrap;
    v->wordwrap = ed->config.wordwrap;

    tab_list_add(&ed->tabs, v);
    ed->active_view = v;
    editor_sync_from_active(ed);
}

void editor_free(editor_t *ed)
{
    if (ed == NULL) return;
    editor_sync_to_active(ed);
    tab_list_free(&ed->tabs);
    colorscheme_free(&ed->colorscheme);
    keybinds_free(ed->keybindings);
    ed->keybindings = NULL;
    ed->active_view = NULL;
}

void editor_set_message(editor_t *ed, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ed->message, sizeof(ed->message), fmt, ap);
    va_end(ap);
}

bool editor_load(editor_t *ed, const char *path)
{
    buffer_t *b = buffer_open(path);
    if (b == NULL) {
        editor_set_message(ed, "Could not open %s", path);
        return false;
    }
    b->syntax_enabled = ed->config.syntax;
    if (ed->config.detectlimit != 100) {
        buffer_syntax_init(b, NULL, ed->config.detectlimit);
    }

    if (ed->active_view != NULL) {
        buffer_unref(ed->active_view->buf);
        ed->active_view->buf = b;
        buffer_ref(b);
        ed->active_view->cx_line     = 0;
        ed->active_view->cx_col      = 0;
        ed->active_view->goal_col    = 0;
        ed->active_view->top_line    = 0;
        ed->active_view->left_col    = 0;
        ed->active_view->top_subline = 0;
        ed->active_view->sel.active  = false;
        editor_sync_from_active(ed);
        return true;
    }
    return false;
}

bool editor_save_to(editor_t *ed, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        editor_set_message(ed, "No file name");
        return false;
    }

    editor_sync_to_active(ed);
    if (ed->active_view && ed->active_view->buf) {
        bool ok = buffer_save_to(ed->active_view->buf, path);
        if (ok) {
            editor_sync_from_active(ed);
            editor_set_message(ed, "Saved to %s", path);
            return true;
        }
    }
    editor_set_message(ed, "Could not save %s", path);
    return false;
}

bool editor_save(editor_t *ed)
{
    editor_sync_to_active(ed);
    if (ed->active_view && ed->active_view->buf) {
        const char *p = ed->active_view->buf->path;
        if (p == NULL || *p == '\0') {
            editor_set_message(ed, "No file name");
            return false;
        }
        bool ok = buffer_save(ed->active_view->buf);
        if (ok) {
            editor_sync_from_active(ed);
            editor_set_message(ed, "Saved %s", p);
            return true;
        }
    }
    editor_set_message(ed, "Could not save");
    return false;
}

/* -------------------------------------------------------------------------- */
/* Split and Tab Actions                                                      */
/* -------------------------------------------------------------------------- */

bool editor_vsplit(editor_t *ed, const char *path)
{
    if (ed->tabs.count == 0) return false;
    tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
    pane_node_t *cur_node = pane_find_view(cur_tab->root, ed->active_view);
    if (cur_node == NULL) return false;

    buffer_t *b = (path != NULL && *path != '\0') ? buffer_open(path)
                                                  : ed->active_view->buf;
    if (b == NULL) return false;
    if (path != NULL && *path != '\0') {
        b->syntax_enabled = ed->config.syntax;
        if (ed->config.detectlimit != 100) {
            buffer_syntax_init(b, NULL, ed->config.detectlimit);
        }
    }

    view_t *nv = view_new(b);
    if (nv == NULL) return false;
    nv->softwrap = ed->config.softwrap;
    nv->wordwrap = ed->config.wordwrap;

    pane_node_t *new_node = pane_split(cur_node, PANE_SPLIT_V, nv);
    if (new_node != NULL) {
        editor_set_active_view(ed, nv);
        return true;
    }
    view_free(nv);
    return false;
}

bool editor_hsplit(editor_t *ed, const char *path)
{
    if (ed->tabs.count == 0) return false;
    tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
    pane_node_t *cur_node = pane_find_view(cur_tab->root, ed->active_view);
    if (cur_node == NULL) return false;

    buffer_t *b = (path != NULL && *path != '\0') ? buffer_open(path)
                                                  : ed->active_view->buf;
    if (b == NULL) return false;
    if (path != NULL && *path != '\0') {
        b->syntax_enabled = ed->config.syntax;
        if (ed->config.detectlimit != 100) {
            buffer_syntax_init(b, NULL, ed->config.detectlimit);
        }
    }

    view_t *nv = view_new(b);
    if (nv == NULL) return false;
    nv->softwrap = ed->config.softwrap;
    nv->wordwrap = ed->config.wordwrap;

    pane_node_t *new_node = pane_split(cur_node, PANE_SPLIT_H, nv);
    if (new_node != NULL) {
        editor_set_active_view(ed, nv);
        return true;
    }
    view_free(nv);
    return false;
}

bool editor_unsplit(editor_t *ed)
{
    if (ed->tabs.count == 0) return false;
    tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
    pane_node_t *cur_node = pane_find_view(cur_tab->root, ed->active_view);
    if (cur_node == NULL || cur_node == cur_tab->root) return false;

    view_t *next_v = pane_next_view(cur_tab->root, ed->active_view);
    pane_close(cur_tab->root, cur_node, &cur_tab->root);
    editor_set_active_view(ed, next_v);
    return true;
}

bool editor_next_split(editor_t *ed)
{
    if (ed->tabs.count == 0) return false;
    tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
    view_t *nv = pane_next_view(cur_tab->root, ed->active_view);
    if (nv != NULL && nv != ed->active_view) {
        editor_set_active_view(ed, nv);
        return true;
    }
    return false;
}

bool editor_prev_split(editor_t *ed)
{
    if (ed->tabs.count == 0) return false;
    tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
    view_t *nv = pane_prev_view(cur_tab->root, ed->active_view);
    if (nv != NULL && nv != ed->active_view) {
        editor_set_active_view(ed, nv);
        return true;
    }
    return false;
}

bool editor_add_tab(editor_t *ed, const char *path)
{
    buffer_t *b = (path != NULL && *path != '\0') ? buffer_open(path)
                                                  : buffer_new(NULL);
    if (b == NULL) return false;
    b->syntax_enabled = ed->config.syntax;
    if (ed->config.detectlimit != 100) {
        buffer_syntax_init(b, NULL, ed->config.detectlimit);
    }

    view_t *v = view_new(b);
    if (v == NULL) return false;
    v->softwrap = ed->config.softwrap;
    v->wordwrap = ed->config.wordwrap;

    tab_t *t = tab_list_add(&ed->tabs, v);
    if (t != NULL) {
        editor_set_active_view(ed, v);
        return true;
    }
    view_free(v);
    return false;
}

bool editor_close_tab(editor_t *ed)
{
    if (ed->tabs.count <= 1) return false;
    tab_list_close(&ed->tabs, ed->tabs.active);
    tab_t *cur = ed->tabs.tabs[ed->tabs.active];
    editor_set_active_view(ed, cur->active_view);
    return true;
}

bool editor_next_tab(editor_t *ed)
{
    if (ed->tabs.count <= 1) return false;
    tab_list_next(&ed->tabs);
    tab_t *cur = ed->tabs.tabs[ed->tabs.active];
    editor_set_active_view(ed, cur->active_view);
    return true;
}

bool editor_prev_tab(editor_t *ed)
{
    if (ed->tabs.count <= 1) return false;
    tab_list_prev(&ed->tabs);
    tab_t *cur = ed->tabs.tabs[ed->tabs.active];
    editor_set_active_view(ed, cur->active_view);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Text Editing API                                                           */
/* -------------------------------------------------------------------------- */

void editor_insert_codepoint(editor_t *ed, uint32_t cp)
{
    if (ed->sel.active) {
        selection_delete(ed);
    }

    char utf8[4];
    size_t n = tam_utf8_encode(cp, utf8);

    undo_record_insert(ed, ed->cx_line, ed->cx_col, utf8, n);
    undo_clear_redo(ed);

    editor_raw_insert(ed, ed->cx_line, ed->cx_col, utf8, n);
    ed->cx_col++;
    ed->goal_col = ed->cx_col;
    touch(ed);
}

void editor_insert_text(editor_t *ed, const char *text, size_t len)
{
    if (text == NULL || len == 0) return;
    if (ed->sel.active) {
        selection_delete(ed);
    }

    undo_record_insert(ed, ed->cx_line, ed->cx_col, text, len);
    undo_clear_redo(ed);

    editor_raw_insert(ed, ed->cx_line, ed->cx_col, text, len);

    const char *last_nl = memrchr(text, '\n', len);
    if (last_nl == NULL) {
        ed->cx_col += edit_line_runes(text, len);
    } else {
        size_t n_lines = 0;
        for (size_t i = 0; i < len; i++) {
            if (text[i] == '\n') n_lines++;
        }
        ed->cx_line += n_lines;
        size_t last_part_len = (text + len) - (last_nl + 1);
        ed->cx_col = edit_line_runes(last_nl + 1, last_part_len);
    }
    ed->goal_col = ed->cx_col;
    cursor_clamp(ed);
    touch(ed);
}

void editor_insert_newline(editor_t *ed)
{
    if (ed->sel.active) {
        selection_delete(ed);
    }

    undo_record_insert(ed, ed->cx_line, ed->cx_col, "\n", 1);
    undo_clear_redo(ed);

    editor_raw_insert(ed, ed->cx_line, ed->cx_col, "\n", 1);
    ed->cx_line++;
    ed->cx_col = 0;
    ed->goal_col = 0;
    touch(ed);
}

void editor_insert_tab(editor_t *ed)
{
    if (ed->config.tabstospaces && ed->config.tabsize > 0) {
        int spaces = ed->config.tabsize;
        char sp[16];
        if (spaces > 16) spaces = 16;
        memset(sp, ' ', spaces);
        editor_insert_text(ed, sp, spaces);
    } else {
        editor_insert_codepoint(ed, '\t');
    }
}

void editor_backspace(editor_t *ed)
{
    if (ed->sel.active) {
        selection_delete(ed);
        touch(ed);
        return;
    }

    if (ed->cx_col > 0) {
        char *line = ed->doc.lines[ed->cx_line];
        size_t len = ed->doc.lens[ed->cx_line];
        size_t from = edit_rune_to_byte(line, len, ed->cx_col - 1);
        size_t to   = edit_rune_to_byte(line, len, ed->cx_col);

        undo_record_remove(ed, ed->cx_line, ed->cx_col - 1, line + from, to - from);
        undo_clear_redo(ed);

        editor_raw_remove(ed, ed->cx_line, ed->cx_col - 1, 1);
        ed->cx_col--;
        ed->goal_col = ed->cx_col;
        touch(ed);
        return;
    }

    if (ed->cx_line == 0) return;

    size_t prev = ed->cx_line - 1;
    size_t prev_runes = edit_line_runes(ed->doc.lines[prev],
                                        ed->doc.lens[prev]);

    undo_record_remove(ed, prev, prev_runes, "\n", 1);
    undo_clear_redo(ed);

    editor_raw_remove(ed, prev, prev_runes, 1);
    ed->cx_line = prev;
    ed->cx_col  = prev_runes;
    ed->goal_col = ed->cx_col;
    touch(ed);
}

void editor_delete(editor_t *ed)
{
    if (ed->sel.active) {
        selection_delete(ed);
        touch(ed);
        return;
    }

    size_t runes = edit_line_runes(ed->doc.lines[ed->cx_line],
                                   ed->doc.lens[ed->cx_line]);

    if (ed->cx_col < runes) {
        char *line = ed->doc.lines[ed->cx_line];
        size_t len = ed->doc.lens[ed->cx_line];
        size_t from = edit_rune_to_byte(line, len, ed->cx_col);
        size_t to   = edit_rune_to_byte(line, len, ed->cx_col + 1);

        undo_record_remove(ed, ed->cx_line, ed->cx_col, line + from, to - from);
        undo_clear_redo(ed);

        editor_raw_remove(ed, ed->cx_line, ed->cx_col, 1);
        touch(ed);
        return;
    }

    if (ed->cx_line + 1 >= ed->doc.count) return;

    undo_record_remove(ed, ed->cx_line, runes, "\n", 1);
    undo_clear_redo(ed);

    editor_raw_remove(ed, ed->cx_line, runes, 1);
    touch(ed);
}

bool editor_undo(editor_t *ed)
{
    bool ok = undo_perform(ed);
    if (ok) touch(ed);
    return ok;
}

bool editor_redo(editor_t *ed)
{
    bool ok = redo_perform(ed);
    if (ok) touch(ed);
    return ok;
}

void editor_move(editor_t *ed, long dx, long dy)
{
    cursor_move(ed, dx, dy, false);
}

void editor_home(editor_t *ed)
{
    cursor_home(ed, false);
}

void editor_end(editor_t *ed)
{
    cursor_end(ed, false);
}

void editor_page(editor_t *ed, long pages, int text_rows)
{
    cursor_page(ed, pages, text_rows, false);
}
