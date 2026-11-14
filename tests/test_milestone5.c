#include "actions.h"
#include "buffer.h"
#include "cursor.h"
#include "editor.h"
#include "pane.h"
#include "ported_buffer_fixtures.h"
#include "render.h"
#include "syntax.h"
#include "undo.h"

#include <tam.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M5_CHECK(cond) do {                                             \
    if (!(cond)) {                                                      \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1);                                                        \
    }                                                                   \
} while (0)

static char *doc_join_lines(const editor_t *ed)
{
    size_t total = 0;
    for (size_t i = 0; i < ed->doc.count; i++) {
        total += ed->doc.lens[i] + (i + 1 < ed->doc.count ? 1 : 0);
    }
    char *buf = malloc(total + 1);
    M5_CHECK(buf != NULL);
    size_t pos = 0;
    for (size_t i = 0; i < ed->doc.count; i++) {
        if (ed->doc.lens[i] > 0) {
            memcpy(buf + pos, ed->doc.lines[i], ed->doc.lens[i]);
            pos += ed->doc.lens[i];
        }
        if (i + 1 < ed->doc.count) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    return buf;
}

static void load_text_into_editor(editor_t *ed, const char *text)
{
    ed->config.autoindent = false;
    ed->config.smartpaste = false;
    ed->config.tabstospaces = false;
    if (text && text[0] != '\0') {
        editor_raw_insert(ed, 0, 0, text, strlen(text));
    }
    undo_clear_stack(&ed->undo_stack);
    undo_clear_stack(&ed->redo_stack);
    ed->dirty = false;
}

typedef struct {
    ported_op_t op;
    size_t      idx;
} indexed_op_t;

static int cmp_op_desc(const void *a, const void *b)
{
    const indexed_op_t *ia = (const indexed_op_t *)a;
    const indexed_op_t *ib = (const indexed_op_t *)b;
    if (ia->op.start_line != ib->op.start_line) {
        return (ia->op.start_line > ib->op.start_line) ? -1 : 1;
    }
    if (ia->op.start_col != ib->op.start_col) {
        return (ia->op.start_col > ib->op.start_col) ? -1 : 1;
    }
    return (ia->idx > ib->idx) ? -1 : 1;
}

static void test_ported_buffer_corpus(void)
{
    for (size_t i = 0; i < PORTED_CASE_COUNT; i++) {
        const ported_case_t *tc = &k_ported_cases[i];
        editor_t ed;
        editor_init(&ed);
        load_text_into_editor(&ed, tc->before);

        char *initial = doc_join_lines(&ed);
        M5_CHECK(strcmp(initial, tc->before) == 0);
        free(initial);

        indexed_op_t *sorted = malloc(tc->op_count * sizeof(indexed_op_t));
        M5_CHECK(sorted != NULL);
        for (size_t j = 0; j < tc->op_count; j++) {
            sorted[j].op = tc->ops[j];
            sorted[j].idx = j;
        }
        qsort(sorted, tc->op_count, sizeof(indexed_op_t), cmp_op_desc);

        for (size_t j = 0; j < tc->op_count; j++) {
            const ported_op_t *op = &sorted[j].op;
            undo_begin_group(&ed);
            if (op->start_line != op->end_line || op->start_col != op->end_col) {
                ed.sel.active = true;
                ed.sel.anchor.line = op->start_line;
                ed.sel.anchor.col  = op->start_col;
                ed.sel.cursor.line = op->end_line;
                ed.sel.cursor.col  = op->end_col;
                ed.cx_line = op->end_line;
                ed.cx_col  = op->end_col;
                selection_delete(&ed);
            } else {
                ed.sel.active = false;
                ed.cx_line = op->start_line;
                ed.cx_col  = op->start_col;
            }
            if (op->text && op->text[0] != '\0') {
                editor_insert_text(&ed, op->text, strlen(op->text));
            }
            undo_end_group(&ed);
        }

        char *actual = doc_join_lines(&ed);
        if (strcmp(actual, tc->after) != 0) {
            fprintf(stderr, "Ported case %zu mismatch!\nExpected:\n%s\nActual:\n%s\n",
                    i, tc->after, actual);
            free(actual);
            free(sorted);
            editor_free(&ed);
            exit(1);
        }
        free(actual);

        while (editor_undo(&ed)) {
        }
        char *restored = doc_join_lines(&ed);
        M5_CHECK(strcmp(restored, tc->before) == 0);
        free(restored);

        free(sorted);
        editor_free(&ed);
    }
    printf("  [PASS] test_ported_buffer_corpus (%d cases)\n", PORTED_CASE_COUNT);
}

static void test_stress_large_file(void)
{
    editor_t ed;
    editor_init(&ed);
    ed.config.autoindent = false;

    const size_t n_lines = 50000;
    tam_str_t big = tam_str_new("");
    for (size_t i = 0; i < n_lines; i++) {
        char line[96];
        snprintf(line, sizeof(line), "int var_%zu = %zu; /* line %zu */\n", i, i * 2, i);
        big = tam_str_cat(big, line);
    }
    editor_insert_text(&ed, big, tam_str_len(big));
    tam_str_free(big);
    M5_CHECK(ed.doc.count >= n_lines);

    if (ed.active_view && ed.active_view->buf) {
        ed.active_view->buf->syntax_enabled = true;
        ed.active_view->buf->syntax_def = syntax_detect(
            "stress.c", NULL, (const char *const *)ed.doc.lines,
            ed.doc.lens, ed.doc.count, 100);
        M5_CHECK(ed.active_view->buf->syntax_def != NULL);
        buffer_syntax_update_to(ed.active_view->buf, 40);
    }

    ed.cx_line = 0; ed.cx_col = 0;
    editor_insert_text(&ed, "/* top */\n", 10);
    ed.cx_line = n_lines / 2; ed.cx_col = 0;
    editor_insert_text(&ed, "/* mid */\n", 10);
    ed.cx_line = ed.doc.count - 1; ed.cx_col = 0;
    editor_insert_text(&ed, "/* end */", 9);

    renderer_t ren;
    renderer_init(&ren);
    renderer_resize(&ren, 40, 120);
    tam_tty_t tty = {.rows = 40, .cols = 120, .out_fd = -1};
    render_draw(&ren, &ed, &tty);
    renderer_free(&ren);

    M5_CHECK(editor_undo(&ed));
    M5_CHECK(editor_redo(&ed));

    editor_free(&ed);
    printf("  [PASS] test_stress_large_file (%zu lines)\n", n_lines);
}

static void test_stress_long_line(void)
{
    editor_t ed;
    editor_init(&ed);
    ed.config.softwrap = true;
    ed.config.wordwrap = true;

    tam_str_t line = tam_str_new("");
    for (int i = 0; i < 1000; i++) {
        line = tam_str_cat(line, "alpha_βγδ_omega ");
    }
    editor_insert_text(&ed, line, tam_str_len(line));
    M5_CHECK(ed.doc.lens[0] > 18000);

    size_t sublines = edit_softwrap_line_sublines(
        ed.doc.lines[0], ed.doc.lens[0], 80, 4, true);
    M5_CHECK(sublines >= 200);

    renderer_t ren;
    renderer_init(&ren);
    renderer_resize(&ren, 24, 80);
    tam_tty_t tty = {.rows = 24, .cols = 80, .out_fd = -1};
    render_draw(&ren, &ed, &tty);
    renderer_free(&ren);

    tam_str_free(line);
    editor_free(&ed);
    printf("  [PASS] test_stress_long_line\n");
}

static void test_stress_many_cursors(void)
{
    editor_t ed;
    editor_init(&ed);
    ed.config.autoindent = false;

    const size_t n = 220;
    tam_str_t text = tam_str_new("");
    for (size_t i = 0; i < n; i++) {
        text = tam_str_cat(text, "item_\n");
    }
    editor_insert_text(&ed, text, tam_str_len(text));
    tam_str_free(text);
    undo_clear_stack(&ed.undo_stack);
    undo_clear_stack(&ed.redo_stack);

    ed.cx_line = 0;
    ed.cx_col = 5;
    for (size_t i = 1; i < n; i++) {
        editor_toggle_cursor_at(&ed, i, 5);
    }
    M5_CHECK(ed.extra_count == n - 1);

    editor_insert_text(&ed, "OK", 2);
    for (size_t i = 0; i < n; i++) {
        M5_CHECK(strcmp(ed.doc.lines[i], "item_OK") == 0);
    }

    editor_backspace(&ed);
    for (size_t i = 0; i < n; i++) {
        M5_CHECK(strcmp(ed.doc.lines[i], "item_O") == 0);
    }

    editor_remove_all_cursors(&ed);
    M5_CHECK(ed.extra_count == 0);

    editor_free(&ed);
    printf("  [PASS] test_stress_many_cursors (%zu cursors)\n", n);
}

static void test_stress_many_splits(void)
{
    editor_t ed;
    editor_init(&ed);

    for (int t = 0; t < 3; t++) {
        editor_add_tab(&ed, NULL);
    }
    M5_CHECK(ed.tabs.count == 4);

    for (int s = 0; s < 15; s++) {
        if (s % 2 == 0) {
            M5_CHECK(editor_vsplit(&ed, NULL));
        } else {
            M5_CHECK(editor_hsplit(&ed, NULL));
        }
    }

    renderer_t ren;
    renderer_init(&ren);
    renderer_resize(&ren, 48, 160);
    tam_tty_t tty = {.rows = 48, .cols = 160, .out_fd = -1};
    render_draw(&ren, &ed, &tty);
    renderer_free(&ren);

    editor_free(&ed);
    printf("  [PASS] test_stress_many_splits (16 splits x 4 tabs)\n");
}

static void test_parity_ctrl_g_help(void)
{
    editor_t ed;
    editor_init(&ed);

    /* Ctrl-h opens help, pressing Ctrl-h again closes it */
    tam_key_event_t ev = {0};
    ev.type = TAM_KEY_CHAR;
    ev.codepoint = 'h';
    ev.mods = TAM_MOD_CTRL;
    M5_CHECK(actions_handle_key(&ed, &ev, 24));
    M5_CHECK(ed.active_view != NULL && ed.active_view->buf != NULL && ed.active_view->buf->scratch);

    M5_CHECK(actions_handle_key(&ed, &ev, 24));
    M5_CHECK(ed.active_view != NULL && ed.active_view->buf != NULL && !ed.active_view->buf->scratch);

    /* Ctrl-h opens help, Esc closes it */
    M5_CHECK(actions_handle_key(&ed, &ev, 24));
    M5_CHECK(ed.active_view != NULL && ed.active_view->buf != NULL && ed.active_view->buf->scratch);
    tam_key_event_t esc = {.type = TAM_KEY_ESC, .codepoint = 0, .mods = 0};
    M5_CHECK(actions_handle_key(&ed, &esc, 24));
    M5_CHECK(ed.active_view != NULL && ed.active_view->buf != NULL && !ed.active_view->buf->scratch);

    /* Alt-g opens live keybindings help listing all bindings */
    ev.codepoint = 'g';
    ev.mods = TAM_MOD_ALT;
    M5_CHECK(actions_handle_key(&ed, &ev, 24));
    M5_CHECK(ed.active_view != NULL && ed.active_view->buf != NULL && ed.active_view->buf->scratch);
    bool found_multi = false;
    for (size_t i = 0; i < ed.doc.count; i++) {
        if (strstr(ed.doc.lines[i], "SpawnMultiCursor") != NULL) {
            found_multi = true;
            break;
        }
    }
    M5_CHECK(found_multi);

    /* Verify second footer row (row 23 of 24) renders keybindings bar */
    renderer_t ren;
    renderer_init(&ren);
    renderer_resize(&ren, 24, 80);
    tam_tty_t tty = {.rows = 24, .cols = 80, .out_fd = -1};
    render_draw(&ren, &ed, &tty);
    char row23[81];
    for (int c = 0; c < 80; c++) {
        uint32_t cp = ren.back.cells[23 * 80 + c].codepoint;
        row23[c] = (cp >= 0x20 && cp < 0x7f) ? (char)cp : '?';
    }
    row23[80] = '\0';
    M5_CHECK(strstr(row23, "^S Save") != NULL);
    M5_CHECK(strstr(row23, "M-G Keys") != NULL);
    renderer_free(&ren);

    editor_free(&ed);
    printf("  [PASS] test_parity_ctrl_g_help\n");
}

int run_milestone5_tests(void)
{
    printf("\n=== Milestone 5: Stabilization, Ported Corpus & Stress Tests ===\n");
    test_ported_buffer_corpus();
    test_stress_large_file();
    test_stress_long_line();
    test_stress_many_cursors();
    test_stress_many_splits();
    test_parity_ctrl_g_help();
    printf("All Milestone 5 tests passed.\n");
    return 0;
}
