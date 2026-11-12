#include "actions.h"
#include "clipboard.h"
#include "cursor.h"
#include "editor.h"
#include "prompt.h"
#include "render.h"
#include "undo.h"

#include <tam.h>
#include <tam_key.h>
#include <tam_uni.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__,      \
                    #cond);                                                  \
            abort();                                                         \
        }                                                                    \
    } while (0)

static void test_undo_redo_typing(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_codepoint(&ed, 'h');
    editor_insert_codepoint(&ed, 'i');
    CHECK(ed.doc.count == 1);
    CHECK(strcmp(ed.doc.lines[0], "hi") == 0);
    CHECK(ed.cx_col == 2);

    /* Undo coalesced typing */
    CHECK(editor_undo(&ed));
    CHECK(ed.doc.lens[0] == 0);
    CHECK(ed.cx_col == 0);

    /* Redo */
    CHECK(editor_redo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "hi") == 0);
    CHECK(ed.cx_col == 2);

    editor_free(&ed);
    printf("  [PASS] test_undo_redo_typing\n");
}

static void test_undo_redo_multiline(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_text(&ed, "line1\nline2", 11);
    CHECK(ed.doc.count == 2);
    CHECK(strcmp(ed.doc.lines[0], "line1") == 0);
    CHECK(strcmp(ed.doc.lines[1], "line2") == 0);

    CHECK(editor_undo(&ed));
    CHECK(ed.doc.count == 1);
    CHECK(ed.doc.lens[0] == 0);

    CHECK(editor_redo(&ed));
    CHECK(ed.doc.count == 2);
    CHECK(strcmp(ed.doc.lines[0], "line1") == 0);
    CHECK(strcmp(ed.doc.lines[1], "line2") == 0);

    editor_free(&ed);
    printf("  [PASS] test_undo_redo_multiline\n");
}

static void test_selection_operations(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_text(&ed, "hello world", 11);
    CHECK(ed.cx_col == 11);

    /* Select all */
    cursor_select_all(&ed);
    CHECK(ed.sel.active);
    loc_t s, e;
    CHECK(selection_get_range(&ed, &s, &e));
    CHECK(s.line == 0 && s.col == 0);
    CHECK(e.line == 0 && e.col == 11);

    size_t len = 0;
    char *text = selection_get_text(&ed, &len);
    CHECK(text != NULL && strcmp(text, "hello world") == 0 && len == 11);
    free(text);

    /* Replace selection by inserting a single character */
    editor_insert_codepoint(&ed, '!');
    CHECK(ed.doc.count == 1);
    CHECK(strcmp(ed.doc.lines[0], "!") == 0);
    CHECK(!ed.sel.active);

    /* Undo replacement */
    CHECK(editor_undo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "hello world") == 0);

    editor_free(&ed);
    printf("  [PASS] test_selection_operations\n");
}

static void test_word_navigation(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_text(&ed, "foo bar  baz_qux", 16);
    CHECK(ed.cx_col == 16);

    cursor_word_left(&ed, false);
    CHECK(ed.cx_col == 9); /* start of baz_qux */

    cursor_word_left(&ed, false);
    CHECK(ed.cx_col == 4); /* start of bar */

    cursor_word_left(&ed, false);
    CHECK(ed.cx_col == 0); /* start of foo */

    cursor_word_right(&ed, false);
    CHECK(ed.cx_col == 4); /* after foo and space, start of bar */

    cursor_word_right(&ed, false);
    CHECK(ed.cx_col == 9); /* start of baz_qux */

    editor_free(&ed);
    printf("  [PASS] test_word_navigation\n");
}

static void test_clipboard_operations(void)
{
    clipboard_init();

    const char *msg = "tam-edit clipboard test";
    CHECK(clipboard_set(msg, strlen(msg), -1));

    size_t out_len = 0;
    char *got = clipboard_get(&out_len);
    CHECK(got != NULL);
    CHECK(strcmp(got, msg) == 0);
    CHECK(out_len == strlen(msg));
    free(got);

    editor_t ed;
    editor_init(&ed);

    actions_execute(&ed, "Paste");
    CHECK(ed.doc.count == 1);
    CHECK(strcmp(ed.doc.lines[0], msg) == 0);

    editor_free(&ed);
    clipboard_free();
    printf("  [PASS] test_clipboard_operations\n");
}

