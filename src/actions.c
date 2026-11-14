#include "actions.h"
#include "buffer.h"
#include "clipboard.h"
#include "cursor.h"
#include "prompt.h"
#include "undo.h"

#include <tam.h>
#include <tam_uni.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_text_rows = 24;

/* -------------------------------------------------------------------------- */
/* Action Implementations                                                     */
/* -------------------------------------------------------------------------- */

static void act_cursor_up(editor_t *ed)    { cursor_move(ed, 0, -1, false); }
static void act_cursor_down(editor_t *ed)  { cursor_move(ed, 0, 1, false); }
static void act_cursor_left(editor_t *ed)  { cursor_move(ed, -1, 0, false); }
static void act_cursor_right(editor_t *ed) { cursor_move(ed, 1, 0, false); }

static void act_select_up(editor_t *ed)    { cursor_move(ed, 0, -1, true); }
static void act_select_down(editor_t *ed)  { cursor_move(ed, 0, 1, true); }
static void act_select_left(editor_t *ed)  { cursor_move(ed, -1, 0, true); }
static void act_select_right(editor_t *ed) { cursor_move(ed, 1, 0, true); }

static void act_word_left(editor_t *ed)        { cursor_word_left(ed, false); }
static void act_word_right(editor_t *ed)       { cursor_word_right(ed, false); }
static void act_select_word_left(editor_t *ed)  { cursor_word_left(ed, true); }
static void act_select_word_right(editor_t *ed) { cursor_word_right(ed, true); }

static void act_start_of_text_toggle(editor_t *ed) { cursor_home(ed, false); }
static void act_select_to_start_of_text(editor_t *ed) { cursor_home(ed, true); }
static void act_end_of_line(editor_t *ed) { cursor_end(ed, false); }
static void act_select_to_end_of_line(editor_t *ed) { cursor_end(ed, true); }

static void act_cursor_start(editor_t *ed) { cursor_start(ed, false); }
static void act_cursor_end(editor_t *ed)   { cursor_end_doc(ed, false); }
static void act_select_to_start(editor_t *ed) { cursor_start(ed, true); }
static void act_select_to_end(editor_t *ed)   { cursor_end_doc(ed, true); }

static void act_cursor_page_up(editor_t *ed)   { cursor_page(ed, -1, g_text_rows, false); }
static void act_cursor_page_down(editor_t *ed) { cursor_page(ed, 1, g_text_rows, false); }
static void act_select_page_up(editor_t *ed)   { cursor_page(ed, -1, g_text_rows, true); }
static void act_select_page_down(editor_t *ed) { cursor_page(ed, 1, g_text_rows, true); }

static void act_select_all(editor_t *ed) { cursor_select_all(ed); }
static void act_deselect(editor_t *ed)   { cursor_deselect(ed); }

static void act_insert_newline(editor_t *ed) { editor_insert_newline(ed); }
static void act_insert_tab(editor_t *ed)     { editor_insert_tab(ed); }
static void act_backspace(editor_t *ed)      { editor_backspace(ed); }
static void act_delete(editor_t *ed)         { editor_delete(ed); }

static void act_delete_word_left(editor_t *ed)
{
    if (ed->active_view && ed->active_view->buf && ed->active_view->buf->readonly) {
        editor_set_message(ed, "Buffer is read-only");
        return;
    }
    if (ed->sel.active) {
        selection_delete(ed);
        return;
    }
    size_t orig_line = ed->cx_line;
    size_t orig_col  = ed->cx_col;
    cursor_word_left(ed, false);
    if (ed->cx_line == orig_line && ed->cx_col < orig_col) {
        size_t runes = orig_col - ed->cx_col;
        const char *l_str = ed->doc.lines[orig_line];
        size_t l_len = ed->doc.lens[orig_line];
        size_t b_start = edit_rune_to_byte(l_str, l_len, ed->cx_col);
        size_t b_end   = edit_rune_to_byte(l_str, l_len, orig_col);
        undo_record_remove(ed, orig_line, ed->cx_col, l_str + b_start, b_end - b_start);
        undo_clear_redo(ed);
        editor_raw_remove(ed, orig_line, ed->cx_col, runes);
        ed->dirty = true;
    }
}

