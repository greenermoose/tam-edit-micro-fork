#ifndef TAM_EDIT_EDITOR_H
#define TAM_EDIT_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EDIT_TABSTOP 8

typedef struct {
    size_t line; /* 0-based line index */
    size_t col;  /* 0-based rune (code point) column */
} loc_t;

typedef struct {
    char  **lines;
    size_t *lens;
    size_t  count;
    size_t  cap;
} edit_doc_t;

typedef struct {
    loc_t anchor;
    loc_t cursor;
    bool  active;
} selection_t;

typedef enum {
    DELTA_INSERT = 1,
    DELTA_REMOVE = -1
} delta_type_t;

typedef struct {
    delta_type_t type;
    size_t       line;
    size_t       col;
    char        *text;
    size_t       len;
} delta_t;

typedef struct undo_group {
    delta_t           *deltas;
    size_t             count;
    size_t             cap;
    loc_t              cursor_before;
    loc_t              cursor_after;
    int64_t            time_ms;
    struct undo_group *next;
} undo_group_t;

typedef struct {
    undo_group_t *head;
    size_t        count;
} undo_stack_t;

typedef enum {
    PROMPT_NONE = 0,
    PROMPT_COMMAND,
    PROMPT_FIND,
    PROMPT_GOTO,
    PROMPT_SAVE_AS,
    PROMPT_QUIT_CONFIRM
} prompt_kind_t;

typedef struct {
    prompt_kind_t kind;
    char          buf[512];
    size_t        len;
    size_t        cx;        /* rune column cursor inside prompt */
    char          label[64]; /* prompt label, e.g. "Find: " or "> " */
    bool          active;
} prompt_t;

typedef struct {
    char   query[256];
    size_t last_line;
    size_t last_col;
    bool   has_match;
} search_state_t;

typedef struct {
    edit_doc_t     doc;
    size_t         cx_line;
    size_t         cx_col;
    size_t         goal_col;
    size_t         top_line;
    size_t         left_col;
    selection_t    sel;
    undo_stack_t   undo_stack;
    undo_stack_t   redo_stack;
    prompt_t       prompt;
    search_state_t search;
    char          *path;
    bool           crlf;
    bool           dirty;
    bool           quit_armed;
    bool           running;
    int            tty_out_fd;
    char           message[256];
} editor_t;

/* Editor lifecycle and document operations */
void editor_init(editor_t *ed);
void editor_free(editor_t *ed);
bool editor_load(editor_t *ed, const char *path);
bool editor_save(editor_t *ed);
bool editor_save_to(editor_t *ed, const char *path);

void editor_set_message(editor_t *ed, const char *fmt, ...);

/* Low-level document operations */
void editor_raw_insert(editor_t *ed, size_t line, size_t col, const char *text, size_t len);
void editor_raw_remove(editor_t *ed, size_t line, size_t col, size_t runes);
void editor_raw_remove_str(editor_t *ed, size_t line, size_t col, const char *text, size_t len);

/* Text editing with undo integration */
void editor_insert_codepoint(editor_t *ed, uint32_t cp);
void editor_insert_text(editor_t *ed, const char *text, size_t len);
void editor_insert_newline(editor_t *ed);
void editor_insert_tab(editor_t *ed);
void editor_backspace(editor_t *ed);
void editor_delete(editor_t *ed);

/* Cursor & motion compatibility helpers */
void editor_move(editor_t *ed, long dx, long dy);
void editor_home(editor_t *ed);
void editor_end(editor_t *ed);
void editor_page(editor_t *ed, long pages, int text_rows);

/* Undo and Redo */
bool editor_undo(editor_t *ed);
bool editor_redo(editor_t *ed);

/* Line & Rune helpers */
size_t edit_line_runes(const char *line, size_t len);
size_t edit_rune_to_byte(const char *line, size_t len, size_t col);
int    edit_visual_col(const char *line, size_t len, size_t col);
size_t edit_visual_to_rune(const char *line, size_t len, int cells);

#endif
