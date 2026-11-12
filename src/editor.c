#include "editor.h"

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

/* Replace [at, at + remove) with ins in one line. */
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

/* Split data into lines on '\n'. A trailing newline yields a final empty
 * line, so the model round-trips the file exactly. */
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
/* Cursor helpers                                                             */
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

static size_t cur_runes(const editor_t *ed)
{
    if (ed->doc.count == 0) return 0;
    return edit_line_runes(ed->doc.lines[ed->cx_line],
                           ed->doc.lens[ed->cx_line]);
}

static void clamp_cursor(editor_t *ed)
{
    if (ed->doc.count == 0) return;
    if (ed->cx_line >= ed->doc.count) ed->cx_line = ed->doc.count - 1;

    size_t runes = cur_runes(ed);
    if (ed->cx_col > runes) ed->cx_col = runes;
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
    doc_insert_line(&ed->doc, 0, "", 0);
}

void editor_free(editor_t *ed)
{
    if (ed == NULL) return;
    doc_clear(&ed->doc);
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
            /* New file: the empty document from editor_init is correct. */
            free(ed->path);
            ed->path = strdup(path);
            ed->dirty = false;
            return true;
        }
        editor_set_message(ed, "Could not read %s", path);
        return false;
    }

    doc_clear(&ed->doc);

    /* A UTF-8 BOM is metadata, not text. */
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
    ed->message[0] = '\0';
    return true;
}

bool editor_save(editor_t *ed)
{
    if (ed->path == NULL) {
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
    if (stat(ed->path, &st) == 0) mode = st.st_mode & 07777;

    bool ok = tam_fs_write_atomic(ed->path, text, tam_str_len(text), mode);
    size_t written = tam_str_len(text);
    tam_str_free(text);

    if (!ok) {
        editor_set_message(ed, "Could not save %s", ed->path);
        return false;
    }

    ed->dirty = false;
    editor_set_message(ed, "Saved %zu bytes to %s", written, ed->path);
    return true;
}

void editor_insert_codepoint(editor_t *ed, uint32_t cp)
{
    char utf8[4];
    size_t n = tam_utf8_encode(cp, utf8);

    char *line = ed->doc.lines[ed->cx_line];
    size_t len = ed->doc.lens[ed->cx_line];
    size_t at = edit_rune_to_byte(line, len, ed->cx_col);

    if (!line_replace(&line, &len, at, 0, utf8, n)) return;
    ed->doc.lines[ed->cx_line] = line;
    ed->doc.lens[ed->cx_line] = len;
    ed->cx_col++;
    ed->goal_col = ed->cx_col;
    touch(ed);
}

void editor_insert_newline(editor_t *ed)
{
    char *line = ed->doc.lines[ed->cx_line];
    size_t len = ed->doc.lens[ed->cx_line];
    size_t at = edit_rune_to_byte(line, len, ed->cx_col);

    if (!doc_insert_line(&ed->doc, ed->cx_line + 1, line + at, len - at)) {
        return;
    }

    if (!line_replace(&line, &len, at, len - at, "", 0)) {
        doc_remove_line(&ed->doc, ed->cx_line + 1);
        return;
    }

    ed->doc.lines[ed->cx_line] = line;
    ed->doc.lens[ed->cx_line] = len;

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
    if (ed->cx_col > 0) {
        char *line = ed->doc.lines[ed->cx_line];
        size_t len = ed->doc.lens[ed->cx_line];
        size_t from = edit_rune_to_byte(line, len, ed->cx_col - 1);
        size_t to = edit_rune_to_byte(line, len, ed->cx_col);
        if (!line_replace(&line, &len, from, to - from, "", 0)) return;
        ed->doc.lines[ed->cx_line] = line;
        ed->doc.lens[ed->cx_line] = len;
        ed->cx_col--;
        ed->goal_col = ed->cx_col;
        touch(ed);
        return;
    }

    if (ed->cx_line == 0) return;

    size_t prev = ed->cx_line - 1;
    size_t prev_runes = edit_line_runes(ed->doc.lines[prev],
                                        ed->doc.lens[prev]);
    char *line = ed->doc.lines[prev];
    size_t len = ed->doc.lens[prev];

    if (!line_replace(&line, &len, len, 0,
                      ed->doc.lines[ed->cx_line],
                      ed->doc.lens[ed->cx_line])) {
        return;
    }

    ed->doc.lines[prev] = line;
    ed->doc.lens[prev] = len;
    doc_remove_line(&ed->doc, ed->cx_line);

    ed->cx_line = prev;
    ed->cx_col = prev_runes;
    ed->goal_col = ed->cx_col;
    touch(ed);
}

void editor_delete(editor_t *ed)
{
    size_t runes = cur_runes(ed);

    if (ed->cx_col < runes) {
        char *line = ed->doc.lines[ed->cx_line];
        size_t len = ed->doc.lens[ed->cx_line];
        size_t from = edit_rune_to_byte(line, len, ed->cx_col);
        size_t to = edit_rune_to_byte(line, len, ed->cx_col + 1);
        if (!line_replace(&line, &len, from, to - from, "", 0)) return;
        ed->doc.lines[ed->cx_line] = line;
        ed->doc.lens[ed->cx_line] = len;
        touch(ed);
        return;
    }

    if (ed->cx_line + 1 >= ed->doc.count) return;

    char *line = ed->doc.lines[ed->cx_line];
    size_t len = ed->doc.lens[ed->cx_line];
    size_t next = ed->cx_line + 1;

    if (!line_replace(&line, &len, len, 0,
                      ed->doc.lines[next], ed->doc.lens[next])) {
        return;
    }
    ed->doc.lines[ed->cx_line] = line;
    ed->doc.lens[ed->cx_line] = len;
    doc_remove_line(&ed->doc, next);
    touch(ed);
}

void editor_move(editor_t *ed, long dx, long dy)
{
    if (dy != 0) {
        long line = (long)ed->cx_line + dy;
        if (line < 0) line = 0;
        if (line >= (long)ed->doc.count) line = (long)ed->doc.count - 1;
        ed->cx_line = (size_t)line;
        ed->cx_col = ed->goal_col;
        clamp_cursor(ed);
    }

    if (dx != 0) {
        long col = (long)ed->cx_col + dx;
        if (col < 0) col = 0;
        size_t runes = cur_runes(ed);
        if (col > (long)runes) col = (long)runes;
        ed->cx_col = (size_t)col;
        ed->goal_col = ed->cx_col;
    }
}

void editor_home(editor_t *ed)
{
    ed->cx_col = 0;
    ed->goal_col = 0;
}

void editor_end(editor_t *ed)
{
    ed->cx_col = cur_runes(ed);
    ed->goal_col = ed->cx_col;
}

void editor_page(editor_t *ed, long pages, int text_rows)
{
    if (text_rows < 1) text_rows = 1;
    editor_move(ed, 0, pages * text_rows);
}
