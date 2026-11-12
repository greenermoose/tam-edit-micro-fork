#include "editor.h"
#include "cursor.h"
#include "undo.h"

#include <tam.h>
#include <tam_uni.h>

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

static void doc_clear(edit_doc_t *d)
{
    if (d == NULL) return;
    for (size_t i = 0; i < d->count; i++) free(d->lines[i]);
    free(d->lines);
    free(d->lens);
    memset(d, 0, sizeof(*d));
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

static bool doc_from_text(edit_doc_t *d, const char *data, size_t len,
                          bool *crlf_out)
{
    bool crlf = false;
    for (size_t i = 0; i + 1 < len; i++) {
        if (data[i] == '\n') {
            crlf = i > 0 && data[i - 1] == '\r';
            break;
        }
    }

    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i != len && data[i] != '\n') continue;

        size_t end = i;
        if (crlf && end > start && data[end - 1] == '\r') end--;

        if (!doc_insert_line(d, d->count, data + start, end - start)) {
            return false;
        }
        start = i + 1;
    }

    if (crlf_out != NULL) *crlf_out = crlf;
    return true;
}

static bool doc_join_text(edit_doc_t *d, bool crlf, tam_str_t *out)
{
    const char *sep = crlf ? "\r\n" : "\n";
    size_t sep_len = strlen(sep);

    for (size_t i = 0; i < d->count; i++) {
        if (i > 0) {
            *out = tam_str_cat_len(*out, sep, sep_len);
            if (*out == NULL) return false;
        }
        if (d->lens[i] > 0) {
            *out = tam_str_cat_len(*out, d->lines[i], d->lens[i]);
            if (*out == NULL) return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* Low-level Raw Operations for Undo/Redo Engine                              */
/* -------------------------------------------------------------------------- */

void editor_raw_insert(editor_t *ed, size_t line, size_t col,
                       const char *text, size_t len)
{
    if (line >= ed->doc.count) {
        if (!doc_insert_line(&ed->doc, line, "", 0)) return;
    }

    char *cur = ed->doc.lines[line];
    size_t cur_len = ed->doc.lens[line];
    size_t at = edit_rune_to_byte(cur, cur_len, col);

    if (len == 1 && text[0] == '\n') {
        doc_insert_line(&ed->doc, line + 1, cur + at, cur_len - at);
        line_replace(&ed->doc.lines[line], &ed->doc.lens[line], at, cur_len - at, "", 0);
        return;
    }

    /* Check if text contains any newlines */
    const char *nl = memchr(text, '\n', len);
    if (nl == NULL) {
        line_replace(&ed->doc.lines[line], &ed->doc.lens[line], at, 0, text, len);
    } else {
        /* Multi-line insertion */
        size_t first_part_len = nl - text;
        size_t rest_offset    = first_part_len + 1;
        size_t tail_len       = cur_len - at;
        char *tail = malloc(tail_len + 1);
        if (tail != NULL) {
            memcpy(tail, cur + at, tail_len);
            tail[tail_len] = '\0';
        }

        /* 1. Truncate current line at 'at' and append first part */
        line_replace(&ed->doc.lines[line], &ed->doc.lens[line], at, cur_len - at, text, first_part_len);

        /* 2. Process subsequent lines */
        size_t cur_l = line + 1;
        const char *p = text + rest_offset;
        size_t rem = len - rest_offset;

        while (rem > 0) {
            const char *next_nl = memchr(p, '\n', rem);
            if (next_nl == NULL) {
                /* Last part: append tail */
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
}

void editor_raw_remove(editor_t *ed, size_t line, size_t col, size_t runes)
{
    if (line >= ed->doc.count) return;

    char *cur = ed->doc.lines[line];
    size_t cur_len = ed->doc.lens[line];
    size_t line_runes = edit_line_runes(cur, cur_len);

    if (col == line_runes && runes == 1 && line + 1 < ed->doc.count) {
        /* Join line with line + 1 */
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

void editor_raw_remove_str(editor_t *ed, size_t line, size_t col, const char *text, size_t len)
{
    if (line >= ed->doc.count || text == NULL || len == 0) return;

    size_t nl_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') nl_count++;
    }

    if (nl_count == 0) {
        size_t runes = edit_line_runes(text, len);
        editor_raw_remove(ed, line, col, runes);
        return;
    }

    /* 1. Remove first segment from line */
    const char *first_nl = memchr(text, '\n', len);
    size_t first_len = first_nl - text;
    size_t first_runes = edit_line_runes(text, first_len);
    if (first_runes > 0) {
        editor_raw_remove(ed, line, col, first_runes);
    }

    /* 2. Remove intermediate lines */
    for (size_t i = 1; i < nl_count; i++) {
        if (line + 1 < ed->doc.count) {
            doc_remove_line(&ed->doc, line + 1);
        }
    }

    /* 3. Remove last segment from line + 1 */
    const char *last_nl = memrchr(text, '\n', len);
    size_t last_len = (text + len) - (last_nl + 1);
    size_t last_runes = edit_line_runes(last_nl + 1, last_len);
    if (line + 1 < ed->doc.count) {
        if (last_runes > 0) {
            editor_raw_remove(ed, line + 1, 0, last_runes);
        }
        /* 4. Join line + 1 onto line */
        size_t cur_runes = edit_line_runes(ed->doc.lines[line], ed->doc.lens[line]);
        editor_raw_remove(ed, line, cur_runes, 1);
    }
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

static void touch(editor_t *ed)
{
    ed->dirty = true;
    ed->message[0] = '\0';
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

void editor_init(editor_t *ed)
{
    memset(ed, 0, sizeof(*ed));
    ed->running = true;
    ed->tty_out_fd = -1;
    doc_insert_line(&ed->doc, 0, "", 0);
    undo_init(ed);
}

void editor_free(editor_t *ed)
{
    if (ed == NULL) return;
    doc_clear(&ed->doc);
    undo_free(ed);
    free(ed->path);
    ed->path = NULL;
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
    size_t len = 0;
    char *data = tam_fs_read_file(path, &len);

    if (data == NULL) {
        if (!tam_fs_file_exists(path)) {
            free(ed->path);
            ed->path = strdup(path);
            ed->dirty = false;
            return true;
        }
        editor_set_message(ed, "Could not read %s", path);
        return false;
    }

    doc_clear(&ed->doc);
    undo_free(ed);
    undo_init(ed);

    const char *text = data;
    size_t text_len = len;
    if (text_len >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text += 3;
        text_len -= 3;
    }

    bool crlf = false;
    bool ok = doc_from_text(&ed->doc, text, text_len, &crlf);
    free(data);

    if (!ok || ed->doc.count == 0) {
        if (ed->doc.count == 0) doc_insert_line(&ed->doc, 0, "", 0);
        editor_set_message(ed, "Could not load %s", path);
        return false;
    }

    free(ed->path);
    ed->path = strdup(path);
    ed->crlf = crlf;
    ed->dirty = false;
    ed->cx_line = 0;
    ed->cx_col = 0;
    ed->goal_col = 0;
    ed->top_line = 0;
    ed->left_col = 0;
    ed->sel.active = false;
    ed->message[0] = '\0';
    return true;
}

bool editor_save_to(editor_t *ed, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        editor_set_message(ed, "No file name");
        return false;
    }

    tam_str_t text = tam_str_new("");
    if (text == NULL) {
        editor_set_message(ed, "Out of memory");
        return false;
    }

    if (!doc_join_text(&ed->doc, ed->crlf, &text)) {
        tam_str_free(text);
        editor_set_message(ed, "Out of memory");
        return false;
    }

    mode_t mode = 0644;
    struct stat st;
    if (stat(path, &st) == 0) mode = st.st_mode & 07777;

    bool ok = tam_fs_write_atomic(path, text, tam_str_len(text), mode);
    size_t written = tam_str_len(text);
    tam_str_free(text);

    if (!ok) {
        editor_set_message(ed, "Could not save %s", path);
        return false;
    }

    if (ed->path == NULL || strcmp(ed->path, path) != 0) {
        free(ed->path);
        ed->path = strdup(path);
    }
    ed->dirty = false;
    editor_set_message(ed, "Saved %zu bytes to %s", written, path);
    return true;
}

bool editor_save(editor_t *ed)
{
    return editor_save_to(ed, ed->path);
}

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

    /* Update cursor after inserted text */
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
    editor_insert_codepoint(ed, '\t');
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
    return undo_perform(ed);
}

bool editor_redo(editor_t *ed)
{
    return redo_perform(ed);
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
