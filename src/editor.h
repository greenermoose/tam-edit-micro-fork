#ifndef TAM_EDIT_EDITOR_H
#define TAM_EDIT_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tab stops are fixed at eight columns, matching micro. */
#define EDIT_TABSTOP 8

/* A document is an array of lines. Each line is a byte string without its
 * terminator, so the cursor can sit at the empty line after a final newline.
 * Columns are counted in code points, as micro does; the renderer converts
 * them to terminal cells. */
typedef struct {
    char  **lines;
    size_t *lens;
    size_t  count;
    size_t  cap;
} edit_doc_t;

typedef struct {
    edit_doc_t doc;
    size_t cx_line;  /* zero-based line index */
    size_t cx_col;   /* zero-based code point column */
    size_t goal_col; /* remembered column for vertical motion */
    size_t top_line; /* first visible line */
    size_t left_col; /* first visible display column */
    char  *path;
    bool   crlf;     /* save with CRLF line endings */
    bool   dirty;
    bool   quit_armed;
    bool   running;
    char   message[256];
} editor_t;

void editor_init(editor_t *ed);
void editor_free(editor_t *ed);

/* Load a file into the document. A missing file starts an empty buffer;
 * memory that is not valid UTF-8 is kept as-is and shown with replacement
 * characters. Returns false only for read errors other than ENOENT. */
bool editor_load(editor_t *ed, const char *path);

/* Write the document back to its path. Sets message and dirty on success. */
bool editor_save(editor_t *ed);

void editor_insert_codepoint(editor_t *ed, uint32_t cp);
void editor_insert_newline(editor_t *ed);
void editor_insert_tab(editor_t *ed);
void editor_backspace(editor_t *ed);
void editor_delete(editor_t *ed);

/* Move by lines (dy) and code point columns (dx). Vertical motion tries to
 * keep the column the cursor had before the move. */
void editor_move(editor_t *ed, long dx, long dy);
void editor_home(editor_t *ed);
void editor_end(editor_t *ed);
void editor_page(editor_t *ed, long pages, int text_rows);

void editor_set_message(editor_t *ed, const char *fmt, ...);

/* Line helpers, shared with the renderer and tests. */
size_t edit_line_runes(const char *line, size_t len);
size_t edit_rune_to_byte(const char *line, size_t len, size_t col);
int    edit_visual_col(const char *line, size_t len, size_t col);
size_t edit_visual_to_rune(const char *line, size_t len, int cells);

#endif
