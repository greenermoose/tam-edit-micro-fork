#include "render.h"

#include <tam.h>
#include <tam_uni.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cat_utf8(tam_str_t *s, uint32_t cp)
{
    char buf[4];
    size_t n = tam_utf8_encode(cp, buf);
    *s = tam_str_cat_len(*s, buf, n);
}

static void cat_spaces(tam_str_t *s, int count)
{
    for (int i = 0; i < count; i++) *s = tam_str_cat(*s, " ");
}

/* Append the visible part of one line, starting at display column left_col
 * and stopping after max_cells cells. Tabs expand to the next tab stop. */
static void cat_line(tam_str_t *s, const char *line, size_t len,
                     int left_col, int max_cells)
{
    int limit = left_col + max_cells;
    int visual = 0;
    size_t i = 0;

    while (i < len && visual < limit) {
        uint32_t cp;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        if (used == 0) break;

        int width;
        bool control = false;
        if (cp == '\t') {
            width = EDIT_TABSTOP - (visual % EDIT_TABSTOP);
        } else if (cp < 0x20 || cp == 0x7F) {
            /* Control bytes are shown as a single replacement cell. */
            width = 1;
            control = true;
        } else {
            width = tam_uni_width(cp);
        }

        int next = visual + width;

        if (width == 0) {
            /* Combining marks join the preceding cell when it is visible. */
            if (visual >= left_col && visual < limit) cat_utf8(s, cp);
        } else if (next > left_col && visual < limit) {
            int skip = visual < left_col ? left_col - visual : 0;
            int emit = width - skip;
            if (visual + skip + emit > limit) emit = limit - (visual + skip);

            if (cp == '\t' || control) {
                cat_spaces(s, emit);
            } else if (skip == 0) {
                *s = tam_str_cat_len(*s, line + i, used);
            } else {
                /* A wide character cut by horizontal scroll: keep the
                 * alignment with spaces rather than half a glyph. */
                cat_spaces(s, emit);
            }
        }

        visual = next;
        i += used;
    }
}

static int cat_fit(tam_str_t *s, const char *text, int cells)
{
    if (cells <= 0 || text == NULL) return 0;

    size_t len = strlen(text);
    char *buf = malloc(len + 1);
    if (buf == NULL) return 0;

    int used = tam_uni_fit(text, len, cells, buf, len + 1);
    *s = tam_str_cat(*s, buf);
    free(buf);
    return used;
}

int render_text_rows(const tam_tty_t *tty)
{
    int rows = tty->rows > 0 ? tty->rows : 24;
    int text_rows = rows - 2;
    return text_rows > 0 ? text_rows : 1;
}

void render_draw(editor_t *ed, tam_tty_t *tty)
{
    int rows = tty->rows;
    int cols = tty->cols;
    if (rows < 3) rows = 3;
    if (cols < 2) cols = 2;

    int text_rows = rows - 2;

    /* Vertical scroll to the cursor. */
    if (ed->cx_line < ed->top_line) ed->top_line = ed->cx_line;
    if (ed->cx_line >= ed->top_line + (size_t)text_rows) {
        ed->top_line = ed->cx_line - (size_t)text_rows + 1;
    }

    /* Horizontal scroll to the cursor. */
    int cursor_cells = 0;
    if (ed->cx_line < ed->doc.count) {
        cursor_cells = edit_visual_col(ed->doc.lines[ed->cx_line],
                                       ed->doc.lens[ed->cx_line],
                                       ed->cx_col);
    }
    if (cursor_cells < (int)ed->left_col) ed->left_col = (size_t)cursor_cells;
    if (cursor_cells >= (int)ed->left_col + cols) {
        ed->left_col = (size_t)(cursor_cells - cols + 1);
    }

    tam_str_t frame = tam_str_new("");
    if (frame == NULL) return;

    frame = tam_str_cat(frame, "\x1b[?25l\x1b[H");

    for (int r = 0; r < text_rows; r++) {
        size_t li = ed->top_line + (size_t)r;
        if (li < ed->doc.count) {
            cat_line(&frame, ed->doc.lines[li], ed->doc.lens[li],
                     (int)ed->left_col, cols);
        }
        frame = tam_str_cat(frame, "\x1b[K");
        if (r + 1 < text_rows) frame = tam_str_cat(frame, "\r\n");
    }

    /* Message line. */
    frame = tam_str_cat(frame, "\r\n");
    if (ed->message[0] != '\0') {
        frame = tam_str_cat(frame, " ");
        cat_fit(&frame, ed->message, cols - 1);
    }
    frame = tam_str_cat(frame, "\x1b[K");

    /* Status line. */
    const char *name = ed->path != NULL ? ed->path : "[No Name]";
    char name_buf[1024];
    snprintf(name_buf, sizeof(name_buf), " %s%s", name,
             ed->dirty ? " [+]" : "");

    char right[160];
    if (cols >= 64) {
        snprintf(right, sizeof(right),
                 "%zu:%zu  %zu lines  Ctrl-S save  Ctrl-Q quit",
                 ed->cx_line + 1, ed->cx_col + 1, ed->doc.count);
    } else {
        snprintf(right, sizeof(right), "%zu:%zu  %zu lines",
                 ed->cx_line + 1, ed->cx_col + 1, ed->doc.count);
    }

    int right_w = tam_uni_str_width(right, strlen(right));
    int name_cells = cols - right_w - 1;
    if (name_cells < 8) {
        /* Narrow terminal: keep the position and the file name. */
        snprintf(right, sizeof(right), "%zu:%zu",
                 ed->cx_line + 1, ed->cx_col + 1);
        right_w = tam_uni_str_width(right, strlen(right));
        name_cells = cols - right_w - 1;
    }
    if (name_cells < 0) name_cells = 0;

    int used = 0;

    tam_str_t status = tam_str_new("");
    if (status == NULL) {
        tam_str_free(frame);
        return;
    }

    used += cat_fit(&status, name_buf, name_cells);

    int pad = cols - right_w - used;
    if (pad > 0) cat_spaces(&status, pad);
    used += pad > 0 ? pad : 0;

    if (right_w <= cols) {
        status = tam_str_cat(status, right);
        used += right_w;
    }
    if (used < cols) cat_spaces(&status, cols - used);

    frame = tam_str_cat(frame, "\r\n");
    if (tty->colors > 0) frame = tam_str_cat(frame, "\x1b[7m");
    frame = tam_str_cat_len(frame, status, tam_str_len(status));
    if (tty->colors > 0) frame = tam_str_cat(frame, "\x1b[0m");
    frame = tam_str_cat(frame, "\x1b[K");

    /* Place the cursor inside the text area. */
    int crow = (int)(ed->cx_line - ed->top_line) + 1;
    int ccol = cursor_cells - (int)ed->left_col + 1;
    if (crow < 1) crow = 1;
    if (crow > text_rows) crow = text_rows;
    if (ccol < 1) ccol = 1;
    if (ccol > cols) ccol = cols;
    frame = tam_str_cat_printf(frame, "\x1b[%d;%dH\x1b[?25h", crow, ccol);

    tam_tty_frame_begin(tty);
    tam_tty_write(tty, frame, tam_str_len(frame));
    tam_tty_frame_end(tty);

    tam_str_free(status);
    tam_str_free(frame);
}