static void test_actions_and_duplicate(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_text(&ed, "first line", 10);
    actions_execute(&ed, "DuplicateLine");
    CHECK(ed.doc.count == 2);
    CHECK(strcmp(ed.doc.lines[0], "first line") == 0);
    CHECK(strcmp(ed.doc.lines[1], "first line") == 0);

    actions_execute(&ed, "SelectAll");
    CHECK(ed.sel.active);
    actions_execute(&ed, "Cut");
    CHECK(ed.doc.count == 1);
    CHECK(ed.doc.lens[0] == 0);

    actions_execute(&ed, "Paste");
    CHECK(ed.doc.count == 2);
    CHECK(strcmp(ed.doc.lines[0], "first line") == 0);
    CHECK(strcmp(ed.doc.lines[1], "first line") == 0);

    editor_free(&ed);
    printf("  [PASS] test_actions_and_duplicate\n");
}

static void test_prompt_and_search(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_text(&ed, "hello world\nsecond line\nhello again", 35);
    ed.cx_line = 0;
    ed.cx_col = 0;

    /* Search for 'line' */
    snprintf(ed.search.query, sizeof(ed.search.query), "line");
    search_find_next(&ed);
    CHECK(ed.cx_line == 1);
    CHECK(ed.cx_col == 7);

    /* Command prompt: goto 3 */
    prompt_open_command(&ed);
    CHECK(ed.prompt.active);
    tam_key_event_t ev_g = {.type = TAM_KEY_CHAR, .codepoint = 'g'};
    prompt_handle_key(&ed, &ev_g);
    tam_key_event_t ev_o = {.type = TAM_KEY_CHAR, .codepoint = 'o'};
    prompt_handle_key(&ed, &ev_o);
    tam_key_event_t ev_t = {.type = TAM_KEY_CHAR, .codepoint = 't'};
    prompt_handle_key(&ed, &ev_t);
    tam_key_event_t ev_o2 = {.type = TAM_KEY_CHAR, .codepoint = 'o'};
    prompt_handle_key(&ed, &ev_o2);
    tam_key_event_t ev_sp = {.type = TAM_KEY_CHAR, .codepoint = ' '};
    prompt_handle_key(&ed, &ev_sp);
    tam_key_event_t ev_3 = {.type = TAM_KEY_CHAR, .codepoint = '3'};
    prompt_handle_key(&ed, &ev_3);
    tam_key_event_t ev_ent = {.type = TAM_KEY_ENTER};
    prompt_handle_key(&ed, &ev_ent);

    CHECK(!ed.prompt.active);
    CHECK(ed.cx_line == 2);

    editor_free(&ed);
    printf("  [PASS] test_prompt_and_search\n");
}

static void test_cell_grid_renderer(void)
{
    renderer_t ren;
    renderer_init(&ren);
    renderer_resize(&ren, 24, 80);

    CHECK(ren.front.rows == 24);
    CHECK(ren.front.cols == 80);
    CHECK(ren.back.rows == 24);
    CHECK(ren.back.cols == 80);

    editor_t ed;
    editor_init(&ed);
    editor_insert_text(&ed, "rendering test line", 19);

    /* Painting into grid check */
    CHECK(render_text_rows(&(tam_tty_t){.rows = 24}) == 22);

    renderer_free(&ren);
    editor_free(&ed);
    printf("  [PASS] test_cell_grid_renderer\n");
}

void test_milestone1_run(void)
{
    printf("tam-edit milestone 1 tests\n");
    test_undo_redo_typing();
    test_undo_redo_multiline();
    test_selection_operations();
    test_word_navigation();
    test_clipboard_operations();
    test_actions_and_duplicate();
    test_prompt_and_search();
    test_cell_grid_renderer();
    printf("milestone 1 tests passed\n");
}