static void copy_cursor_text(const editor_t *ed, tam_str_t *out)
{
    if (ed->sel.active) {
        size_t len = 0;
        char *text = selection_get_text(ed, &len);
        if (text) {
            *out = tam_str_cat_len(*out, text, len);
            free(text);
        }
    } else if (ed->cx_line < ed->doc.count) {
        *out = tam_str_cat_len(*out, ed->doc.lines[ed->cx_line],
                               ed->doc.lens[ed->cx_line]);
        *out = tam_str_cat(*out, ed->crlf ? "\r\n" : "\n");
    }
}

static void act_copy(editor_t *ed)
{
    if (ed->extra_count > 0) {
        tam_str_t joined = tam_str_new("");
        if (!joined) return;
        copy_cursor_text(ed, &joined);
        editor_t tmp = *ed;
        tmp.extra_count = 0;
        for (size_t i = 0; i < ed->extra_count; i++) {
            joined = tam_str_cat(joined, ed->crlf ? "\r\n" : "\n");
            tmp.cx_line = ed->extra_cursors[i].cx_line;
            tmp.cx_col = ed->extra_cursors[i].cx_col;
            tmp.sel = ed->extra_cursors[i].sel;
            copy_cursor_text(&tmp, &joined);
        }
        clipboard_set(joined, tam_str_len(joined), ed->tty_out_fd);
        tam_str_free(joined);
        editor_set_message(ed, "Copied multi-cursor selection");
        return;
    }

    if (ed->sel.active) {
        size_t len = 0;
        char *text = selection_get_text(ed, &len);
        if (text != NULL) {
            clipboard_set(text, len, ed->tty_out_fd);
            free(text);
            editor_set_message(ed, "Copied selection");
        }
    } else {
        if (ed->cx_line < ed->doc.count) {
            tam_str_t s = tam_str_new_len(ed->doc.lines[ed->cx_line],
                                          ed->doc.lens[ed->cx_line]);
            if (s != NULL) {
                s = tam_str_cat(s, ed->crlf ? "\r\n" : "\n");
                clipboard_set(s, tam_str_len(s), ed->tty_out_fd);
                tam_str_free(s);
                editor_set_message(ed, "Copied line");
            }
        }
    }
}

static void act_cut(editor_t *ed)
{
    if (ed->active_view && ed->active_view->buf && ed->active_view->buf->readonly) {
        editor_set_message(ed, "Buffer is read-only");
        return;
    }
    if (ed->extra_count > 0) {
        act_copy(ed);
        if (ed->sel.active) {
            editor_backspace(ed);
            editor_set_message(ed, "Cut multi-cursor selection");
        }
        return;
    }
    if (ed->sel.active) {
        act_copy(ed);
        selection_delete(ed);
        editor_set_message(ed, "Cut selection");
    } else {
        if (ed->cx_line < ed->doc.count) {
            act_copy(ed);
            editor_delete_line(ed);
            editor_set_message(ed, "Cut line");
        }
    }
}

static void act_paste(editor_t *ed)
{
    size_t len = 0;
    char *text = clipboard_get(&len);
    if (text != NULL && len > 0) {
        editor_insert_text(ed, text, len);
        free(text);
        editor_set_message(ed, "Pasted");
    } else {
        free(text);
    }
}

static void act_duplicate_line(editor_t *ed) { editor_duplicate(ed); }
static void act_delete_line(editor_t *ed)    { editor_delete_line(ed); }
static void act_move_lines_up(editor_t *ed)  { editor_move_lines(ed, -1); }
static void act_move_lines_down(editor_t *ed){ editor_move_lines(ed, 1); }

static void act_undo(editor_t *ed) { editor_undo(ed); }
static void act_redo(editor_t *ed) { editor_redo(ed); }

static void act_save(editor_t *ed)
{
    if (ed->path == NULL || ed->path[0] == '\0') {
        prompt_open_save_as(ed);
    } else {
        char err[192] = "";
        editor_save_configured(ed, err, sizeof(err));
    }
}

static void act_save_as(editor_t *ed)
{
    prompt_open_save_as(ed);
}

static void act_open_file(editor_t *ed)
{
    prompt_open_command_prefill(ed, "open ");
}

