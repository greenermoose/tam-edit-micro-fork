#ifndef TAM_EDIT_EDITOR_H
#define TAM_EDIT_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EDIT_TABSTOP 8

#ifndef TAM_EDIT_SELECTION_DEFINED
#define TAM_EDIT_SELECTION_DEFINED
typedef struct {
    size_t line; /* 0-based line index */
    size_t col;  /* 0-based rune (code point) column */
} loc_t;

typedef struct {
    loc_t anchor;
    loc_t cursor;
    bool  active;
} selection_t;

typedef struct {
    size_t      cx_line;
    size_t      cx_col;
    size_t      goal_col;
    selection_t sel;
} cursor_t;
#endif

typedef struct {
    char  **lines;
    size_t *lens;
    size_t  count;
    size_t  cap;
} edit_doc_t;

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
    bool          explicit_group;
    int           explicit_depth;
} undo_stack_t;

typedef enum {
    PROMPT_NONE = 0,
    PROMPT_COMMAND,
    PROMPT_FIND,
    PROMPT_FIND_LITERAL,
    PROMPT_GOTO,
    PROMPT_SAVE_AS,
    PROMPT_QUIT_CONFIRM,
    PROMPT_SHELL,
    PROMPT_REPLACE_CONFIRM,
    PROMPT_CONFIRM_RELOAD,
    PROMPT_SHOW_KEY
} prompt_kind_t;

#define PROMPT_HIST_MAX 64

typedef struct {
    char *items[PROMPT_HIST_MAX];
    int   count;
    int   cursor; /* -1 when editing new line */
    char  saved_cur[512];
} prompt_history_t;

typedef struct {
    prompt_kind_t kind;
    char          buf[512];
    size_t        len;
    size_t        cx;        /* rune column cursor inside prompt */
    char          label[64]; /* prompt label, e.g. "Find: " or "> " */
    bool          active;
    char          payload[512];
    char        **comp_items;
    size_t        comp_count;
    size_t        comp_idx;
    size_t        comp_token_start;
    bool          comp_active;
} prompt_t;

struct tam_regex;

typedef struct {
    char              query[256];
    char              compiled_query[256];
    bool              literal;
    bool              ignorecase;
    bool              compiled_literal;
    bool              compiled_ignorecase;
    struct tam_regex *compiled;
    size_t            last_line;
    size_t            last_col;
    bool              has_match;
    /* Replace state */
    char              replace_with[256];
    bool              replace_in_selection;
    bool              replace_active;
    size_t            rep_cur_line;
    size_t            rep_cur_col;
    size_t            rep_end_line;
    size_t            rep_end_col;
    size_t            rep_match_len;
    size_t            rep_count;
    /* Incremental search snapshot */
    bool              inc_saved;
    size_t            inc_cx_line;
    size_t            inc_cx_col;
    size_t            inc_top_line;
    size_t            inc_left_col;
    selection_t       inc_sel;
} search_state_t;

/* Forward declarations for Milestone 2 modules */
struct buffer;
struct view;
struct custom_keybind;

#include "colorscheme.h"
#include "config.h"
#include "pane.h"
#include "shell.h"

typedef struct {
    edit_doc_t     doc;
    size_t         cx_line;
    size_t         cx_col;
    size_t         goal_col;
    size_t         top_line;
    size_t         left_col;
    int            top_subline;
    selection_t    sel;
    cursor_t      *extra_cursors;
    size_t         extra_count;
    size_t         extra_cap;
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

    /* Milestone 2: Views, Split Tree, Tabs, Config & Colorscheme */
    config_t          config;
    colorscheme_t     colorscheme;
    custom_keybind_t *keybindings;
    tab_list_t        tabs;
    view_t           *active_view;

    /* Milestone 4: TTY handle, background shell job, prompt histories */
    tam_tty_t        *tty;
    shell_bg_t        bg_job;
    prompt_history_t  hist_cmd;
    prompt_history_t  hist_find;
    prompt_history_t  hist_shell;
    size_t            last_match_len;
} editor_t;

/* Editor lifecycle and document operations */
void editor_init(editor_t *ed);
void editor_free(editor_t *ed);
bool editor_load(editor_t *ed, const char *path);
bool editor_save(editor_t *ed);
bool editor_save_configured(editor_t *ed, char *err_buf, size_t err_size);
bool editor_save_to(editor_t *ed, const char *path);
bool editor_save_to_configured(editor_t *ed, const char *path, char *err_buf, size_t err_size);
bool editor_open_file(editor_t *ed, const char *path, bool in_new_tab);
bool editor_open_scratch(editor_t *ed, const char *title, const char *text, bool split_help);
bool editor_reopen(editor_t *ed, const char *encoding);
bool editor_textfilter(editor_t *ed, const char *cmd);

void editor_set_message(editor_t *ed, const char *fmt, ...);
void editor_sync_to_active(editor_t *ed);
void editor_sync_from_active(editor_t *ed);
void editor_set_active_view(editor_t *ed, view_t *v);

/* Milestone 2 View, Split and Tab actions */
bool editor_vsplit(editor_t *ed, const char *path);
bool editor_hsplit(editor_t *ed, const char *path);
bool editor_unsplit(editor_t *ed);
bool editor_next_split(editor_t *ed);
bool editor_prev_split(editor_t *ed);
bool editor_add_tab(editor_t *ed, const char *path);
bool editor_close_tab(editor_t *ed);
bool editor_next_tab(editor_t *ed);
bool editor_prev_tab(editor_t *ed);

/* Milestone 4 line editing & multi-cursor operations */
void editor_delete_line(editor_t *ed);
void editor_duplicate(editor_t *ed);
void editor_move_lines(editor_t *ed, int dir);
void editor_spawn_cursor_dir(editor_t *ed, int dir);
void editor_spawn_cursor_match(editor_t *ed, bool skip_current);
void editor_spawn_cursors_on_selection(editor_t *ed);
void editor_remove_last_cursor(editor_t *ed);
void editor_remove_all_cursors(editor_t *ed);
void editor_toggle_cursor_at(editor_t *ed, size_t line, size_t col);
void editor_merge_cursors(editor_t *ed);

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
void editor_delete_word_left(editor_t *ed);
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

/* Softwrap helpers */
size_t edit_softwrap_line_sublines(const char *line, size_t len, int width,
                                   int tabsize, bool wordwrap);
void   edit_softwrap_loc_to_vloc(const char *line, size_t len, size_t rune_col,
                                 int width, int tabsize, bool wordwrap,
                                 int *subline_out, int *vx_out);
size_t edit_softwrap_vloc_to_rune(const char *line, size_t len, int subline,
                                  int target_vx, int width, int tabsize,
                                  bool wordwrap);

#endif
