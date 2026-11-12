#include "render.h"
#include "cursor.h"

#include <tam.h>
#include <tam_uni.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void grid_init(grid_t *g)
{
    g->cells = NULL;
    g->rows  = 0;
    g->cols  = 0;
}

static void grid_free(grid_t *g)
{
    free(g->cells);
    g->cells = NULL;
    g->rows  = 0;
    g->cols  = 0;
}

static bool grid_resize(grid_t *g, int rows, int cols)
{
    if (g->rows == rows && g->cols == cols && g->cells != NULL) return true;

    cell_t *new_cells = calloc((size_t)rows * (size_t)cols, sizeof(cell_t));
    if (new_cells == NULL) return false;

    for (int i = 0; i < rows * cols; i++) {
        new_cells[i].codepoint = ' ';
        new_cells[i].attr      = ATTR_NONE;
        new_cells[i].width     = 1;
    }

    free(g->cells);
    g->cells = new_cells;
    g->rows  = rows;
    g->cols  = cols;
    return true;
}

void renderer_init(renderer_t *ren)
{
    grid_init(&ren->front);
    grid_init(&ren->back);
    ren->force_redraw = true;
}

void renderer_free(renderer_t *ren)
{
    grid_free(&ren->front);
    grid_free(&ren->back);
}

void renderer_resize(renderer_t *ren, int rows, int cols)
{
    if (rows < 3) rows = 3;
    if (cols < 2) cols = 2;
    grid_resize(&ren->front, rows, cols);
    grid_resize(&ren->back, rows, cols);
    ren->force_redraw = true;
}

static void set_cell(grid_t *g, int r, int c, uint32_t cp, uint16_t attr, uint8_t w)
{
    if (r < 0 || r >= g->rows || c < 0 || c >= g->cols) return;
    int idx = r * g->cols + c;
    g->cells[idx].codepoint = cp;
    g->cells[idx].attr      = attr;
    g->cells[idx].width     = w;
}

static bool in_selection(const editor_t *ed, size_t line, size_t rune_col)
{
    loc_t start, end;
    if (!selection_get_range(ed, &start, &end)) return false;
    if (line < start.line || line > end.line) return false;
    if (line == start.line && rune_col < start.col) return false;
    if (line == end.line && rune_col >= end.col) return false;
    return true;
}

int render_text_rows(const tam_tty_t *tty)
{
    int rows = tty->rows > 0 ? tty->rows : 24;
    int text_rows = rows - 2;
    return text_rows > 0 ? text_rows : 1;
}

static void render_paint_line(editor_t *ed, grid_t *g, size_t line_idx,
                              int screen_row, int left_col)
{
    const char *line = ed->doc.lines[line_idx];
    size_t len = ed->doc.lens[line_idx];

    int visual = 0;
    size_t i = 0;
    size_t rune_col = 0;

    while (i < len && visual < left_col + g->cols) {
        uint32_t cp = 0;
        size_t used = tam_utf8_decode(line + i, len - i, &cp);
        if (used == 0) break;

        int width = 0;
        if (cp == '\t') {
            width = EDIT_TABSTOP - (visual % EDIT_TABSTOP);
        } else if (cp < 0x20 || cp == 0x7F) {
            width = 1;
        } else {
            width = tam_uni_width(cp);
        }

        uint16_t attr = ATTR_NONE;
        if (in_selection(ed, line_idx, rune_col)) {
            attr |= ATTR_REVERSE;
        }

        if (width == 0) {
            /* Combining characters attach to previous visible cell if in bounds */
            int prev_c = visual - 1 - left_col;
            if (prev_c >= 0 && prev_c < g->cols) {
                /* For Milestone 1, combining marks can set codepoint or attribute */
            }
        } else {
            for (int w = 0; w < width; w++) {
                int sc = visual + w - left_col;
                if (sc >= 0 && sc < g->cols) {
                    uint32_t out_cp = (cp == '\t' || cp < 0x20 || cp == 0x7F)
                                          ? ' '
                                          : (w == 0 ? cp : ' ');
                    uint8_t cell_w = (width == 2 && w == 1) ? 0 : (uint8_t)width;
                    set_cell(g, screen_row, sc, out_cp, attr, cell_w);
                }
            }
        }

        visual += width;
        i += used;
        rune_col++;
    }

    /* Trailing newline selection: if whole line is selected up to next line */
    if (in_selection(ed, line_idx, rune_col)) {
        int sc = visual - left_col;
        if (sc >= 0 && sc < g->cols) {
            set_cell(g, screen_row, sc, ' ', ATTR_REVERSE, 1);
        }
    }
}