static void act_quit(editor_t *ed)
{
    if (ed->dirty) {
        if (!ed->quit_armed) {
            editor_set_message(ed,
                "Unsaved changes: Ctrl-Q again to quit, Ctrl-S to save");
            ed->quit_armed = true;
            return;
        }
    }
    ed->running = false;
}

static void act_quit_all(editor_t *ed)
{
    bool any_dirty = false;
    for (size_t i = 0; i < ed->tabs.count; i++) {
        if (tab_is_dirty(ed->tabs.tabs[i])) any_dirty = true;
    }
    if (any_dirty || ed->dirty) {
        prompt_open_quit_confirm(ed);
        return;
    }
    ed->running = false;
}

static void act_command_mode(editor_t *ed) { prompt_open_command(ed); }
static void act_goto(editor_t *ed)         { prompt_open_goto(ed); }
static void act_find(editor_t *ed)         { prompt_open_find(ed); }
static void act_find_literal(editor_t *ed) { prompt_open_find_literal(ed); }
static void act_find_next(editor_t *ed)    { search_find_next(ed); }
static void act_find_prev(editor_t *ed)    { search_find_prev(ed); }
static void act_run_shell(editor_t *ed)    { prompt_open_shell(ed); }
static void act_shell_mode(editor_t *ed)
{
    if (!ed->tty) {
        prompt_open_shell(ed);
        return;
    }
    int rc = shell_run_interactive(ed->tty, NULL, false, true);
    editor_set_message(ed, "Shell exited (%d)", rc);
}
static bool is_help_view(const view_t *v)
{
    return v != NULL && v->buf != NULL && v->buf->scratch &&
           v->buf->path != NULL && strncmp(v->buf->path, "help:", 5) == 0;
}

static bool close_help_view(editor_t *ed, view_t *v)
{
    if (!ed || !is_help_view(v) || ed->tabs.count == 0) return false;
    tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
    pane_node_t *node = pane_find_view(cur_tab->root, v);
    if (node == NULL) return false;
    if (node != cur_tab->root) {
        if (v == ed->active_view) {
            view_t *next_v = pane_next_view(cur_tab->root, v);
            if (next_v != NULL && next_v != v) {
                editor_set_active_view(ed, next_v);
            } else {
                ed->active_view = NULL;
            }
        }
        pane_close(cur_tab->root, node, &cur_tab->root);
        return true;
    }
    if (ed->tabs.count > 1) {
        return editor_close_tab(ed);
    }
    return false;
}

static view_t *find_help_view_in_node(pane_node_t *n)
{
    if (!n) return NULL;
    if (n->view != NULL) {
        return is_help_view(n->view) ? n->view : NULL;
    }
    view_t *c1 = find_help_view_in_node(n->child1);
    return c1 ? c1 : find_help_view_in_node(n->child2);
}

static void act_toggle_help(editor_t *ed)
{
    if (is_help_view(ed->active_view)) {
        if (close_help_view(ed, ed->active_view)) return;
    }
    if (ed->tabs.count > 0 && ed->tabs.active < ed->tabs.count) {
        view_t *hv = find_help_view_in_node(ed->tabs.tabs[ed->tabs.active]->root);
        if (hv != NULL && close_help_view(ed, hv)) return;
    }
    prompt_execute_command(ed, "help");
}
static void act_toggle_key_menu(editor_t *ed) { actions_open_keybindings_help(ed); }
static void act_toggle_overwrite(editor_t *ed)
{
    ed->config.overwrite = !ed->config.overwrite;
    editor_set_message(ed, "Overwrite mode %s", ed->config.overwrite ? "on" : "off");
}

static void act_spawn_cursor(editor_t *ed)        { editor_spawn_cursor_match(ed, false); }
static void act_skip_cursor(editor_t *ed)         { editor_spawn_cursor_match(ed, true); }
static void act_spawn_cursor_up(editor_t *ed)     { editor_spawn_cursor_dir(ed, -1); }
static void act_spawn_cursor_down(editor_t *ed)   { editor_spawn_cursor_dir(ed, 1); }
static void act_spawn_cursor_select(editor_t *ed) { editor_spawn_cursors_on_selection(ed); }
static void act_remove_cursor(editor_t *ed)       { editor_remove_last_cursor(ed); }
static void act_remove_all_cursors(editor_t *ed)  { editor_remove_all_cursors(ed); }

