#include "editor.h"

#include <tam.h>
#include <tam_uni.h>

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__,      \
                    #cond);                                                  \
            abort();                                                         \
        }                                                                    \
    } while (0)

static char *test_path(const char *name)
{
    static char buf[512];
    const char *dir = getenv("TMPDIR");
    if (dir == NULL || dir[0] == '\0') dir = "/tmp";
    snprintf(buf, sizeof(buf), "%s/tam-edit-test-%s-%ld", dir, name,
             (long)getpid());
    return buf;
}

static void write_file(const char *path, const char *data)
{
    CHECK(tam_fs_write_atomic(path, data, strlen(data), 0600));
}

static void check_contents(editor_t *ed, const char *expected)
{
    tam_str_t text = tam_str_new("");
    CHECK(text != NULL);
    for (size_t i = 0; i < ed->doc.count; i++) {
        if (i > 0) text = tam_str_cat(text, ed->crlf ? "\r\n" : "\n");
        text = tam_str_cat_len(text, ed->doc.lines[i], ed->doc.lens[i]);
    }
    CHECK(strcmp(text, expected) == 0);
    tam_str_free(text);
}

static void test_load_split(void)
{
    const char *path = test_path("split");
    write_file(path, "one\ntwo\n");

    editor_t ed;
    editor_init(&ed);
    CHECK(editor_load(&ed, path));
    CHECK(ed.doc.count == 3);
    CHECK(strcmp(ed.doc.lines[0], "one") == 0);
    CHECK(strcmp(ed.doc.lines[1], "two") == 0);
    CHECK(ed.doc.lens[2] == 0);
    CHECK(!ed.dirty);
    check_contents(&ed, "one\ntwo\n");

    CHECK(editor_save(&ed));
    size_t len = 0;
    char *data = tam_fs_read_file(path, &len);
    CHECK(data != NULL && len == 8 && memcmp(data, "one\ntwo\n", 8) == 0);
    free(data);

    editor_free(&ed);
    unlink(path);
    printf("  [PASS] test_load_split\n");
}

static void test_no_trailing_newline(void)
{
    const char *path = test_path("noeol");
    write_file(path, "abc");

    editor_t ed;
    editor_init(&ed);
    CHECK(editor_load(&ed, path));
    CHECK(ed.doc.count == 1);
    CHECK(editor_save(&ed));

    size_t len = 0;
    char *data = tam_fs_read_file(path, &len);
    CHECK(data != NULL && len == 3 && memcmp(data, "abc", 3) == 0);
    free(data);

    editor_free(&ed);
    unlink(path);
    printf("  [PASS] test_no_trailing_newline\n");
}

static void test_crlf(void)
{
    const char *path = test_path("crlf");
    write_file(path, "a\r\nb\r\n");

    editor_t ed;
    editor_init(&ed);
    CHECK(editor_load(&ed, path));
    CHECK(ed.crlf);
    CHECK(ed.doc.count == 3);
    CHECK(strcmp(ed.doc.lines[0], "a") == 0);
    CHECK(strcmp(ed.doc.lines[1], "b") == 0);

    CHECK(editor_save(&ed));
    size_t len = 0;
    char *data = tam_fs_read_file(path, &len);
    CHECK(data != NULL && len == 6 && memcmp(data, "a\r\nb\r\n", 6) == 0);
    free(data);

    editor_free(&ed);
    unlink(path);
    printf("  [PASS] test_crlf\n");
}

static void test_missing_file(void)
{
    const char *path = test_path("missing");
    unlink(path);

    editor_t ed;
    editor_init(&ed);
    CHECK(editor_load(&ed, path));
    CHECK(ed.doc.count == 1);
    CHECK(ed.doc.lens[0] == 0);
    CHECK(!ed.dirty);
    CHECK(ed.path != NULL && strcmp(ed.path, path) == 0);

    editor_free(&ed);
    printf("  [PASS] test_missing_file\n");
}

static void test_insert_and_backspace(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_codepoint(&ed, 'h');
    editor_insert_codepoint(&ed, 'i');
    CHECK(ed.cx_col == 2);
    CHECK(ed.doc.lens[0] == 2);
    CHECK(ed.dirty);

    editor_backspace(&ed);
    CHECK(ed.cx_col == 1);
    CHECK(ed.doc.lens[0] == 1);
    CHECK(memcmp(ed.doc.lines[0], "h", 1) == 0);

    editor_backspace(&ed);
    CHECK(ed.cx_col == 0);
    CHECK(ed.doc.lens[0] == 0);
    CHECK(ed.doc.count == 1);

    /* Backspace at the start of the first line is a no-op. */
    editor_backspace(&ed);
    CHECK(ed.doc.count == 1);

    editor_free(&ed);
    printf("  [PASS] test_insert_and_backspace\n");
}

