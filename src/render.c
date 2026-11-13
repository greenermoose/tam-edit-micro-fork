#include "render.h"
#include "buffer.h"
#include "cursor.h"
#include "colorscheme.h"
#include "pane.h"
#include "syntax.h"

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
        new_cells[i].fg        = COLOR_DEFAULT;
        new_cells[i].bg        = COLOR_DEFAULT;
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

static void set_cell(grid_t *g, int r, int c, uint32_t cp,
                     uint32_t fg, uint32_t bg, uint16_t attr, uint8_t w)
{
    if (r < 0 || r >= g->rows || c < 0 || c >= g->cols) return;
    int idx = r * g->cols + c;
    g->cells[idx].codepoint = cp;
    g->cells[idx].fg        = fg;
    g->cells[idx].bg        = bg;
    g->cells[idx].attr      = attr;
    g->cells[idx].width     = w;
}

static bool in_view_selection(const view_t *v, size_t line, size_t rune_col)
{
    if (!v->sel.active) return false;
    loc_t start = v->sel.anchor;
    loc_t end   = v->sel.cursor;
    if (start.line > end.line || (start.line == end.line && start.col > end.col)) {
        loc_t tmp = start;
        start = end;
        end = tmp;
    }
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

/* -------------------------------------------------------------------------- */
/* Painting Tab Bar                                                           */
/* -------------------------------------------------------------------------- */

static void render_paint_tabbar(editor_t *ed, grid_t *g)
{
    style_t tb_style = colorscheme_get(&ed->colorscheme, "tabbar");
    if (tb_style.bg == COLOR_DEFAULT && tb_style.fg == COLOR_DEFAULT) {
        tb_style.attr |= ATTR_REVERSE;
    }

    /* Fill background of row 0 */
    for (int c = 0; c < g->cols; c++) {
        set_cell(g, 0, c, ' ', tb_style.fg, tb_style.bg, tb_style.attr, 1);
    }

    int col = 1;
    for (size_t i = 0; i < ed->tabs.count && col < g->cols; i++) {
        tab_t *t = ed->tabs.tabs[i];
        char title[128];
        tab_get_title(t, title, sizeof(title));

        char tab_text[160];
        snprintf(tab_text, sizeof(tab_text), " %s%s ", title,
                 tab_is_dirty(t) ? " [+]" : "");

        bool is_active = (i == ed->tabs.active);
        style_t st = tb_style;
        if (is_active) {
            st.attr |= ATTR_BOLD;
            if (!(st.attr & ATTR_REVERSE)) st.attr |= ATTR_REVERSE;
        }

        for (size_t j = 0; tab_text[j] != '\0' && col < g->cols; j++) {
            set_cell(g, 0, col++, (unsigned char)tab_text[j],
                     st.fg, st.bg, st.attr, 1);
        }
        col++;
    }
}

/* -------------------------------------------------------------------------- */
/* Painting Split View Text and Gutter                                        */
/* -------------------------------------------------------------------------- */

static inline void apply_cell_syntax(const colorscheme_t *cs,
                                     const style_t *def_style,
                                     const uint16_t *syn_groups,
                                     size_t syn_runes,
                                     size_t rune_col,
                                     uint32_t *cell_fg,
                                     uint32_t *cell_bg,
                                     uint16_t *attr)
{
    if (!syn_groups || rune_col >= syn_runes) return;
    uint16_t gid = syn_groups[rune_col];
    if (gid == 0 || gid >= SYNTAX_MAX_GROUPS) return;
    style_t st = cs->group_cache[gid];
    *cell_fg = st.fg;
    *attr = st.attr;
    if (st.bg != COLOR_DEFAULT && st.bg != def_style->bg) {
        *cell_bg = st.bg;
    }
}

static void render_paint_view(editor_t *ed, view_t *v, grid_t *g,
                              style_t def_style, style_t num_style,
                              style_t cur_num_style, style_t curline_style)
{
    if (v == NULL || v->buf == NULL || v->w <= 1 || v->h <= 1) return;
    buffer_t *b = v->buf;

    int text_rows = v->h > 1 ? v->h - 1 : 1;
    int tabsize = ed->config.tabsize > 0 ? ed->config.tabsize : 4;

    /* Ruler gutter calculation */
    int gw = 0;
    if (ed->config.ruler) {
        gw = 4;
        if (b->doc.count >= 1000) gw = 5;
        if (b->doc.count >= 10000) gw = 6;
        if (gw >= v->w / 2) gw = 0;
    }
    int text_cols = v->w - gw;
    if (text_cols < 1) text_cols = 1;

    /* Vertical scroll clamping */
    if (!v->softwrap) {
        if (v->cx_line < v->top_line) v->top_line = v->cx_line;
        if (v->cx_line >= v->top_line + (size_t)text_rows) {
            v->top_line = v->cx_line - (size_t)text_rows + 1;
        }

        /* Horizontal scroll clamping */
        int cx_cells = 0;
        if (v->cx_line < b->doc.count) {
            cx_cells = edit_visual_col(b->doc.lines[v->cx_line],
                                       b->doc.lens[v->cx_line],
                                       v->cx_col);
        }
        if (cx_cells < (int)v->left_col) v->left_col = (size_t)cx_cells;
        if (cx_cells >= (int)v->left_col + text_cols) {
            v->left_col = (size_t)(cx_cells - text_cols + 1);
        }
    } else {
        /* Softwrap vertical scrolling: ensure current line is visible */
        if (v->cx_line < v->top_line) v->top_line = v->cx_line;
        if (v->cx_line >= v->top_line + (size_t)text_rows) {
            v->top_line = v->cx_line - (size_t)text_rows + 1;
        }
        v->left_col = 0;
    }

    /* Advance incremental syntax states up through the visible viewport */
    if (b->syntax_enabled && b->syntax_def != NULL && text_rows > 0) {
        buffer_syntax_update_to(b, v->top_line + (size_t)text_rows);
    }

    /* Paint text rows */
    int screen_r = 0;
    size_t line_idx = v->top_line;

    while (screen_r < text_rows) {
        int r = v->y + screen_r;
        if (line_idx < b->doc.count) {
            const char *line = b->doc.lines[line_idx];
            size_t len = b->doc.lens[line_idx];
            bool is_cur_line = (v == ed->active_view && line_idx == v->cx_line);
            size_t syn_runes = 0;
            const uint16_t *syn_groups = buffer_syntax_get_line_groups(b, line_idx, &syn_runes);

            /* Paint gutter line number */
            if (gw > 0) {
                char num_buf[32];
                snprintf(num_buf, sizeof(num_buf), "%*zu ", gw - 1, line_idx + 1);
                style_t n_st = is_cur_line ? cur_num_style : num_style;
                for (int gc = 0; gc < gw && v->x + gc < v->x + v->w; gc++) {
                    uint32_t cp = (gc < (int)strlen(num_buf)) ? (unsigned char)num_buf[gc] : ' ';
                    set_cell(g, r, v->x + gc, cp, n_st.fg, n_st.bg, n_st.attr, 1);
                }
            }

            int content_x = v->x + gw;
            uint32_t row_bg = (ed->config.cursorline && is_cur_line) ? curline_style.bg : def_style.bg;

            if (!v->softwrap) {
                /* Non-wrapped line painting */
                int visual = 0;
                size_t i = 0;
                size_t rune_col = 0;

                while (i < len && visual < (int)v->left_col + text_cols) {
                    uint32_t cp = 0;
                    size_t used = tam_utf8_decode(line + i, len - i, &cp);
                    if (used == 0) break;

                    int width = (cp == '\t') ? (tabsize - (visual % tabsize)) :
                                ((cp < 0x20 || cp == 0x7F) ? 1 : tam_uni_width(cp));

                    uint16_t attr = def_style.attr;
                    uint32_t cell_fg = def_style.fg;
                    uint32_t cell_bg = row_bg;

                    apply_cell_syntax(&ed->colorscheme, &def_style,
                                      syn_groups, syn_runes, rune_col,
                                      &cell_fg, &cell_bg, &attr);

                    if (in_view_selection(v, line_idx, rune_col)) {
                        attr |= ATTR_REVERSE;
                    }

                    for (int w = 0; w < width; w++) {
                        int sc = visual + w - (int)v->left_col;
                        if (sc >= 0 && sc < text_cols) {
                            uint32_t out_cp = (cp == '\t' || cp < 0x20 || cp == 0x7F)
                                                  ? ' ' : (w == 0 ? cp : ' ');
                            uint8_t cell_w = (width == 2 && w == 1) ? 0 : (uint8_t)width;
                            set_cell(g, r, content_x + sc, out_cp, cell_fg, cell_bg, attr, cell_w);
                        }
                    }

                    visual += width;
                    i += used;
                    rune_col++;
                }

                /* Clear remaining line columns */
                int sc = visual - (int)v->left_col;
                if (sc < 0) sc = 0;
                for (; sc < text_cols; sc++) {
                    set_cell(g, r, content_x + sc, ' ', def_style.fg, row_bg, def_style.attr, 1);
                }
                screen_r++;
            } else {
                /* Softwrap line painting */
                size_t sublines = edit_softwrap_line_sublines(line, len, text_cols, tabsize, v->wordwrap);
                for (size_t sub = 0; sub < sublines && screen_r < text_rows; sub++) {
                    int r_sub = v->y + screen_r;
                    if (sub > 0 && gw > 0) {
                        /* Continuation line gutter */
                        for (int gc = 0; gc < gw; gc++) {
                            set_cell(g, r_sub, v->x + gc, ' ', num_style.fg, num_style.bg, num_style.attr, 1);
                        }
                    }

                    /* Paint characters belonging to this subline */
                    size_t rune_start = edit_softwrap_vloc_to_rune(line, len, (int)sub, 0, text_cols, tabsize, v->wordwrap);
                    size_t rune_end   = edit_softwrap_vloc_to_rune(line, len, (int)sub + 1, 0, text_cols, tabsize, v->wordwrap);

                    size_t b_start = edit_rune_to_byte(line, len, rune_start);
                    size_t b_end   = edit_rune_to_byte(line, len, rune_end);
                    size_t i = b_start;
                    size_t rune_col = rune_start;
                    int visual = 0;

                    while (i < b_end && visual < text_cols) {
                        uint32_t cp = 0;
                        size_t used = tam_utf8_decode(line + i, len - i, &cp);
                        if (used == 0) break;

                        int width = (cp == '\t') ? (tabsize - (visual % tabsize)) :
                                    ((cp < 0x20 || cp == 0x7F) ? 1 : tam_uni_width(cp));

                        uint16_t attr = def_style.attr;
                        uint32_t cell_fg = def_style.fg;
                        uint32_t cell_bg = row_bg;

                        apply_cell_syntax(&ed->colorscheme, &def_style,
                                          syn_groups, syn_runes, rune_col,
                                          &cell_fg, &cell_bg, &attr);

                        if (in_view_selection(v, line_idx, rune_col)) {
                            attr |= ATTR_REVERSE;
                        }

                        for (int w = 0; w < width; w++) {
                            int sc = visual + w;
                            if (sc >= 0 && sc < text_cols) {
                                uint32_t out_cp = (cp == '\t' || cp < 0x20 || cp == 0x7F)
                                                      ? ' ' : (w == 0 ? cp : ' ');
                                uint8_t cell_w = (width == 2 && w == 1) ? 0 : (uint8_t)width;
                                set_cell(g, r_sub, content_x + sc, out_cp, cell_fg, cell_bg, attr, cell_w);
                            }
                        }

                        visual += width;
                        i += used;
                        rune_col++;
                    }

                    for (; visual < text_cols; visual++) {
                        set_cell(g, r_sub, content_x + visual, ' ', def_style.fg, row_bg, def_style.attr, 1);
                    }
                    screen_r++;
                }
            }
            line_idx++;
        } else {
            /* Empty lines beyond EOF */
            if (gw > 0) {
                for (int gc = 0; gc < gw; gc++) {
                    set_cell(g, r, v->x + gc, ' ', num_style.fg, num_style.bg, num_style.attr, 1);
                }
            }
            set_cell(g, r, v->x + gw, '~', def_style.fg, def_style.bg, def_style.attr, 1);
            for (int sc = 1; sc < text_cols; sc++) {
                set_cell(g, r, v->x + gw + sc, ' ', def_style.fg, def_style.bg, def_style.attr, 1);
            }
            screen_r++;
            line_idx++;
        }
    }

    /* Paint pane statusline at v->y + v->h - 1 */
    int status_row = v->y + v->h - 1;
    style_t st_style = colorscheme_get(&ed->colorscheme, "statusline");
    bool is_active_view = (v == ed->active_view);

    if (is_active_view) {
        st_style.attr |= ATTR_REVERSE;
    }

    const char *name = b->path != NULL ? b->path : "[No Name]";
    const char *base = strrchr(name, '/');
    if (base != NULL) base++;
    else base = name;

    char left_txt[128];
    snprintf(left_txt, sizeof(left_txt), " %s%s", base, b->dirty ? " [+]" : "");

    char right_txt[96];
    if (b->filetype[0] != '\0' && strcmp(b->filetype, "unknown") != 0) {
        snprintf(right_txt, sizeof(right_txt), "%s | %zu:%zu ",
                 b->filetype, v->cx_line + 1, v->cx_col + 1);
    } else {
        snprintf(right_txt, sizeof(right_txt), "%zu:%zu ", v->cx_line + 1, v->cx_col + 1);
    }

    int l_len = (int)strlen(left_txt);
    int r_len = (int)strlen(right_txt);
    int r_start = v->w - r_len;

    for (int sc = 0; sc < v->w; sc++) {
        uint32_t cp = ' ';
        if (sc < l_len) {
            cp = (unsigned char)left_txt[sc];
        } else if (sc >= r_start && sc < r_start + r_len) {
            cp = (unsigned char)right_txt[sc - r_start];
        }
        set_cell(g, status_row, v->x + sc, cp, st_style.fg, st_style.bg, st_style.attr, 1);
    }
}

static void render_paint_panes_recursive(editor_t *ed, pane_node_t *node, grid_t *g,
                                         style_t def_style, style_t num_style,
                                         style_t cur_num_style, style_t curline_style)
{
    if (node == NULL) return;

    if (node->type == PANE_LEAF) {
        render_paint_view(ed, node->view, g, def_style, num_style,
                          cur_num_style, curline_style);
        return;
    }

    if (node->type == PANE_SPLIT_V) {
        /* Vertical divider between child1 and child2 */
        int div_col = node->child1->x + node->child1->w;
        style_t div_style = colorscheme_get(&ed->colorscheme, "divider");
        for (int r = node->y; r < node->y + node->h; r++) {
            set_cell(g, r, div_col, 0x2502, div_style.fg, div_style.bg, div_style.attr, 1);
        }
    }

    render_paint_panes_recursive(ed, node->child1, g, def_style, num_style,
                                 cur_num_style, curline_style);
    render_paint_panes_recursive(ed, node->child2, g, def_style, num_style,
                                 cur_num_style, curline_style);
}

/* -------------------------------------------------------------------------- */
/* Painting Infobar / Prompt / Message                                        */
/* -------------------------------------------------------------------------- */

static void render_paint_prompt_or_message(editor_t *ed, grid_t *g, int row)
{
    int col = 0;
    if (ed->prompt.active) {
        const char *lbl = ed->prompt.label;
        while (*lbl != '\0' && col < g->cols) {
            set_cell(g, row, col++, (unsigned char)*lbl++, COLOR_DEFAULT, COLOR_DEFAULT, ATTR_BOLD, 1);
        }
        size_t i = 0;
        size_t p_len = ed->prompt.len;
        while (i < p_len && col < g->cols) {
            uint32_t cp = 0;
            size_t used = tam_utf8_decode(ed->prompt.buf + i, p_len - i, &cp);
            if (used == 0) break;
            set_cell(g, row, col++, cp, COLOR_DEFAULT, COLOR_DEFAULT, ATTR_NONE, 1);
            i += used;
        }
    } else if (ed->message[0] != '\0') {
        const char *m = ed->message;
        if (col < g->cols) set_cell(g, row, col++, ' ', COLOR_DEFAULT, COLOR_DEFAULT, ATTR_NONE, 1);
        while (*m != '\0' && col < g->cols) {
            set_cell(g, row, col++, (unsigned char)*m++, COLOR_DEFAULT, COLOR_DEFAULT, ATTR_NONE, 1);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Terminal Diffing and Flush                                                 */
/* -------------------------------------------------------------------------- */

void render_draw(renderer_t *ren, editor_t *ed, tam_tty_t *tty)
{
    int rows = tty->rows > 0 ? tty->rows : 24;
    int cols = tty->cols > 0 ? tty->cols : 80;
    if (rows < 3) rows = 3;
    if (cols < 2) cols = 2;

    renderer_resize(ren, rows, cols);

    if (!ed->colorscheme.cache_valid) {
        syntax_build_style_cache(&ed->colorscheme, ed->colorscheme.group_cache);
        ed->colorscheme.cache_valid = true;
    }

    style_t def_style     = colorscheme_get(&ed->colorscheme, "default");
    style_t num_style     = colorscheme_get(&ed->colorscheme, "line-number");
    style_t cur_num_style = colorscheme_get(&ed->colorscheme, "current-line-number");
    style_t curline_style = colorscheme_get(&ed->colorscheme, "cursor-line");

    /* 1. Clear back grid */
    for (int i = 0; i < rows * cols; i++) {
        ren->back.cells[i].codepoint = ' ';
        ren->back.cells[i].fg        = def_style.fg;
        ren->back.cells[i].bg        = def_style.bg;
        ren->back.cells[i].attr      = ATTR_NONE;
        ren->back.cells[i].width     = 1;
    }

    /* 2. Check Tab Bar */
    bool show_tabbar = (ed->config.tabbar == TABBAR_ON) ||
                       (ed->config.tabbar == TABBAR_AUTO && ed->tabs.count > 1);
    int content_top = show_tabbar ? 1 : 0;
    int bottom_prompt_row = rows - 1;
    int content_height = bottom_prompt_row - content_top;
    if (content_height < 1) content_height = 1;

    if (show_tabbar) {
        render_paint_tabbar(ed, &ren->back);
    }

    /* 3. Paint Active Tab Split Tree */
    if (ed->tabs.count > 0 && ed->tabs.active < ed->tabs.count) {
        tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
        pane_layout(cur_tab->root, 0, content_top, cols, content_height);
        render_paint_panes_recursive(ed, cur_tab->root, &ren->back,
                                     def_style, num_style, cur_num_style, curline_style);
    }

    /* 4. Paint Prompt / Message line */
    render_paint_prompt_or_message(ed, &ren->back, bottom_prompt_row);

    /* 5. Damage diffing and flush with ANSI/Truecolor */
    tam_str_t out = tam_str_new("");
    if (out == NULL) return;

    out = tam_str_cat(out, "\x1b[?25l"); /* Hide cursor during flush */

    uint32_t cur_fg   = COLOR_DEFAULT;
    uint32_t cur_bg   = COLOR_DEFAULT;
    uint16_t cur_attr = ATTR_NONE;

    for (int r = 0; r < rows; r++) {
        int c = 0;
        while (c < cols) {
            int idx = r * cols + c;
            cell_t *b = &ren->back.cells[idx];
            cell_t *f = &ren->front.cells[idx];

            if (ren->force_redraw ||
                b->codepoint != f->codepoint ||
                b->fg != f->fg ||
                b->bg != f->bg ||
                b->attr != f->attr ||
                b->width != f->width) {

                out = tam_str_cat_printf(out, "\x1b[%d;%dH", r + 1, c + 1);

                while (c < cols) {
                    idx = r * cols + c;
                    b = &ren->back.cells[idx];
                    f = &ren->front.cells[idx];

                    if (!ren->force_redraw &&
                        b->codepoint == f->codepoint &&
                        b->fg == f->fg &&
                        b->bg == f->bg &&
                        b->attr == f->attr &&
                        b->width == f->width) {
                        break;
                    }

                    /* Attribute change */
                    if (b->attr != cur_attr) {
                        if (cur_attr & ATTR_REVERSE && !(b->attr & ATTR_REVERSE)) out = tam_str_cat(out, "\x1b[27m");
                        if (cur_attr & ATTR_BOLD && !(b->attr & ATTR_BOLD))       out = tam_str_cat(out, "\x1b[22m");
                        if (cur_attr & ATTR_UNDERLINE && !(b->attr & ATTR_UNDERLINE)) out = tam_str_cat(out, "\x1b[24m");
                        if (cur_attr & ATTR_ITALIC && !(b->attr & ATTR_ITALIC))   out = tam_str_cat(out, "\x1b[23m");

                        if (b->attr & ATTR_REVERSE && !(cur_attr & ATTR_REVERSE)) out = tam_str_cat(out, "\x1b[7m");
                        if (b->attr & ATTR_BOLD && !(cur_attr & ATTR_BOLD))       out = tam_str_cat(out, "\x1b[1m");
                        if (b->attr & ATTR_UNDERLINE && !(cur_attr & ATTR_UNDERLINE)) out = tam_str_cat(out, "\x1b[4m");
                        if (b->attr & ATTR_ITALIC && !(cur_attr & ATTR_ITALIC))   out = tam_str_cat(out, "\x1b[3m");
                        cur_attr = b->attr;
                    }

                    /* Foreground color change */
                    if (b->fg != cur_fg) {
                        if (COLOR_IS_DEFAULT(b->fg)) {
                            out = tam_str_cat(out, "\x1b[39m");
                        } else if (COLOR_IS_RGB(b->fg)) {
                            out = tam_str_cat_printf(out, "\x1b[38;2;%u;%u;%um",
                                                     COLOR_R(b->fg), COLOR_G(b->fg), COLOR_B(b->fg));
                        } else if (COLOR_IS_ANSI(b->fg)) {
                            uint8_t a = COLOR_ANSI_NUM(b->fg);
                            if (a < 8) out = tam_str_cat_printf(out, "\x1b[%dm", 30 + a);
                            else if (a < 16) out = tam_str_cat_printf(out, "\x1b[%dm", 90 + (a - 8));
                            else out = tam_str_cat_printf(out, "\x1b[38;5;%um", a);
                        }
                        cur_fg = b->fg;
                    }

                    /* Background color change */
                    if (b->bg != cur_bg) {
                        if (COLOR_IS_DEFAULT(b->bg)) {
                            out = tam_str_cat(out, "\x1b[49m");
                        } else if (COLOR_IS_RGB(b->bg)) {
                            out = tam_str_cat_printf(out, "\x1b[48;2;%u;%u;%um",
                                                     COLOR_R(b->bg), COLOR_G(b->bg), COLOR_B(b->bg));
                        } else if (COLOR_IS_ANSI(b->bg)) {
                            uint8_t a = COLOR_ANSI_NUM(b->bg);
                            if (a < 8) out = tam_str_cat_printf(out, "\x1b[%dm", 40 + a);
                            else if (a < 16) out = tam_str_cat_printf(out, "\x1b[%dm", 100 + (a - 8));
                            else out = tam_str_cat_printf(out, "\x1b[48;5;%um", a);
                        }
                        cur_bg = b->bg;
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

    if (cur_attr != ATTR_NONE || cur_fg != COLOR_DEFAULT || cur_bg != COLOR_DEFAULT) {
        out = tam_str_cat(out, "\x1b[0m");
    }

    /* 6. Hardware cursor placement */
    int crow, ccol;
    if (ed->prompt.active) {
        crow = rows;
        int lbl_w = (int)strlen(ed->prompt.label);
        int prompt_cx_cells = edit_visual_col(ed->prompt.buf, ed->prompt.len, ed->prompt.cx);
        ccol = lbl_w + prompt_cx_cells + 1;
    } else if (ed->active_view != NULL) {
        view_t *av = ed->active_view;
        int gw = 0;
        if (ed->config.ruler && av->buf != NULL) {
            gw = 4;
            if (av->buf->doc.count >= 1000) gw = 5;
            if (av->buf->doc.count >= 10000) gw = 6;
            if (gw >= av->w / 2) gw = 0;
        }

        if (!av->softwrap) {
            crow = av->y + (int)(av->cx_line - av->top_line) + 1;
            int cursor_cells = 0;
            if (av->buf != NULL && av->cx_line < av->buf->doc.count) {
                cursor_cells = edit_visual_col(av->buf->doc.lines[av->cx_line],
                                               av->buf->doc.lens[av->cx_line],
                                               av->cx_col);
            }
            ccol = av->x + gw + cursor_cells - (int)av->left_col + 1;
        } else {
            int subline = 0, vx = 0;
            int tabsize = ed->config.tabsize > 0 ? ed->config.tabsize : 4;
            int text_cols = av->w > gw ? av->w - gw : 80;
            if (av->buf != NULL && av->cx_line < av->buf->doc.count) {
                edit_softwrap_loc_to_vloc(av->buf->doc.lines[av->cx_line],
                                          av->buf->doc.lens[av->cx_line],
                                          av->cx_col, text_cols, tabsize,
                                          av->wordwrap, &subline, &vx);
            }
            crow = av->y + (int)(av->cx_line - av->top_line) + subline + 1;
            ccol = av->x + gw + vx + 1;
        }
    } else {
        crow = 1;
        ccol = 1;
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

    memcpy(ren->front.cells, ren->back.cells, (size_t)rows * (size_t)cols * sizeof(cell_t));
    ren->force_redraw = false;
}