/* Milestone 2 Action implementations */
static void act_vsplit(editor_t *ed)          { editor_vsplit(ed, NULL); }
static void act_hsplit(editor_t *ed)          { editor_hsplit(ed, NULL); }
static void act_next_split(editor_t *ed)      { editor_next_split(ed); }
static void act_prev_split(editor_t *ed)      { editor_prev_split(ed); }
static void act_unsplit(editor_t *ed)         { editor_unsplit(ed); }
static void act_add_tab(editor_t *ed)         { editor_add_tab(ed, NULL); }
static void act_next_tab(editor_t *ed)        { editor_next_tab(ed); }
static void act_prev_tab(editor_t *ed)        { editor_prev_tab(ed); }
static void act_first_tab(editor_t *ed)
{
    if (ed->tabs.count > 0) {
        ed->tabs.active = 0;
        editor_set_active_view(ed, ed->tabs.tabs[0]->active_view);
    }
}
static void act_last_tab(editor_t *ed)
{
    if (ed->tabs.count > 0) {
        ed->tabs.active = ed->tabs.count - 1;
        editor_set_active_view(ed, ed->tabs.tabs[ed->tabs.count - 1]->active_view);
    }
}
static void act_toggle_softwrap(editor_t *ed)
{
    if (ed->active_view) {
        ed->active_view->softwrap = !ed->active_view->softwrap;
        editor_set_message(ed, "Softwrap toggled %s", ed->active_view->softwrap ? "on" : "off");
    }
}
static void act_toggle_ruler(editor_t *ed)
{
    ed->config.ruler = !ed->config.ruler;
    editor_set_message(ed, "Ruler toggled %s", ed->config.ruler ? "on" : "off");
}

/* -------------------------------------------------------------------------- */
/* Action Registry Table                                                      */
/* -------------------------------------------------------------------------- */

typedef struct {
    const char *name;
    action_fn   func;
} action_def_t;

static const action_def_t g_actions[] = {
    {"CursorUp", act_cursor_up},
    {"CursorDown", act_cursor_down},
    {"CursorLeft", act_cursor_left},
    {"CursorRight", act_cursor_right},
    {"SelectUp", act_select_up},
    {"SelectDown", act_select_down},
    {"SelectLeft", act_select_left},
    {"SelectRight", act_select_right},
    {"WordLeft", act_word_left},
    {"WordRight", act_word_right},
    {"SelectWordLeft", act_select_word_left},
    {"SelectWordRight", act_select_word_right},
    {"StartOfTextToggle", act_start_of_text_toggle},
    {"SelectToStartOfTextToggle", act_select_to_start_of_text},
    {"EndOfLine", act_end_of_line},
    {"SelectToEndOfLine", act_select_to_end_of_line},
    {"CursorStart", act_cursor_start},
    {"CursorEnd", act_cursor_end},
    {"SelectToStart", act_select_to_start},
    {"SelectToEnd", act_select_to_end},
    {"CursorPageUp", act_cursor_page_up},
    {"CursorPageDown", act_cursor_page_down},
    {"SelectPageUp", act_select_page_up},
    {"SelectPageDown", act_select_page_down},
    {"SelectAll", act_select_all},
    {"Deselect", act_deselect},
    {"InsertNewline", act_insert_newline},
    {"InsertTab", act_insert_tab},
    {"Backspace", act_backspace},
    {"Delete", act_delete},
    {"DeleteWordLeft", act_delete_word_left},
    {"Copy", act_copy},
    {"CopyLine", act_copy},
    {"Cut", act_cut},
    {"CutLine", act_cut},
    {"Paste", act_paste},
    {"DeleteLine", act_delete_line},
    {"MoveLinesUp", act_move_lines_up},
    {"MoveLinesDown", act_move_lines_down},
    {"Duplicate", act_duplicate_line},
    {"DuplicateLine", act_duplicate_line},
    {"Undo", act_undo},
    {"Redo", act_redo},
    {"Save", act_save},
    {"SaveAs", act_save_as},
    {"OpenFile", act_open_file},
    {"Quit", act_quit},
    {"QuitAll", act_quit_all},
    {"CommandMode", act_command_mode},
    {"Goto", act_goto},
    {"Find", act_find},
    {"FindLiteral", act_find_literal},
    {"FindNext", act_find_next},
    {"FindPrevious", act_find_prev},
    {"ShellMode", act_shell_mode},
    {"RunShell", act_run_shell},
    {"ToggleHelp", act_toggle_help},
    {"ToggleKeyMenu", act_toggle_key_menu},
    {"ToggleOverwriteMode", act_toggle_overwrite},

    /* Multi-cursor Actions */
    {"SpawnMultiCursor", act_spawn_cursor},
    {"SkipMultiCursor", act_skip_cursor},
    {"SpawnMultiCursorUp", act_spawn_cursor_up},
    {"SpawnMultiCursorDown", act_spawn_cursor_down},
    {"SpawnMultiCursorSelect", act_spawn_cursor_select},
    {"RemoveMultiCursor", act_remove_cursor},
    {"RemoveAllMultiCursors", act_remove_all_cursors},

    /* Milestone 2 Actions */
    {"VSplit", act_vsplit},
    {"HSplit", act_hsplit},
    {"NextSplit", act_next_split},
    {"PreviousSplit", act_prev_split},
    {"FirstSplit", act_next_split},
    {"LastSplit", act_prev_split},
    {"Unsplit", act_unsplit},
    {"AddTab", act_add_tab},
    {"NextTab", act_next_tab},
    {"PreviousTab", act_prev_tab},
    {"FirstTab", act_first_tab},
    {"LastTab", act_last_tab},
    {"ToggleSoftwrap", act_toggle_softwrap},
    {"ToggleRuler", act_toggle_ruler},
    {NULL, NULL}
};