static void render_paint_status(editor_t *ed, grid_t *g, int row)
{
    const char *name = ed->path != NULL ? ed->path : "[No Name]";
    char left[256];
    snprintf(left, sizeof(left), " %s%s", name, ed->dirty ? " [+]" : "");

    char right[160];
    if (g->cols >= 64) {
        snprintf(right, sizeof(right),
                 "%zu:%zu  %zu lines  Ctrl-S save  Ctrl-Q quit  Ctrl-E cmd ",
                 ed->cx_line + 1, ed->cx_col + 1, ed->doc.count);
    } else {
        snprintf(right, sizeof(right), "%zu:%zu  %zu lines ",
                 ed->cx_line + 1, ed->cx_col + 1, ed->doc.count);
    }

    int right_len = (int)strlen(right);
    int left_len  = (int)strlen(left);
    int right_start = g->cols - right_len;

    for (int c = 0; c < g->cols; c++) {
        uint32_t cp = ' ';
        if (c < left_len) {
            cp = (unsigned char)left[c];
        } else if (c >= right_start && c < right_start + right_len) {
            cp = (unsigned char)right[c - right_start];
        }
        set_cell(g, row, c, cp, ATTR_REVERSE, 1);
    }
}

static void render_paint_prompt_or_message(editor_t *ed, grid_t *g, int row)
{
    int col = 0;
    if (ed->prompt.active) {
        /* Draw prompt label */
        const char *lbl = ed->prompt.label;
        while (*lbl != '\0' && col < g->cols) {
            set_cell(g, row, col++, (unsigned char)*lbl++, ATTR_BOLD, 1);
        }
        /* Draw prompt text */
        size_t i = 0;
        size_t p_len = ed->prompt.len;
        while (i < p_len && col < g->cols) {
            uint32_t cp = 0;
            size_t used = tam_utf8_decode(ed->prompt.buf + i, p_len - i, &cp);
            if (used == 0) break;
            set_cell(g, row, col++, cp, ATTR_NONE, 1);
            i += used;
        }
    } else if (ed->message[0] != '\0') {
        const char *m = ed->message;
        if (col < g->cols) set_cell(g, row, col++, ' ', ATTR_NONE, 1);
        while (*m != '\0' && col < g->cols) {
            set_cell(g, row, col++, (unsigned char)*m++, ATTR_NONE, 1);
        }
    }
}

