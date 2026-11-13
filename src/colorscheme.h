#ifndef TAM_EDIT_COLORSCHEME_H
#define TAM_EDIT_COLORSCHEME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ATTR_NONE      0
#define ATTR_REVERSE   (1u << 0)
#define ATTR_BOLD      (1u << 1)
#define ATTR_UNDERLINE (1u << 2)
#define ATTR_ITALIC    (1u << 3)

#define COLOR_DEFAULT    0xFFFFFFFFu
#define COLOR_IS_DEFAULT(c) ((c) == COLOR_DEFAULT)
#define COLOR_IS_RGB(c)     (((c) & 0xFF000000u) == 0)
#define COLOR_RGB(r, g, b)  (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define COLOR_R(c)          (((c) >> 16) & 0xFF)
#define COLOR_G(c)          (((c) >> 8) & 0xFF)
#define COLOR_B(c)          ((c) & 0xFF)

#define COLOR_IS_ANSI(c)    (((c) & 0xFF000000u) == 0x01000000u)
#define COLOR_ANSI(n)       (0x01000000u | ((uint32_t)(n) & 0xFF))
#define COLOR_ANSI_NUM(c)   ((c) & 0xFF)

typedef struct {
    uint32_t fg;     /* COLOR_DEFAULT, RGB, or COLOR_ANSI */
    uint32_t bg;     /* COLOR_DEFAULT, RGB, or COLOR_ANSI */
    uint16_t attr;   /* Bitmask of ATTR_* */
} style_t;

typedef struct color_link {
    char              *group;
    style_t            style;
    struct color_link *next;
} color_link_t;

typedef struct {
    char          name[64];
    color_link_t *links;
    style_t       default_style;
} colorscheme_t;

void    colorscheme_init(colorscheme_t *cs);
void    colorscheme_free(colorscheme_t *cs);
bool    colorscheme_load(colorscheme_t *cs, const char *name);
bool    colorscheme_load_file(colorscheme_t *cs, const char *path);
style_t colorscheme_get(const colorscheme_t *cs, const char *group);

/* Helper to parse style from micro string format "attrs fg,bg" */
style_t style_parse(const char *str);

#endif