void actions_init(void) {}
void actions_free(void) {}

bool actions_execute(editor_t *ed, const char *name)
{
    if (name == NULL || *name == '\0') return false;

    if (strncmp(name, "command-edit:", 13) == 0) {
        prompt_open_command_prefill(ed, name + 13);
        return true;
    }

    if (strncmp(name, "command:", 8) == 0) {
        const char *cmd = name + 8;
        if (strcmp(cmd, "togglelocal softwrap") == 0 ||
            strcmp(cmd, "toggle softwrap") == 0) {
            act_toggle_softwrap(ed);
            return true;
        }
        prompt_execute_command(ed, cmd);
        return true;
    }

    if (strcmp(name, "togglelocal softwrap") == 0 ||
        strcmp(name, "toggle softwrap") == 0) {
        act_toggle_softwrap(ed);
        return true;
    }

    /* Handle multi-action pipeline 'Action1|Action2' */
    const char *pipe = strchr(name, '|');
    if (pipe != NULL) {
        char first[64];
        size_t n = (size_t)(pipe - name);
        if (n >= sizeof(first)) n = sizeof(first) - 1;
        memcpy(first, name, n);
        first[n] = '\0';
        if (actions_execute(ed, first)) return true;
        return actions_execute(ed, pipe + 1);
    }

    if (strcmp(name, "CloseHelp") == 0) {
        return close_help_view(ed, ed->active_view);
    }

    if (strcmp(name, "RemoveAllMultiCursors") == 0) {
        if (ed->extra_count == 0) return false;
        editor_remove_all_cursors(ed);
        return true;
    }

    for (size_t i = 0; g_actions[i].name != NULL; i++) {
        if (strcmp(g_actions[i].name, name) == 0) {
            g_actions[i].func(ed);
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* Keybinding Lookup Table                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    tam_key_type_t type;
    uint32_t       codepoint;
    unsigned       mods;
    const char    *action;
} keybind_entry_t;

static const keybind_entry_t g_keybindings[] = {
    /* Arrow navigation */
    {TAM_KEY_UP, 0, 0, "CursorUp"},
    {TAM_KEY_DOWN, 0, 0, "CursorDown"},
    {TAM_KEY_LEFT, 0, 0, "CursorLeft"},
    {TAM_KEY_RIGHT, 0, 0, "CursorRight"},
    {TAM_KEY_UP, 0, TAM_MOD_SHIFT, "SelectUp"},
    {TAM_KEY_DOWN, 0, TAM_MOD_SHIFT, "SelectDown"},
    {TAM_KEY_LEFT, 0, TAM_MOD_SHIFT, "SelectLeft"},
    {TAM_KEY_RIGHT, 0, TAM_MOD_SHIFT, "SelectRight"},
    {TAM_KEY_LEFT, 0, TAM_MOD_CTRL, "WordLeft"},
    {TAM_KEY_RIGHT, 0, TAM_MOD_CTRL, "WordRight"},
    {TAM_KEY_LEFT, 0, TAM_MOD_CTRL | TAM_MOD_SHIFT, "SelectWordLeft"},
    {TAM_KEY_RIGHT, 0, TAM_MOD_CTRL | TAM_MOD_SHIFT, "SelectWordRight"},

    /* Home / End / Page */
    {TAM_KEY_HOME, 0, 0, "StartOfTextToggle"},
    {TAM_KEY_END, 0, 0, "EndOfLine"},
    {TAM_KEY_HOME, 0, TAM_MOD_SHIFT, "SelectToStartOfTextToggle"},
    {TAM_KEY_END, 0, TAM_MOD_SHIFT, "SelectToEndOfLine"},
    {TAM_KEY_HOME, 0, TAM_MOD_CTRL, "CursorStart"},
    {TAM_KEY_END, 0, TAM_MOD_CTRL, "CursorEnd"},
    {TAM_KEY_HOME, 0, TAM_MOD_CTRL | TAM_MOD_SHIFT, "SelectToStart"},
    {TAM_KEY_END, 0, TAM_MOD_CTRL | TAM_MOD_SHIFT, "SelectToEnd"},
    {TAM_KEY_PAGE_UP, 0, 0, "CursorPageUp"},
    {TAM_KEY_PAGE_DOWN, 0, 0, "CursorPageDown"},
    {TAM_KEY_PAGE_UP, 0, TAM_MOD_SHIFT, "SelectPageUp"},
    {TAM_KEY_PAGE_DOWN, 0, TAM_MOD_SHIFT, "SelectPageDown"},

    /* Tab navigation with Ctrl-PageUp/Down */
    {TAM_KEY_PAGE_UP, 0, TAM_MOD_CTRL, "PreviousTab|LastTab"},
    {TAM_KEY_PAGE_DOWN, 0, TAM_MOD_CTRL, "NextTab|FirstTab"},

    /* Basic edits */
    {TAM_KEY_ENTER, 0, 0, "InsertNewline"},
    {TAM_KEY_TAB, 0, 0, "InsertTab"},
    {TAM_KEY_BACKSPACE, 0, 0, "Backspace"},
    {TAM_KEY_DELETE, 0, 0, "Delete"},
    {TAM_KEY_INSERT, 0, 0, "ToggleOverwriteMode"},
    {TAM_KEY_BACKSPACE, 0, TAM_MOD_CTRL, "DeleteWordLeft"},
    {TAM_KEY_BACKSPACE, 0, TAM_MOD_ALT, "DeleteWordLeft"},

    /* Common Micro Ctrl keys */
    {TAM_KEY_CHAR, 's', TAM_MOD_CTRL, "Save"},
    {TAM_KEY_CHAR, 'o', TAM_MOD_CTRL, "OpenFile"},
    {TAM_KEY_CHAR, 'q', TAM_MOD_CTRL, "Quit"},
    {TAM_KEY_CHAR, 'z', TAM_MOD_CTRL, "Undo"},
    {TAM_KEY_CHAR, 'y', TAM_MOD_CTRL, "Redo"},
    {TAM_KEY_CHAR, 'c', TAM_MOD_CTRL, "Copy|CopyLine"},
    {TAM_KEY_CHAR, 'x', TAM_MOD_CTRL, "Cut|CutLine"},
    {TAM_KEY_CHAR, 'v', TAM_MOD_CTRL, "Paste"},
    {TAM_KEY_CHAR, 'a', TAM_MOD_CTRL, "SelectAll"},
    {TAM_KEY_CHAR, 'd', TAM_MOD_CTRL, "Duplicate|DuplicateLine"},
    {TAM_KEY_CHAR, 'e', TAM_MOD_CTRL, "CommandMode"},
    {TAM_KEY_CHAR, 'l', TAM_MOD_CTRL, "Goto"},
    {TAM_KEY_CHAR, 'f', TAM_MOD_CTRL, "Find"},
    {TAM_KEY_CHAR, 'n', TAM_MOD_CTRL, "FindNext"},
    {TAM_KEY_CHAR, 'p', TAM_MOD_CTRL, "FindPrevious"},
    {TAM_KEY_CHAR, 'k', TAM_MOD_CTRL, "CutLine"},
    {TAM_KEY_CHAR, 'b', TAM_MOD_CTRL, "ShellMode"},
    {TAM_KEY_CHAR, 'w', TAM_MOD_CTRL, "NextSplit|FirstSplit"},
    {TAM_KEY_CHAR, 't', TAM_MOD_CTRL, "AddTab"},
    {TAM_KEY_CHAR, 'r', TAM_MOD_CTRL, "ToggleRuler"},
    {TAM_KEY_CHAR, 'h', TAM_MOD_CTRL, "ToggleHelp"},

    /* Alt shortcuts */
    {TAM_KEY_CHAR, 'f', TAM_MOD_ALT, "FindLiteral"},
    {TAM_KEY_CHAR, 'w', TAM_MOD_ALT, "ToggleSoftwrap"},
    {TAM_KEY_CHAR, 'g', TAM_MOD_ALT, "ToggleKeyMenu"},
    {TAM_KEY_CHAR, '.', TAM_MOD_ALT, "NextTab|FirstTab"},
    {TAM_KEY_CHAR, ',', TAM_MOD_ALT, "PreviousTab|LastTab"},
    {TAM_KEY_CHAR, 'n', TAM_MOD_ALT, "SpawnMultiCursor"},
    {TAM_KEY_CHAR, 'm', TAM_MOD_ALT, "SpawnMultiCursorSelect"},
    {TAM_KEY_CHAR, 'x', TAM_MOD_ALT, "SkipMultiCursor"},
    {TAM_KEY_CHAR, 'p', TAM_MOD_ALT, "RemoveMultiCursor"},
    {TAM_KEY_CHAR, 'c', TAM_MOD_ALT, "RemoveAllMultiCursors"},
    {TAM_KEY_UP, 0, TAM_MOD_ALT | TAM_MOD_SHIFT, "SpawnMultiCursorUp"},
    {TAM_KEY_DOWN, 0, TAM_MOD_ALT | TAM_MOD_SHIFT, "SpawnMultiCursorDown"},
    {TAM_KEY_UP, 0, TAM_MOD_ALT, "MoveLinesUp"},
    {TAM_KEY_DOWN, 0, TAM_MOD_ALT, "MoveLinesDown"},

    /* Function keys */
    {TAM_KEY_F1, 0, 0, "ToggleHelp"},
    {TAM_KEY_F2, 0, 0, "Save"},
    {TAM_KEY_F3, 0, 0, "FindNext"},
    {TAM_KEY_F4, 0, 0, "Quit"},
    {TAM_KEY_F7, 0, 0, "Find"},
    {TAM_KEY_F10, 0, 0, "Quit"},

    /* Esc */
    {TAM_KEY_ESC, 0, 0, "CloseHelp|RemoveAllMultiCursors|Deselect"},

    {TAM_KEY_NONE, 0, 0, NULL}
};

const char *actions_lookup_key_description(const editor_t *ed, const tam_key_event_t *ev)
{
    if (!ed || !ev) return NULL;
    for (custom_keybind_t *ckb = ed->keybindings; ckb != NULL; ckb = ckb->next) {
        if (ckb->type == ev->type && ckb->mods == ev->mods) {
            if (ckb->type == TAM_KEY_CHAR && ckb->codepoint != 0) {
                if (tolower((unsigned char)ckb->codepoint) !=
                    tolower((unsigned char)ev->codepoint)) {
                    continue;
                }
            }
            return ckb->action;
        }
    }
    for (size_t i = 0; g_keybindings[i].action != NULL; i++) {
        const keybind_entry_t *kb = &g_keybindings[i];
        if (kb->type == ev->type && kb->mods == ev->mods) {
            if (kb->type == TAM_KEY_CHAR && kb->codepoint != 0) {
                if (tolower((unsigned char)kb->codepoint) !=
                    tolower((unsigned char)ev->codepoint)) {
                    continue;
                }
            }
            return kb->action;
        }
    }
    return NULL;
}

void actions_open_keybindings_help(editor_t *ed)
{
    if (!ed) return;
    tam_str_t out = tam_str_new("# tam-edit Keybindings\n\n");
    if (!out) return;

    if (ed->keybindings != NULL) {
        out = tam_str_cat(out, "## Custom Bindings (settings.ini)\n\n");
        for (custom_keybind_t *ckb = ed->keybindings; ckb != NULL; ckb = ckb->next) {
            char kbuf[64];
            keybind_format_string(ckb->type, ckb->codepoint, ckb->mods, kbuf, sizeof(kbuf));
            char line[256];
            snprintf(line, sizeof(line), "  %-20s -> %s\n", kbuf, ckb->action ? ckb->action : "");
            out = tam_str_cat(out, line);
        }
        out = tam_str_cat(out, "\n");
    }

    out = tam_str_cat(out, "## Default Keybindings\n\n");
    for (size_t i = 0; g_keybindings[i].action != NULL; i++) {
        char kbuf[64];
        keybind_format_string(g_keybindings[i].type, g_keybindings[i].codepoint,
                              g_keybindings[i].mods, kbuf, sizeof(kbuf));
        char line[256];
        snprintf(line, sizeof(line), "  %-20s -> %s\n", kbuf, g_keybindings[i].action);
        out = tam_str_cat(out, line);
    }

    out = tam_str_cat(out,
        "\n## Mouse Bindings\n\n"
        "  LeftClick            -> Move cursor\n"
        "  LeftDrag             -> Select text\n"
        "  WheelUp / WheelDown  -> Scroll view\n"
        "  Ctrl-LeftClick       -> Toggle multi-cursor\n");

    editor_open_scratch(ed, "help:keybindings", out, true);
    tam_str_free(out);
}

bool actions_handle_key(editor_t *ed, const tam_key_event_t *ev, int text_rows)
{
    g_text_rows = text_rows > 0 ? text_rows : 24;

    /* Reset quit confirmation on any key other than Ctrl-Q */
    bool was_armed = ed->quit_armed;
    if (ev->type != TAM_KEY_CHAR || !(ev->mods & TAM_MOD_CTRL) || ev->codepoint != 'q') {
        ed->quit_armed = false;
    }

    /* 1. Look up in custom user keybindings first */
    for (custom_keybind_t *ckb = ed->keybindings; ckb != NULL; ckb = ckb->next) {
        if (ckb->type == ev->type && ckb->mods == ev->mods) {
            if (ckb->type == TAM_KEY_CHAR && ckb->codepoint != 0) {
                if (tolower((unsigned char)ckb->codepoint) !=
                    tolower((unsigned char)ev->codepoint)) {
                    continue;
                }
            }
            if (actions_execute(ed, ckb->action)) {
                return true;
            }
        }
    }

    /* 2. Look up in default keybindings table */
    for (size_t i = 0; g_keybindings[i].action != NULL; i++) {
        const keybind_entry_t *kb = &g_keybindings[i];
        if (kb->type == ev->type && kb->mods == ev->mods) {
            if (kb->type == TAM_KEY_CHAR && kb->codepoint != 0) {
                if (tolower((unsigned char)kb->codepoint) !=
                    tolower((unsigned char)ev->codepoint)) {
                    continue;
                }
            }
            if (actions_execute(ed, kb->action)) {
                return true;
            }
        }
    }

    /* 3. Regular character insertion */
    if (ev->type == TAM_KEY_CHAR &&
        (ev->mods == 0 || ev->mods == TAM_MOD_SHIFT) &&
        ev->codepoint >= 0x20 && ev->codepoint != 0x7F) {
        editor_insert_codepoint(ed, ev->codepoint);
        return true;
    }

    /* 4. Handle lone Ctrl-Q when armed */
    if (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_CTRL) && ev->codepoint == 'q') {
        if (was_armed) {
            ed->running = false;
            return true;
        }
    }

    return false;
}