void render_draw(renderer_t *ren, editor_t *ed, tam_tty_t *tty)
{
    int rows = tty->rows > 0 ? tty->rows : 24;
    int cols = tty->cols > 0 ? tty->cols : 80;
    if (rows < 3) rows = 3;
    if (cols < 2) cols = 2;

    renderer_resize(ren, rows, cols);

    int text_rows = rows - 2;

    /* Scroll vertical to cursor */
    if (ed->cx_line < ed->top_line) ed->top_line = ed->cx_line;
    if (ed->cx_line >= ed->top_line + (size_t)text_rows) {
        ed->top_line = ed->cx_line - (size_t)text_rows + 1;
    }

    /* Scroll horizontal to cursor */
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

    /* 1. Clear back grid */
    for (int i = 0; i < rows * cols; i++) {
        ren->back.cells[i].codepoint = ' ';
        ren->back.cells[i].attr      = ATTR_NONE;
        ren->back.cells[i].width     = 1;
    }

    /* 2. Paint document lines */
    for (int r = 0; r < text_rows; r++) {
        size_t li = ed->top_line + (size_t)r;
        if (li < ed->doc.count) {
            render_paint_line(ed, &ren->back, li, r, (int)ed->left_col);
        } else {
            /* Empty lines beyond EOF */
            set_cell(&ren->back, r, 0, '~', ATTR_NONE, 1);
        }
    }

    /* 3. Paint status line */
    render_paint_status(ed, &ren->back, rows - 2);

    /* 4. Paint message / prompt line */
    render_paint_prompt_or_message(ed, &ren->back, rows - 1);

    /* 5. Damage diffing and flush */
    tam_str_t out = tam_str_new("");
    if (out == NULL) return;

    out = tam_str_cat(out, "\x1b[?25l"); /* Hide cursor during repaint */

    uint16_t cur_attr = ATTR_NONE;

    for (int r = 0; r < rows; r++) {
        int c = 0;
        while (c < cols) {
            int idx = r * cols + c;
            cell_t *b = &ren->back.cells[idx];
            cell_t *f = &ren->front.cells[idx];

            if (ren->force_redraw ||
                b->codepoint != f->codepoint ||
                b->attr != f->attr ||
                b->width != f->width) {

                /* Move cursor to row and col */
                out = tam_str_cat_printf(out, "\x1b[%d;%dH", r + 1, c + 1);

                /* Output run of cells with identical attributes */
                while (c < cols) {
                    idx = r * cols + c;
                    b = &ren->back.cells[idx];
                    f = &ren->front.cells[idx];

                    if (!ren->force_redraw &&
                        b->codepoint == f->codepoint &&
                        b->attr == f->attr &&
                        b->width == f->width) {
                        /* End of dirty run */
                        break;
                    }

                    if (b->attr != cur_attr) {
                        if (cur_attr & ATTR_REVERSE) out = tam_str_cat(out, "\x1b[27m");
                        if (cur_attr & ATTR_BOLD)    out = tam_str_cat(out, "\x1b[22m");
                        if (b->attr & ATTR_REVERSE)  out = tam_str_cat(out, "\x1b[7m");
                        if (b->attr & ATTR_BOLD)     out = tam_str_cat(out, "\x1b[1m");
                        cur_attr = b->attr;
                    }

                    if (b->width != 0) {
                        char utf8[4];
                        size_t n = tam_utf8_encode(b->codepoint, utf8);
                        out = tam_str_cat_len(out, utf8, n);
                    }

                    c++;
                }
            } else {
                c++;
            }
        }
    }

    if (cur_attr != ATTR_NONE) {
        out = tam_str_cat(out, "\x1b[0m");
    }

    /* 6. Hardware cursor placement */
    int crow, ccol;
    if (ed->prompt.active) {
        crow = rows;
        int lbl_w = (int)strlen(ed->prompt.label);
        int prompt_cx_cells = edit_visual_col(ed->prompt.buf, ed->prompt.len, ed->prompt.cx);
        ccol = lbl_w + prompt_cx_cells + 1;
    } else {
        crow = (int)(ed->cx_line - ed->top_line) + 1;
        ccol = cursor_cells - (int)ed->left_col + 1;
    }
    if (crow < 1) crow = 1;
    if (crow > rows) crow = rows;
    if (ccol < 1) ccol = 1;
    if (ccol > cols) ccol = cols;

    out = tam_str_cat_printf(out, "\x1b[%d;%dH\x1b[?25h", crow, ccol);

    tam_tty_frame_begin(tty);
    tam_tty_write(tty, out, tam_str_len(out));
    tam_tty_frame_end(tty);

    tam_str_free(out);

    /* 7. Copy back grid to front grid */
    memcpy(ren->front.cells, ren->back.cells, (size_t)rows * (size_t)cols * sizeof(cell_t));
    ren->force_redraw = false;
}
