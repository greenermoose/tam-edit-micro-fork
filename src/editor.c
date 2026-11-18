#include "editor.h"
#include "buffer.h"
#include "complete.h"
#include "cursor.h"
#include "search.h"
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

        for (;;) {
            const char *next_nl = (rem > 0) ? memchr(p, '\n', rem) : NULL;
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
    int ans_subline = 0;
    int ans_vx = 0;
    bool have_ans = false;

    while (i < len) {
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
                if (cur_rune <= rune_col) {
                    have_ans = false;
                }
                continue;
            } else {
                subline++;
                vx = 0;
                last_word_end = 0;
            }
        }

        if (cur_rune == rune_col) {
            ans_subline = subline;
            ans_vx = vx;
            have_ans = true;
            if (!wordwrap) break;
        }

        if (isspace((unsigned char)cp) || cp == '-' || cp == '_' ||
            cp == '/' || cp == '\\') {
            last_word_end = i + used;
            last_word_rune = cur_rune + 1;
        }

        if (have_ans && last_word_rune > rune_col) {
            break;
        }

        vx += cw;
        i += used;
        cur_rune++;
    }

    if (!have_ans) {
        ans_subline = subline;
        ans_vx = vx;
    }

    if (subline_out != NULL) *subline_out = ans_subline;
    if (vx_out != NULL) *vx_out = ans_vx;
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
    size_t last_on_target = 0;
    bool have_on_target = false;

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
                if (subline > target_subline) {
                    if (have_on_target && last_word_rune > 0) {
                        return last_word_rune - 1;
                    }
                    return cur_rune;
                }
                continue;
            } else {
                subline++;
                vx = 0;
                last_word_end = 0;
                if (subline > target_subline) {
                    return have_on_target ? last_on_target : cur_rune;
                }
            }
        }

        if (subline == target_subline && vx + cw > target_vx) {
            return cur_rune;
        }

        if (subline == target_subline) {
            last_on_target = cur_rune;
            have_on_target = true;
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

static void sync_cursors_copy(cursor_t **dst, size_t *dst_cnt, size_t *dst_cap,
                              const cursor_t *src, size_t src_cnt)
{
    if (src_cnt == 0) {
        *dst_cnt = 0;
        return;
    }
    if (src_cnt > *dst_cap) {
        size_t ncap = src_cnt * 2;
        cursor_t *nc = realloc(*dst, ncap * sizeof(*nc));
        if (!nc) return;
        *dst = nc;
        *dst_cap = ncap;
    }
    memcpy(*dst, src, src_cnt * sizeof(cursor_t));
    *dst_cnt = src_cnt;
}

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
        sync_cursors_copy(&v->extra_cursors, &v->extra_count, &v->extra_cap,
                          ed->extra_cursors, ed->extra_count);
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
        sync_cursors_copy(&ed->extra_cursors, &ed->extra_count, &ed->extra_cap,
                          v->extra_cursors, v->extra_count);
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

static void prompt_history_free(prompt_history_t *h)
{
    for (int i = 0; i < h->count; i++) {
        free(h->items[i]);
        h->items[i] = NULL;
    }
    h->count = 0;
    h->cursor = -1;
}

void editor_init(editor_t *ed)
{
    memset(ed, 0, sizeof(*ed));
    ed->running        = true;
    ed->tty_out_fd     = -1;
    ed->bg_job.pipe_fd = -1;
    ed->hist_cmd.cursor   = -1;
    ed->hist_find.cursor  = -1;
    ed->hist_shell.cursor = -1;

    config_init_defaults(&ed->config);
    colorscheme_init(&ed->colorscheme);
    config_load(&ed->config, &ed->keybindings);
    colorscheme_load(&ed->colorscheme, ed->config.colorscheme);
    ed->search.ignorecase = ed->config.ignorecase;

    tab_list_init(&ed->tabs);
    buffer_t *b = buffer_new(NULL);
    snprintf(b->encoding, sizeof(b->encoding), "%s", ed->config.encoding);
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
    shell_bg_free(&ed->bg_job);
    search_invalidate(ed);
    prompt_comp_clear(&ed->prompt);
    prompt_history_free(&ed->hist_cmd);
    prompt_history_free(&ed->hist_find);
    prompt_history_free(&ed->hist_shell);
    editor_sync_to_active(ed);
    free(ed->extra_cursors);
    ed->extra_cursors = NULL;
    ed->extra_count = 0;
    ed->extra_cap = 0;
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

static buffer_t *open_configured_buffer(const editor_t *ed, const char *path)
{
    buffer_t *b = buffer_open_with_encoding(path, ed->config.encoding, ed->config.detectlimit);
    if (b) {
        b->syntax_enabled = ed->config.syntax;
    }
    return b;
}

bool editor_load(editor_t *ed, const char *path)
{
    buffer_t *b = open_configured_buffer(ed, path);
    if (b == NULL) {
        editor_set_message(ed, "Could not open %s", path);
        return false;
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
        ed->active_view->extra_count = 0;
        editor_sync_from_active(ed);
        if (b->used_fallback_enc && b->fallback_msg[0]) {
            editor_set_message(ed, "%s", b->fallback_msg);
        }
        return true;
    }
    buffer_unref(b);
    return false;
}

bool editor_open_file(editor_t *ed, const char *path, bool in_new_tab)
{
    if (!path || !*path) return false;
    if (in_new_tab) return editor_add_tab(ed, path);
    return editor_load(ed, path);
}

bool editor_open_scratch(editor_t *ed, const char *title, const char *text, bool split_help)
{
    if (!ed) return false;
    buffer_t *b = buffer_new_scratch(title, text);
    if (!b) return false;

    if (split_help && ed->tabs.count > 0 && ed->active_view != NULL) {
        tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
        pane_node_t *cur_node = pane_find_view(cur_tab->root, ed->active_view);
        if (cur_node) {
            view_t *nv = view_new(b);
            if (!nv) {
                buffer_ref(b);
                buffer_unref(b);
                return false;
            }
            nv->softwrap = ed->config.softwrap;
            nv->wordwrap = ed->config.wordwrap;
            pane_type_t stype = (strcmp(ed->config.helpsplit, "vsplit") == 0)
                                ? PANE_SPLIT_V : PANE_SPLIT_H;
            pane_node_t *nn = pane_split(cur_node, stype, nv);
            if (nn) {
                editor_set_active_view(ed, nv);
                return true;
            }
            view_free(nv);
            return false;
        }
    }

    if (!editor_add_tab(ed, NULL) || ed->active_view == NULL) {
        buffer_ref(b);
        buffer_unref(b);
        return false;
    }
    buffer_unref(ed->active_view->buf);
    ed->active_view->buf = b;
    buffer_ref(b);
    ed->active_view->cx_line = 0;
    ed->active_view->cx_col = 0;
    ed->active_view->goal_col = 0;
    ed->active_view->sel.active = false;
    ed->active_view->extra_count = 0;
    editor_sync_from_active(ed);
    return true;
}

bool editor_save_to_configured(editor_t *ed, const char *path, char *err_buf, size_t err_size)
{
    if (err_buf && err_size > 0) err_buf[0] = '\0';
    if (path == NULL || path[0] == '\0') {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "No file name");
        editor_set_message(ed, "No file name");
        return false;
    }

    editor_sync_to_active(ed);
    if (ed->active_view && ed->active_view->buf) {
        char err[192] = "";
        bool ok = buffer_save_to_with_opts(ed->active_view->buf, path,
                                           ed->config.rmtrailingws,
                                           ed->config.eofnewline,
                                           ed->config.mkparents,
                                           err, sizeof(err));
        if (ok) {
            editor_sync_from_active(ed);
            cursor_clamp(ed);
            editor_set_message(ed, "Saved to %s", path);
            return true;
        }
        if (err[0]) {
            if (err_buf && err_size > 0) snprintf(err_buf, err_size, "%s", err);
            editor_set_message(ed, "%s", err);
            return false;
        }
    }
    if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Could not save %s", path);
    editor_set_message(ed, "Could not save %s", path);
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

bool editor_save_configured(editor_t *ed, char *err_buf, size_t err_size)
{
    if (err_buf && err_size > 0) err_buf[0] = '\0';
    editor_sync_to_active(ed);
    if (ed->active_view && ed->active_view->buf) {
        const char *p = ed->active_view->buf->path;
        if (p == NULL || *p == '\0') {
            if (err_buf && err_size > 0) snprintf(err_buf, err_size, "No file name");
            editor_set_message(ed, "No file name");
            return false;
        }
        char err[192] = "";
        bool ok = buffer_save_with_opts(ed->active_view->buf,
                                        ed->config.rmtrailingws,
                                        ed->config.eofnewline,
                                        ed->config.mkparents,
                                        err, sizeof(err));
        if (ok) {
            editor_sync_from_active(ed);
            cursor_clamp(ed);
            editor_set_message(ed, "Saved %s", p);
            return true;
        }
        if (err[0]) {
            if (err_buf && err_size > 0) snprintf(err_buf, err_size, "%s", err);
            editor_set_message(ed, "%s", err);
            return false;
        }
    }
    if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Could not save");
    editor_set_message(ed, "Could not save");
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

bool editor_reopen(editor_t *ed, const char *encoding)
{
    if (!ed || !ed->active_view || !ed->active_view->buf) return false;
    editor_sync_to_active(ed);
    char err[192] = "";
    bool ok = buffer_reopen(ed->active_view->buf, encoding, ed->config.detectlimit,
                            err, sizeof(err));
    if (!ok) {
        editor_set_message(ed, "%s", err[0] ? err : "Reopen failed");
        return false;
    }
    editor_sync_from_active(ed);
    cursor_clamp(ed);
    ed->sel.active = false;
    ed->extra_count = 0;
    if (ed->active_view->buf->used_fallback_enc && ed->active_view->buf->fallback_msg[0]) {
        editor_set_message(ed, "%s", ed->active_view->buf->fallback_msg);
    } else {
        editor_set_message(ed, "Reopened (%s)", ed->active_view->buf->encoding);
    }
    return true;
}

bool editor_textfilter(editor_t *ed, const char *cmd)
{
    if (!ed || !cmd || !cmd[0]) return false;
    if (ed->active_view && ed->active_view->buf && ed->active_view->buf->readonly) {
        editor_set_message(ed, "Buffer is read-only");
        return false;
    }

    char *input = NULL;
    size_t in_len = 0;
    loc_t start = {0, 0}, end = {0, 0};
    bool has_sel = ed->sel.active;

    if (has_sel) {
        input = selection_get_text(ed, &in_len);
        selection_get_range(ed, &start, &end);
    } else {
        tam_str_t all = tam_str_new("");
        if (!all) return false;
        const char *sep = ed->crlf ? "\r\n" : "\n";
        for (size_t i = 0; i < ed->doc.count; i++) {
            if (i > 0) all = tam_str_cat(all, sep);
            all = tam_str_cat_len(all, ed->doc.lines[i], ed->doc.lens[i]);
        }
        in_len = tam_str_len(all);
        input = malloc(in_len + 1);
        if (input) memcpy(input, all, in_len + 1);
        tam_str_free(all);
        start = (loc_t){0, 0};
        end.line = ed->doc.count ? (ed->doc.count - 1) : 0;
        end.col = ed->doc.count ? edit_line_runes(ed->doc.lines[end.line], ed->doc.lens[end.line]) : 0;
    }
    if (!input) return false;

    bool had_nl = (in_len > 0 && input[in_len - 1] == '\n');
    char *out_text = NULL;
    size_t out_len = 0;
    char err[192] = "";
    bool ok = shell_textfilter(cmd, input, in_len, 30000, &out_text, &out_len, err, sizeof(err));
    free(input);
    if (!ok) {
        editor_set_message(ed, "%s", err[0] ? err : "textfilter failed");
        return false;
    }

    /* Trim single trailing newline when original input didn't end on '\n' */
    if (!had_nl && out_len > 0 && out_text[out_len - 1] == '\n') {
        out_len--;
        if (out_len > 0 && out_text[out_len - 1] == '\r') out_len--;
        out_text[out_len] = '\0';
    }

    undo_begin_group(ed);
    if (has_sel) {
        selection_delete(ed);
    } else if (ed->doc.count > 0) {
        ed->sel.active = true;
        ed->sel.anchor = start;
        ed->sel.cursor = end;
        selection_delete(ed);
    }
    if (out_len > 0) {
        undo_record_insert(ed, ed->cx_line, ed->cx_col, out_text, out_len);
        editor_raw_insert(ed, ed->cx_line, ed->cx_col, out_text, out_len);
        const char *last_nl = memrchr(out_text, '\n', out_len);
        if (!last_nl) {
            ed->cx_col += edit_line_runes(out_text, out_len);
        } else {
            size_t n_lines = 0;
            for (size_t i = 0; i < out_len; i++) if (out_text[i] == '\n') n_lines++;
            ed->cx_line += n_lines;
            ed->cx_col = edit_line_runes(last_nl + 1, (size_t)((out_text + out_len) - (last_nl + 1)));
        }
    }
    undo_clear_redo(ed);
    undo_end_group(ed);
    free(out_text);
    cursor_clamp(ed);
    touch(ed);
    editor_set_message(ed, "Filtered through: %s", cmd);
    return true;
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

    buffer_t *b = (path != NULL && *path != '\0') ? open_configured_buffer(ed, path)
                                                  : ed->active_view->buf;
    if (b == NULL) return false;

    view_t *nv = view_new(b);
    if (nv == NULL) {
        if (b != ed->active_view->buf) { buffer_ref(b); buffer_unref(b); }
        return false;
    }
    nv->softwrap = ed->config.softwrap;
    nv->wordwrap = ed->config.wordwrap;

    pane_node_t *new_node = pane_split(cur_node, PANE_SPLIT_V, nv);
    if (new_node != NULL) {
        editor_set_active_view(ed, nv);
        if (b->used_fallback_enc && b->fallback_msg[0]) {
            editor_set_message(ed, "%s", b->fallback_msg);
        }
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

    buffer_t *b = (path != NULL && *path != '\0') ? open_configured_buffer(ed, path)
                                                  : ed->active_view->buf;
    if (b == NULL) return false;

    view_t *nv = view_new(b);
    if (nv == NULL) {
        if (b != ed->active_view->buf) { buffer_ref(b); buffer_unref(b); }
        return false;
    }
    nv->softwrap = ed->config.softwrap;
    nv->wordwrap = ed->config.wordwrap;

    pane_node_t *new_node = pane_split(cur_node, PANE_SPLIT_H, nv);
    if (new_node != NULL) {
        editor_set_active_view(ed, nv);
        if (b->used_fallback_enc && b->fallback_msg[0]) {
            editor_set_message(ed, "%s", b->fallback_msg);
        }
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
    buffer_t *b = (path != NULL && *path != '\0') ? open_configured_buffer(ed, path)
                                                  : buffer_new(NULL);
    if (b == NULL) return false;
    if (path == NULL || *path == '\0') {
        snprintf(b->encoding, sizeof(b->encoding), "%s", ed->config.encoding);
        b->syntax_enabled = ed->config.syntax;
    }

    view_t *v = view_new(b);
    if (v == NULL) {
        buffer_ref(b);
        buffer_unref(b);
        return false;
    }
    v->softwrap = ed->config.softwrap;
    v->wordwrap = ed->config.wordwrap;

    tab_t *t = tab_list_add(&ed->tabs, v);
    if (t != NULL) {
        editor_set_active_view(ed, v);
        if (b->used_fallback_enc && b->fallback_msg[0]) {
            editor_set_message(ed, "%s", b->fallback_msg);
        }
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
/* Text Editing API & Multi-Cursor Dispatch                                   */
/* -------------------------------------------------------------------------- */

static bool check_writable(editor_t *ed)
{
    if (ed->active_view && ed->active_view->buf && ed->active_view->buf->readonly) {
        editor_set_message(ed, "Buffer is read-only");
        return false;
    }
    return true;
}

typedef enum {
    MC_OP_CODEPOINT,
    MC_OP_TEXT,
    MC_OP_NEWLINE,
    MC_OP_BACKSPACE,
    MC_OP_DELETE,
    MC_OP_DELETE_WORD_LEFT
} mc_op_kind_t;

typedef struct {
    cursor_t c;
    bool     is_primary;
} mc_item_t;

static int mc_item_cmp(const void *a, const void *b)
{
    const mc_item_t *ia = (const mc_item_t *)a;
    const mc_item_t *ib = (const mc_item_t *)b;
    if (ia->c.cx_line != ib->c.cx_line) {
        return (ia->c.cx_line < ib->c.cx_line) ? -1 : 1;
    }
    if (ia->c.cx_col != ib->c.cx_col) {
        return (ia->c.cx_col < ib->c.cx_col) ? -1 : 1;
    }
    return ia->is_primary ? -1 : (ib->is_primary ? 1 : 0);
}

static void insert_codepoint_single(editor_t *ed, uint32_t cp)
{
    if (ed->sel.active) {
        selection_delete(ed);
    }

    bool opened_grp = false;
    if (ed->config.overwrite && ed->cx_line < ed->doc.count) {
        size_t runes = edit_line_runes(ed->doc.lines[ed->cx_line], ed->doc.lens[ed->cx_line]);
        if (ed->cx_col < runes) {
            char *line = ed->doc.lines[ed->cx_line];
            size_t len = ed->doc.lens[ed->cx_line];
            size_t from = edit_rune_to_byte(line, len, ed->cx_col);
            size_t to = edit_rune_to_byte(line, len, ed->cx_col + 1);
            undo_begin_group(ed);
            opened_grp = true;
            undo_record_remove(ed, ed->cx_line, ed->cx_col, line + from, to - from);
            editor_raw_remove(ed, ed->cx_line, ed->cx_col, 1);
        }
    }

    char utf8[4];
    size_t n = tam_utf8_encode(cp, utf8);

    undo_record_insert(ed, ed->cx_line, ed->cx_col, utf8, n);
    undo_clear_redo(ed);

    editor_raw_insert(ed, ed->cx_line, ed->cx_col, utf8, n);
    ed->cx_col++;
    ed->goal_col = ed->cx_col;
    if (opened_grp) {
        undo_end_group(ed);
    }
}

static void insert_text_single(editor_t *ed, const char *text, size_t len)
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
        size_t last_part_len = (size_t)((text + len) - (last_nl + 1));
        ed->cx_col = edit_line_runes(last_nl + 1, last_part_len);
    }
    ed->goal_col = ed->cx_col;
    cursor_clamp(ed);
}

static void insert_newline_single(editor_t *ed)
{
    if (ed->sel.active) {
        selection_delete(ed);
    }

    size_t indent_len = 0;
    char indent_buf[256];
    if (ed->config.autoindent && ed->cx_line < ed->doc.count) {
        const char *line = ed->doc.lines[ed->cx_line];
        size_t len = ed->doc.lens[ed->cx_line];
        size_t max_b = edit_rune_to_byte(line, len, ed->cx_col);
        while (indent_len < max_b && indent_len + 1 < sizeof(indent_buf) &&
               (line[indent_len] == ' ' || line[indent_len] == '\t')) {
            indent_buf[indent_len] = line[indent_len];
            indent_len++;
        }
    }

    if (indent_len > 0) undo_begin_group(ed);
    undo_record_insert(ed, ed->cx_line, ed->cx_col, "\n", 1);
    undo_clear_redo(ed);

    editor_raw_insert(ed, ed->cx_line, ed->cx_col, "\n", 1);
    ed->cx_line++;
    ed->cx_col = 0;
    if (indent_len > 0) {
        undo_record_insert(ed, ed->cx_line, 0, indent_buf, indent_len);
        editor_raw_insert(ed, ed->cx_line, 0, indent_buf, indent_len);
        ed->cx_col = edit_line_runes(indent_buf, indent_len);
        undo_end_group(ed);
    }
    ed->goal_col = ed->cx_col;
}

static void backspace_single(editor_t *ed)
{
    if (ed->sel.active) {
        selection_delete(ed);
        return;
    }

    if (ed->cx_col > 0) {
        char *line = ed->doc.lines[ed->cx_line];
        size_t len = ed->doc.lens[ed->cx_line];

        if (ed->config.tabstospaces && ed->config.tabsize > 1) {
            bool all_spaces = true;
            for (size_t i = 0; i < ed->cx_col && i < len; i++) {
                if (line[i] != ' ') {
                    all_spaces = false;
                    break;
                }
            }
            if (all_spaces) {
                size_t tab = (size_t)ed->config.tabsize;
                size_t rm = (ed->cx_col % tab == 0) ? tab : (ed->cx_col % tab);
                if (rm > ed->cx_col) rm = ed->cx_col;
                size_t start_col = ed->cx_col - rm;
                undo_record_remove(ed, ed->cx_line, start_col, line + start_col, rm);
                undo_clear_redo(ed);
                editor_raw_remove(ed, ed->cx_line, start_col, rm);
                ed->cx_col = start_col;
                ed->goal_col = ed->cx_col;
                return;
            }
        }

        size_t from = edit_rune_to_byte(line, len, ed->cx_col - 1);
        size_t to   = edit_rune_to_byte(line, len, ed->cx_col);

        undo_record_remove(ed, ed->cx_line, ed->cx_col - 1, line + from, to - from);
        undo_clear_redo(ed);

        editor_raw_remove(ed, ed->cx_line, ed->cx_col - 1, 1);
        ed->cx_col--;
        ed->goal_col = ed->cx_col;
        return;
    }

    if (ed->cx_line == 0) return;

    size_t prev = ed->cx_line - 1;
    size_t prev_runes = edit_line_runes(ed->doc.lines[prev], ed->doc.lens[prev]);

    undo_record_remove(ed, prev, prev_runes, "\n", 1);
    undo_clear_redo(ed);

    editor_raw_remove(ed, prev, prev_runes, 1);
    ed->cx_line = prev;
    ed->cx_col  = prev_runes;
    ed->goal_col = ed->cx_col;
}

static void delete_word_left_single(editor_t *ed)
{
    if (ed->sel.active) {
        selection_delete(ed);
        return;
    }
    if (ed->cx_col == 0) {
        backspace_single(ed);
        return;
    }

    size_t orig_col = ed->cx_col;
    cursor_word_left_single(ed, false);
    size_t new_col = ed->cx_col;
    if (new_col < orig_col) {
        size_t runes = orig_col - new_col;
        const char *l_str = ed->doc.lines[ed->cx_line];
        size_t l_len = ed->doc.lens[ed->cx_line];
        size_t b_start = edit_rune_to_byte(l_str, l_len, new_col);
        size_t b_end   = edit_rune_to_byte(l_str, l_len, orig_col);

        ed->cx_col = orig_col;
        undo_record_remove(ed, ed->cx_line, new_col,
                           l_str + b_start, b_end - b_start);
        undo_clear_redo(ed);

        editor_raw_remove(ed, ed->cx_line, new_col, runes);
        ed->cx_col = new_col;
    }
}

static void delete_single(editor_t *ed)
{
    if (ed->sel.active) {
        selection_delete(ed);
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
        return;
    }

    if (ed->cx_line + 1 >= ed->doc.count) return;

    undo_record_remove(ed, ed->cx_line, runes, "\n", 1);
    undo_clear_redo(ed);

    editor_raw_remove(ed, ed->cx_line, runes, 1);
}

static void multi_edit_apply(editor_t *ed, mc_op_kind_t op,
                             uint32_t cp, const char *text, size_t len)
{
    size_t total = 1 + ed->extra_count;
    mc_item_t *items = calloc(total, sizeof(*items));
    if (!items) return;

    items[0].c = (cursor_t){ed->cx_line, ed->cx_col, ed->goal_col, ed->sel};
    items[0].is_primary = true;
    for (size_t i = 0; i < ed->extra_count; i++) {
        items[1 + i].c = ed->extra_cursors[i];
        items[1 + i].is_primary = false;
    }
    qsort(items, total, sizeof(*items), mc_item_cmp);

    size_t saved_extra = ed->extra_count;
    ed->extra_count = 0;

    undo_begin_group(ed);

    for (size_t idx = total; idx > 0; idx--) {
        size_t k = idx - 1;
        ed->cx_line  = items[k].c.cx_line;
        ed->cx_col   = items[k].c.cx_col;
        ed->goal_col = items[k].c.goal_col;
        ed->sel      = items[k].c.sel;
        cursor_clamp(ed);

        size_t lines_before = ed->doc.count;
        size_t ref_line = ed->cx_line;
        size_t ref_col = ed->cx_col;
        if (ed->sel.active) {
            loc_t ss, se;
            selection_get_range(ed, &ss, &se);
            ref_line = ss.line;
            ref_col = ss.col;
        }
        size_t ref_line_runes = (ref_line < ed->doc.count)
                                ? edit_line_runes(ed->doc.lines[ref_line], ed->doc.lens[ref_line])
                                : 0;

        switch (op) {
        case MC_OP_CODEPOINT: insert_codepoint_single(ed, cp); break;
        case MC_OP_TEXT:      insert_text_single(ed, text, len); break;
        case MC_OP_NEWLINE:   insert_newline_single(ed); break;
        case MC_OP_BACKSPACE:        backspace_single(ed); break;
        case MC_OP_DELETE:           delete_single(ed); break;
        case MC_OP_DELETE_WORD_LEFT: delete_word_left_single(ed); break;
        }

        items[k].c.cx_line  = ed->cx_line;
        items[k].c.cx_col   = ed->cx_col;
        items[k].c.goal_col = ed->goal_col;
        items[k].c.sel      = ed->sel;

        long d_lines = (long)ed->doc.count - (long)lines_before;
        long d_cols = (d_lines == 0 && ref_line < ed->doc.count)
                      ? ((long)edit_line_runes(ed->doc.lines[ref_line], ed->doc.lens[ref_line]) - (long)ref_line_runes)
                      : 0;

        for (size_t j = k + 1; j < total; j++) {
            if (d_lines > 0) {
                if (items[j].c.cx_line == ref_line) {
                    items[j].c.cx_line += (size_t)d_lines;
                    items[j].c.cx_col = (items[j].c.cx_col >= ref_col)
                                        ? (ed->cx_col + (items[j].c.cx_col - ref_col))
                                        : items[j].c.cx_col;
                    items[j].c.goal_col = items[j].c.cx_col;
                } else if (items[j].c.cx_line > ref_line) {
                    items[j].c.cx_line += (size_t)d_lines;
                }
            } else if (d_lines < 0) {
                size_t removed_lines = (size_t)(-d_lines);
                if (items[j].c.cx_line == ref_line + removed_lines) {
                    items[j].c.cx_line = ref_line;
                    items[j].c.cx_col += ed->cx_col;
                    items[j].c.goal_col = items[j].c.cx_col;
                } else if (items[j].c.cx_line > ref_line + removed_lines) {
                    items[j].c.cx_line -= removed_lines;
                } else if (items[j].c.cx_line > ref_line) {
                    items[j].c.cx_line = ref_line;
                }
            } else if (items[j].c.cx_line == ref_line) {
                long nc = (long)items[j].c.cx_col + d_cols;
                items[j].c.cx_col = nc < 0 ? 0 : (size_t)nc;
                items[j].c.goal_col = items[j].c.cx_col;
            }
        }
    }

    undo_end_group(ed);

    ed->extra_count = saved_extra;
    size_t ex_i = 0;
    for (size_t k = 0; k < total; k++) {
        if (items[k].is_primary) {
            ed->cx_line  = items[k].c.cx_line;
            ed->cx_col   = items[k].c.cx_col;
            ed->goal_col = items[k].c.goal_col;
            ed->sel      = items[k].c.sel;
        } else if (ex_i < ed->extra_count) {
            ed->extra_cursors[ex_i++] = items[k].c;
        }
    }
    free(items);

    cursor_clamp(ed);
    editor_merge_cursors(ed);
    touch(ed);
}

void editor_insert_codepoint(editor_t *ed, uint32_t cp)
{
    if (!check_writable(ed)) return;
    if (ed->extra_count > 0) {
        multi_edit_apply(ed, MC_OP_CODEPOINT, cp, NULL, 0);
        return;
    }
    insert_codepoint_single(ed, cp);
    touch(ed);
}

void editor_insert_text(editor_t *ed, const char *text, size_t len)
{
    if (text == NULL || len == 0 || !check_writable(ed)) return;
    if (ed->extra_count > 0) {
        multi_edit_apply(ed, MC_OP_TEXT, 0, text, len);
        return;
    }
    insert_text_single(ed, text, len);
    touch(ed);
}

void editor_insert_newline(editor_t *ed)
{
    if (!check_writable(ed)) return;
    if (ed->extra_count > 0) {
        multi_edit_apply(ed, MC_OP_NEWLINE, 0, NULL, 0);
        return;
    }
    insert_newline_single(ed);
    touch(ed);
}

void editor_insert_tab(editor_t *ed)
{
    if (!check_writable(ed)) return;
    if (ed->config.tabstospaces && ed->config.tabsize > 0) {
        int spaces = ed->config.tabsize;
        char sp[16];
        if (spaces > 16) spaces = 16;
        memset(sp, ' ', spaces);
        editor_insert_text(ed, sp, (size_t)spaces);
    } else {
        editor_insert_codepoint(ed, '\t');
    }
}

void editor_backspace(editor_t *ed)
{
    if (!check_writable(ed)) return;
    if (ed->extra_count > 0) {
        multi_edit_apply(ed, MC_OP_BACKSPACE, 0, NULL, 0);
        return;
    }
    backspace_single(ed);
    touch(ed);
}

void editor_delete_word_left(editor_t *ed)
{
    if (!check_writable(ed)) return;
    if (ed->extra_count > 0) {
        multi_edit_apply(ed, MC_OP_DELETE_WORD_LEFT, 0, NULL, 0);
        return;
    }
    delete_word_left_single(ed);
    touch(ed);
}

void editor_delete(editor_t *ed)
{
    if (!check_writable(ed)) return;
    if (ed->extra_count > 0) {
        multi_edit_apply(ed, MC_OP_DELETE, 0, NULL, 0);
        return;
    }
    delete_single(ed);
    touch(ed);
}

void editor_delete_line(editor_t *ed)
{
    if (!check_writable(ed) || ed->doc.count == 0) return;

    size_t lstart = ed->cx_line, lend = ed->cx_line;
    if (ed->sel.active) {
        loc_t s, e;
        selection_get_range(ed, &s, &e);
        lstart = s.line;
        lend = e.line;
        if (lend > lstart && e.col == 0) lend--;
    }

    ed->sel.active = false;
    ed->extra_count = 0;
    undo_begin_group(ed);

    if (lstart == 0 && lend + 1 >= ed->doc.count) {
        /* Removing entire document -> leave single empty line */
        for (size_t l = ed->doc.count; l > 0; l--) {
            size_t idx = l - 1;
            if (ed->doc.lens[idx] > 0) {
                size_t runes = edit_line_runes(ed->doc.lines[idx], ed->doc.lens[idx]);
                undo_record_remove(ed, idx, 0, ed->doc.lines[idx], ed->doc.lens[idx]);
                editor_raw_remove(ed, idx, 0, runes);
            }
            if (idx > 0) {
                size_t prev_runes = edit_line_runes(ed->doc.lines[idx - 1], ed->doc.lens[idx - 1]);
                undo_record_remove(ed, idx - 1, prev_runes, "\n", 1);
                editor_raw_remove(ed, idx - 1, prev_runes, 1);
            }
        }
        ed->cx_line = 0;
        ed->cx_col = 0;
    } else if (lend + 1 < ed->doc.count) {
        /* Delete lines lstart..lend including each line's trailing '\n' */
        for (size_t l = lend + 1; l > lstart; l--) {
            size_t idx = l - 1;
            if (ed->doc.lens[idx] > 0) {
                size_t runes = edit_line_runes(ed->doc.lines[idx], ed->doc.lens[idx]);
                undo_record_remove(ed, idx, 0, ed->doc.lines[idx], ed->doc.lens[idx]);
                editor_raw_remove(ed, idx, 0, runes);
            }
            undo_record_remove(ed, idx, 0, "\n", 1);
            editor_raw_remove(ed, idx, 0, 1);
        }
        ed->cx_line = lstart;
        ed->cx_col = 0;
    } else {
        /* lend is last line and lstart > 0: delete lstart..lend and '\n' before lstart */
        for (size_t l = lend + 1; l > lstart; l--) {
            size_t idx = l - 1;
            if (ed->doc.lens[idx] > 0) {
                size_t runes = edit_line_runes(ed->doc.lines[idx], ed->doc.lens[idx]);
                undo_record_remove(ed, idx, 0, ed->doc.lines[idx], ed->doc.lens[idx]);
                editor_raw_remove(ed, idx, 0, runes);
            }
            size_t prev_runes = edit_line_runes(ed->doc.lines[idx - 1], ed->doc.lens[idx - 1]);
            undo_record_remove(ed, idx - 1, prev_runes, "\n", 1);
            editor_raw_remove(ed, idx - 1, prev_runes, 1);
        }
        ed->cx_line = lstart - 1;
    }

    ed->goal_col = ed->cx_col;
    cursor_clamp(ed);
    undo_clear_redo(ed);
    undo_end_group(ed);
    touch(ed);
}

void editor_duplicate(editor_t *ed)
{
    if (!check_writable(ed) || ed->doc.count == 0) return;

    if (ed->sel.active) {
        size_t tlen = 0;
        char *txt = selection_get_text(ed, &tlen);
        if (!txt || tlen == 0) {
            free(txt);
            return;
        }
        loc_t start, end;
        selection_get_range(ed, &start, &end);
        ed->sel.active = false;
        ed->cx_line = end.line;
        ed->cx_col = end.col;
        editor_insert_text(ed, txt, tlen);
        free(txt);
        return;
    }

    /* Duplicate current line below */
    size_t line_idx = ed->cx_line;
    const char *line = ed->doc.lines[line_idx];
    size_t len = ed->doc.lens[line_idx];
    size_t runes = edit_line_runes(line, len);

    char *copy = malloc(len + 2);
    if (!copy) return;
    copy[0] = '\n';
    if (len > 0) memcpy(copy + 1, line, len);
    copy[len + 1] = '\0';

    undo_begin_group(ed);
    undo_record_insert(ed, line_idx, runes, copy, len + 1);
    undo_clear_redo(ed);
    editor_raw_insert(ed, line_idx, runes, copy, len + 1);
    undo_end_group(ed);
    free(copy);

    ed->cx_line++;
    cursor_clamp(ed);
    touch(ed);
}

void editor_move_lines(editor_t *ed, int dir)
{
    if (!check_writable(ed) || ed->doc.count < 2) return;

    size_t lstart = ed->cx_line, lend = ed->cx_line;
    bool had_sel = ed->sel.active;
    loc_t sel_s = {0, 0}, sel_e = {0, 0};
    if (had_sel) {
        selection_get_range(ed, &sel_s, &sel_e);
        lstart = sel_s.line;
        lend = sel_e.line;
        if (lend > lstart && sel_e.col == 0) lend--;
    }

    if (dir < 0 && lstart == 0) return;
    if (dir > 0 && lend + 1 >= ed->doc.count) return;

    size_t swap_idx = (dir < 0) ? (lstart - 1) : (lend + 1);
    size_t target_idx = (dir < 0) ? lend : lstart;

    char *swap_txt = strdup(ed->doc.lines[swap_idx]);
    size_t swap_len = ed->doc.lens[swap_idx];
    if (!swap_txt) return;
    size_t swap_runes = edit_line_runes(swap_txt, swap_len);

    undo_begin_group(ed);
    if (dir < 0) {
        /* Remove line (lstart - 1) + its trailing '\n', then insert '\n' + swap_txt at end of lend */
        if (swap_runes > 0) {
            undo_record_remove(ed, swap_idx, 0, swap_txt, swap_len);
            editor_raw_remove(ed, swap_idx, 0, swap_runes);
        }
        undo_record_remove(ed, swap_idx, 0, "\n", 1);
        editor_raw_remove(ed, swap_idx, 0, 1);

        size_t dest_line = target_idx - 1;
        size_t dest_runes = edit_line_runes(ed->doc.lines[dest_line], ed->doc.lens[dest_line]);
        undo_record_insert(ed, dest_line, dest_runes, "\n", 1);
        editor_raw_insert(ed, dest_line, dest_runes, "\n", 1);
        if (swap_len > 0) {
            undo_record_insert(ed, dest_line + 1, 0, swap_txt, swap_len);
            editor_raw_insert(ed, dest_line + 1, 0, swap_txt, swap_len);
        }
        ed->cx_line--;
        if (had_sel) {
            ed->sel.anchor.line--;
            ed->sel.cursor.line--;
        }
    } else {
        /* Remove line (lend + 1) + '\n' before it, then insert swap_txt + '\n' at start of lstart */
        size_t prev_runes = edit_line_runes(ed->doc.lines[swap_idx - 1], ed->doc.lens[swap_idx - 1]);
        if (swap_runes > 0) {
            undo_record_remove(ed, swap_idx, 0, swap_txt, swap_len);
            editor_raw_remove(ed, swap_idx, 0, swap_runes);
        }
        undo_record_remove(ed, swap_idx - 1, prev_runes, "\n", 1);
        editor_raw_remove(ed, swap_idx - 1, prev_runes, 1);

        undo_record_insert(ed, target_idx, 0, "\n", 1);
        editor_raw_insert(ed, target_idx, 0, "\n", 1);
        if (swap_len > 0) {
            undo_record_insert(ed, target_idx, 0, swap_txt, swap_len);
            editor_raw_insert(ed, target_idx, 0, swap_txt, swap_len);
        }
        ed->cx_line++;
        if (had_sel) {
            ed->sel.anchor.line++;
            ed->sel.cursor.line++;
        }
    }
    free(swap_txt);
    undo_clear_redo(ed);
    undo_end_group(ed);
    cursor_clamp(ed);
    touch(ed);
}

static bool add_extra_cursor(editor_t *ed, cursor_t c)
{
    if (ed->extra_count >= ed->extra_cap) {
        size_t ncap = ed->extra_cap ? ed->extra_cap * 2 : 8;
        cursor_t *nc = realloc(ed->extra_cursors, ncap * sizeof(*nc));
        if (!nc) return false;
        ed->extra_cursors = nc;
        ed->extra_cap = ncap;
    }
    ed->extra_cursors[ed->extra_count++] = c;
    return true;
}

void editor_merge_cursors(editor_t *ed)
{
    if (!ed || ed->extra_count == 0) return;
    size_t write_idx = 0;
    for (size_t i = 0; i < ed->extra_count; i++) {
        cursor_t c = ed->extra_cursors[i];
        bool dup = (c.cx_line == ed->cx_line && c.cx_col == ed->cx_col);
        for (size_t j = 0; !dup && j < write_idx; j++) {
            if (ed->extra_cursors[j].cx_line == c.cx_line &&
                ed->extra_cursors[j].cx_col == c.cx_col) {
                dup = true;
            }
        }
        if (!dup) {
            ed->extra_cursors[write_idx++] = c;
        }
    }
    ed->extra_count = write_idx;
}

void editor_spawn_cursor_dir(editor_t *ed, int dir)
{
    if (!ed || ed->doc.count == 0 || dir == 0) return;

    size_t ref_line = ed->cx_line;
    size_t goal = ed->cx_col;
    for (size_t i = 0; i < ed->extra_count; i++) {
        if ((dir > 0 && ed->extra_cursors[i].cx_line > ref_line) ||
            (dir < 0 && ed->extra_cursors[i].cx_line < ref_line)) {
            ref_line = ed->extra_cursors[i].cx_line;
            goal = ed->extra_cursors[i].goal_col;
        }
    }

    if (dir < 0 && ref_line == 0) return;
    if (dir > 0 && ref_line + 1 >= ed->doc.count) return;

    size_t nl = (dir < 0) ? (ref_line - 1) : (ref_line + 1);
    size_t runes = edit_line_runes(ed->doc.lines[nl], ed->doc.lens[nl]);
    size_t nc = goal <= runes ? goal : runes;

    cursor_t c = {nl, nc, goal, {{0, 0}, {0, 0}, false}};
    add_extra_cursor(ed, c);
    editor_merge_cursors(ed);
    editor_sync_to_active(ed);
}

static bool word_bounds_at(const char *line, size_t len, size_t col,
                           size_t *out_sc, size_t *out_ec)
{
    size_t runes = edit_line_runes(line, len);
    if (runes == 0) return false;
    if (col >= runes) col = runes - 1;

    uint32_t cp = 0;
    size_t b = edit_rune_to_byte(line, len, col);
    tam_utf8_decode(line + b, len - b, &cp);
    if (cp == ' ' || cp == '\t') {
        if (col > 0) {
            col--;
            b = edit_rune_to_byte(line, len, col);
            tam_utf8_decode(line + b, len - b, &cp);
        }
        if (cp == ' ' || cp == '\t') return false;
    }

    size_t sc = col, ec = col + 1;
    while (sc > 0) {
        size_t pb = edit_rune_to_byte(line, len, sc - 1);
        uint32_t pcp = 0;
        tam_utf8_decode(line + pb, len - pb, &pcp);
        if (!(isalnum((unsigned char)pcp) || pcp == '_' || pcp >= 0x80)) break;
        sc--;
    }
    while (ec < runes) {
        size_t nb = edit_rune_to_byte(line, len, ec);
        uint32_t ncp = 0;
        tam_utf8_decode(line + nb, len - nb, &ncp);
        if (!(isalnum((unsigned char)ncp) || ncp == '_' || ncp >= 0x80)) break;
        ec++;
    }
    *out_sc = sc;
    *out_ec = ec;
    return ec > sc;
}

void editor_spawn_cursor_match(editor_t *ed, bool skip_current)
{
    if (!ed || ed->doc.count == 0) return;

    if (!ed->sel.active) {
        size_t sc = 0, ec = 0;
        if (word_bounds_at(ed->doc.lines[ed->cx_line], ed->doc.lens[ed->cx_line],
                           ed->cx_col, &sc, &ec)) {
            ed->sel.active = true;
            ed->sel.anchor = (loc_t){ed->cx_line, sc};
            ed->sel.cursor = (loc_t){ed->cx_line, ec};
            ed->cx_col = ec;
            ed->goal_col = ec;
            editor_sync_to_active(ed);
        }
        return;
    }

    size_t tlen = 0;
    char *needle = selection_get_text(ed, &tlen);
    if (!needle || tlen == 0 || memchr(needle, '\n', tlen) != NULL) {
        free(needle);
        return;
    }

    size_t search_line = ed->cx_line;
    size_t search_col  = ed->cx_col;
    bool had_extra = (ed->extra_count > 0);
    if (had_extra) {
        cursor_t *last = &ed->extra_cursors[ed->extra_count - 1];
        search_line = last->cx_line;
        search_col  = last->cx_col;
        if (skip_current) {
            ed->extra_count--;
        }
    } else if (skip_current) {
        /* Single cursor skip -> move primary to next occurrence */
        loc_t s, e;
        selection_get_range(ed, &s, &e);
        search_line = e.line;
        search_col = e.col;
    }

    size_t found_line = 0, found_sc = 0, found_ec = 0;
    bool found = false;
    size_t nrunes = edit_line_runes(needle, tlen);

    for (size_t step = 0; step < ed->doc.count && !found; step++) {
        size_t l = (search_line + step) % ed->doc.count;
        const char *line = ed->doc.lines[l];
        size_t len = ed->doc.lens[l];
        size_t start_byte = (step == 0) ? edit_rune_to_byte(line, len, search_col) : 0;
        if (start_byte + tlen <= len) {
            for (size_t b = start_byte; b + tlen <= len; b++) {
                if (memcmp(line + b, needle, tlen) == 0) {
                    found_line = l;
                    found_sc = edit_line_runes(line, b);
                    found_ec = found_sc + nrunes;
                    found = true;
                    break;
                }
            }
        }
    }
    free(needle);

    if (!found) return;

    /* Avoid duplicating existing cursor */
    if (found_line == ed->cx_line && found_ec == ed->cx_col && !skip_current) return;
    for (size_t i = 0; i < ed->extra_count; i++) {
        if (ed->extra_cursors[i].cx_line == found_line &&
            ed->extra_cursors[i].cx_col == found_ec) {
            return;
        }
    }

    cursor_t nc = {
        .cx_line = found_line,
        .cx_col = found_ec,
        .goal_col = found_ec,
        .sel = { .active = true, .anchor = {found_line, found_sc}, .cursor = {found_line, found_ec} }
    };

    if (skip_current && !had_extra) {
        ed->cx_line = nc.cx_line;
        ed->cx_col = nc.cx_col;
        ed->goal_col = nc.goal_col;
        ed->sel = nc.sel;
    } else {
        add_extra_cursor(ed, nc);
    }
    editor_sync_to_active(ed);
}

void editor_spawn_cursors_on_selection(editor_t *ed)
{
    if (!ed || !ed->sel.active) return;
    loc_t s, e;
    selection_get_range(ed, &s, &e);
    if (s.line == e.line) return;

    size_t first_col = s.col;
    ed->extra_count = 0;
    ed->sel.active = false;
    ed->cx_line = s.line;
    ed->cx_col = first_col;
    ed->goal_col = first_col;

    size_t last_line = e.line;
    if (last_line > s.line && e.col == 0) last_line--;

    for (size_t l = s.line + 1; l <= last_line && l < ed->doc.count; l++) {
        size_t runes = edit_line_runes(ed->doc.lines[l], ed->doc.lens[l]);
        size_t c = first_col <= runes ? first_col : runes;
        cursor_t nc = {l, c, first_col, {{0, 0}, {0, 0}, false}};
        add_extra_cursor(ed, nc);
    }
    editor_sync_to_active(ed);
}

void editor_remove_last_cursor(editor_t *ed)
{
    if (!ed || ed->extra_count == 0) return;
    ed->extra_count--;
    editor_sync_to_active(ed);
}

void editor_remove_all_cursors(editor_t *ed)
{
    if (!ed || ed->extra_count == 0) return;
    ed->extra_count = 0;
    editor_sync_to_active(ed);
}

void editor_toggle_cursor_at(editor_t *ed, size_t line, size_t col)
{
    if (!ed || ed->doc.count == 0) return;
    if (line >= ed->doc.count) line = ed->doc.count - 1;
    size_t runes = edit_line_runes(ed->doc.lines[line], ed->doc.lens[line]);
    if (col > runes) col = runes;

    for (size_t i = 0; i < ed->extra_count; i++) {
        if (ed->extra_cursors[i].cx_line == line && ed->extra_cursors[i].cx_col == col) {
            memmove(ed->extra_cursors + i, ed->extra_cursors + i + 1,
                    (ed->extra_count - i - 1) * sizeof(cursor_t));
            ed->extra_count--;
            editor_sync_to_active(ed);
            return;
        }
    }
    if (line == ed->cx_line && col == ed->cx_col) return;

    cursor_t nc = {line, col, col, {{0, 0}, {0, 0}, false}};
    add_extra_cursor(ed, nc);
    editor_sync_to_active(ed);
}

bool editor_undo(editor_t *ed)
{
    bool ok = undo_perform(ed);
    if (ok) {
        ed->sel.active = false;
        ed->extra_count = 0;
        touch(ed);
    }
    return ok;
}

bool editor_redo(editor_t *ed)
{
    bool ok = redo_perform(ed);
    if (ok) {
        ed->sel.active = false;
        ed->extra_count = 0;
        touch(ed);
    }
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
