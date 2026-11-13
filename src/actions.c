#include "actions.h"
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

static void act_copy(editor_t *ed)
{
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
    if (ed->sel.active) {
        act_copy(ed);
        selection_delete(ed);
        editor_set_message(ed, "Cut selection");
    } else {
        if (ed->cx_line < ed->doc.count) {
            act_copy(ed);
            size_t runes = edit_line_runes(ed->doc.lines[ed->cx_line],
                                           ed->doc.lens[ed->cx_line]);
            undo_record_remove(ed, ed->cx_line, 0, ed->doc.lines[ed->cx_line],
                               ed->doc.lens[ed->cx_line]);
            undo_record_remove(ed, ed->cx_line, runes, "\n", 1);
            undo_clear_redo(ed);
            editor_raw_remove(ed, ed->cx_line, 0, runes);
            editor_raw_remove(ed, ed->cx_line, 0, 1);
            cursor_clamp(ed);
            ed->dirty = true;
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
    }
}

static void act_duplicate_line(editor_t *ed)
{
    if (ed->cx_line < ed->doc.count) {
        const char *cur = ed->doc.lines[ed->cx_line];
        size_t len = ed->doc.lens[ed->cx_line];
        tam_str_t s = tam_str_new("\n");
        s = tam_str_cat_len(s, cur, len);
        if (s != NULL) {
            size_t runes = edit_line_runes(cur, len);
            undo_record_insert(ed, ed->cx_line, runes, s, tam_str_len(s));
            undo_clear_redo(ed);
            editor_raw_insert(ed, ed->cx_line, runes, s, tam_str_len(s));
            ed->cx_line++;
            cursor_clamp(ed);
            ed->dirty = true;
            tam_str_free(s);
        }
    }
}

static void act_undo(editor_t *ed) { editor_undo(ed); }
static void act_redo(editor_t *ed) { editor_redo(ed); }

static void act_save(editor_t *ed)
{
    if (ed->path == NULL || ed->path[0] == '\0') {
        prompt_open_save_as(ed);
    } else {
        editor_save(ed);
    }
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

static void act_command_mode(editor_t *ed) { prompt_open_command(ed); }
static void act_goto(editor_t *ed)         { prompt_open_goto(ed); }
static void act_find(editor_t *ed)         { prompt_open_find(ed); }
static void act_find_next(editor_t *ed)    { search_find_next(ed); }
static void act_find_prev(editor_t *ed)    { search_find_prev(ed); }

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
    {"Duplicate", act_duplicate_line},
    {"DuplicateLine", act_duplicate_line},
    {"Undo", act_undo},
    {"Redo", act_redo},
    {"Save", act_save},
    {"Quit", act_quit},
    {"CommandMode", act_command_mode},
    {"Goto", act_goto},
    {"Find", act_find},
    {"FindNext", act_find_next},
    {"FindPrevious", act_find_prev},

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

    /* Handle command: prefix (from micro bindings) */
    if (strncmp(name, "command:", 8) == 0) {
        name += 8;
    }

    if (strncmp(name, "togglelocal softwrap", 20) == 0 ||
        strncmp(name, "toggle softwrap", 15) == 0) {
        act_toggle_softwrap(ed);
        return true;
    }

    /* Handle multi-action pipeline 'Action1|Action2' */
    const char *pipe = strchr(name, '|');
    if (pipe != NULL) {
        char first[64];
        size_t n = pipe - name;
        if (n >= sizeof(first)) n = sizeof(first) - 1;
        memcpy(first, name, n);
        first[n] = '\0';
        if (actions_execute(ed, first)) return true;
        return actions_execute(ed, pipe + 1);
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
    {TAM_KEY_BACKSPACE, 0, TAM_MOD_CTRL, "DeleteWordLeft"},
    {TAM_KEY_BACKSPACE, 0, TAM_MOD_ALT, "DeleteWordLeft"},

    /* Common Micro Ctrl keys */
    {TAM_KEY_CHAR, 's', TAM_MOD_CTRL, "Save"},
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
    {TAM_KEY_CHAR, 'w', TAM_MOD_CTRL, "NextSplit|FirstSplit"},
    {TAM_KEY_CHAR, 't', TAM_MOD_CTRL, "AddTab"},
    {TAM_KEY_CHAR, 'r', TAM_MOD_CTRL, "ToggleRuler"},

    /* Alt shortcuts */
    {TAM_KEY_CHAR, 'w', TAM_MOD_ALT, "ToggleSoftwrap"},
    {TAM_KEY_CHAR, '.', TAM_MOD_ALT, "NextTab|FirstTab"},
    {TAM_KEY_CHAR, ',', TAM_MOD_ALT, "PreviousTab|LastTab"},

    /* Function keys */
    {TAM_KEY_F2, 0, 0, "Save"},
    {TAM_KEY_F3, 0, 0, "FindNext"},
    {TAM_KEY_F4, 0, 0, "Quit"},
    {TAM_KEY_F7, 0, 0, "Find"},
    {TAM_KEY_F10, 0, 0, "Quit"},

    /* Esc */
    {TAM_KEY_ESC, 0, 0, "Deselect"},

    {TAM_KEY_NONE, 0, 0, NULL}
};

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