static void test_utf8_edit(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_codepoint(&ed, 0x00E9); /* e acute, two bytes */
    CHECK(ed.cx_col == 1);
    CHECK(ed.doc.lens[0] == 2);
    CHECK(edit_line_runes(ed.doc.lines[0], ed.doc.lens[0]) == 1);

    editor_insert_codepoint(&ed, 0x4E2D); /* CJK, three bytes, two cells */
    CHECK(ed.cx_col == 2);
    CHECK(ed.doc.lens[0] == 5);
    CHECK(edit_visual_col(ed.doc.lines[0], ed.doc.lens[0], 2) == 3);

    editor_backspace(&ed);
    CHECK(ed.cx_col == 1);
    CHECK(ed.doc.lens[0] == 2);
    CHECK(edit_visual_col(ed.doc.lines[0], ed.doc.lens[0], 1) == 1);

    editor_backspace(&ed);
    CHECK(ed.cx_col == 0);
    CHECK(ed.doc.lens[0] == 0);

    editor_free(&ed);
    printf("  [PASS] test_utf8_edit\n");
}

static void test_newline_and_join(void)
{
    editor_t ed;
    editor_init(&ed);

    editor_insert_codepoint(&ed, 'a');
    editor_insert_codepoint(&ed, 'b');
    editor_insert_codepoint(&ed, 'c');
    editor_insert_codepoint(&ed, 'd');
    editor_home(&ed);
    editor_move(&ed, 2, 0);
    editor_insert_newline(&ed);

    CHECK(ed.doc.count == 2);
    CHECK(strcmp(ed.doc.lines[0], "ab") == 0);
    CHECK(strcmp(ed.doc.lines[1], "cd") == 0);
    CHECK(ed.cx_line == 1 && ed.cx_col == 0);

    editor_move(&ed, 0, -1);
    editor_end(&ed);
    editor_delete(&ed);
    CHECK(ed.doc.count == 1);
    CHECK(strcmp(ed.doc.lines[0], "abcd") == 0);

    editor_free(&ed);
    printf("  [PASS] test_newline_and_join\n");
}

static void test_visual_columns(void)
{
    const char *line = "a\tb";
    size_t len = strlen(line);

    CHECK(edit_line_runes(line, len) == 3);
    CHECK(edit_visual_col(line, len, 0) == 0);
    CHECK(edit_visual_col(line, len, 1) == 1);
    CHECK(edit_visual_col(line, len, 2) == 8);
    CHECK(edit_visual_col(line, len, 3) == 9);

    CHECK(edit_visual_to_rune(line, len, 0) == 0);
    CHECK(edit_visual_to_rune(line, len, 1) == 1);
    CHECK(edit_visual_to_rune(line, len, 8) == 2);
    CHECK(edit_visual_to_rune(line, len, 9) == 3);

    CHECK(edit_rune_to_byte(line, len, 0) == 0);
    CHECK(edit_rune_to_byte(line, len, 1) == 1);
    CHECK(edit_rune_to_byte(line, len, 2) == 2);
    CHECK(edit_rune_to_byte(line, len, 3) == 3);

    printf("  [PASS] test_visual_columns\n");
}

static void test_vertical_goal(void)
{
    const char *path = test_path("goal");
    write_file(path, "long line here\nx\nanother long line\n");

    editor_t ed;
    editor_init(&ed);
    CHECK(editor_load(&ed, path));

    editor_move(&ed, 10, 0);
    CHECK(ed.cx_col == 10);

    editor_move(&ed, 0, 1);
    CHECK(ed.cx_line == 1);
    CHECK(ed.cx_col == 1);

    editor_move(&ed, 0, 1);
    CHECK(ed.cx_line == 2);
    CHECK(ed.cx_col == 10);

    editor_move(&ed, 0, -2);
    CHECK(ed.cx_line == 0);
    CHECK(ed.cx_col == 10);

    editor_free(&ed);
    unlink(path);
    printf("  [PASS] test_vertical_goal\n");
}

static void test_save_without_path(void)
{
    editor_t ed;
    editor_init(&ed);
    CHECK(!editor_save(&ed));
    CHECK(strstr(ed.message, "No file name") != NULL);
    editor_free(&ed);
    printf("  [PASS] test_save_without_path\n");
}

int main(void)
{
    setlocale(LC_CTYPE, "");

    printf("tam-edit editor tests\n");
    test_load_split();
    test_no_trailing_newline();
    test_crlf();
    test_missing_file();
    test_insert_and_backspace();
    test_utf8_edit();
    test_newline_and_join();
    test_visual_columns();
    test_vertical_goal();
    test_save_without_path();
    printf("editor tests passed\n");
    return 0;
}
