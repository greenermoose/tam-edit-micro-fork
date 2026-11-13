#ifndef TAM_EDIT_RENDER_H
#define TAM_EDIT_RENDER_H

#include "editor.h"
#include <tam_tty.h>

#define ATTR_NONE      0
#define ATTR_REVERSE   (1u << 0)
#define ATTR_BOLD      (1u << 1)
#define ATTR_UNDERLINE (1u << 2)
#define ATTR_ITALIC    (1u << 3)

typedef struct {
    uint32_t codepoint;
    uint32_t fg;     /* RGB, COLOR_ANSI, or COLOR_DEFAULT */
    uint32_t bg;     /* RGB, COLOR_ANSI, or COLOR_DEFAULT */
    uint16_t attr;   /* Bitmask of ATTR_* */
    uint8_t  width;  /* 1, 2, or 0 (second half of wide char) */
} cell_t;

typedef struct {
    cell_t *cells;
    int     rows;
    int     cols;
} grid_t;

typedef struct {
    grid_t front;
    grid_t back;
    bool   force_redraw;
} renderer_t;

void renderer_init(renderer_t *ren);
void renderer_free(renderer_t *ren);
void renderer_resize(renderer_t *ren, int rows, int cols);

int  render_text_rows(const tam_tty_t *tty);
void render_draw(renderer_t *ren, editor_t *ed, tam_tty_t *tty);

#endif
