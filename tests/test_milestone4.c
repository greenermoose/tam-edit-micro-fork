#include "actions.h"
#include "buffer.h"
#include "clipboard.h"
#include "complete.h"
#include "config.h"
#include "cursor.h"
#include "editor.h"
#include "encoding.h"
#include "prompt.h"
#include "regex.h"
#include "search.h"
#include "shell.h"

#include <tam.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__,      \
                    #cond);                                                  \
            abort();                                                         \
        }                                                                    \
    } while (0)

static void setup_isolated_cfg(char *buf, size_t sz)
{
    snprintf(buf, sz, "/tmp/tam-edit-m4-cfg-%ld", (long)getpid());
    tam_fs_mkdir_p(buf, 0755);
    setenv("TAM_EDIT_CONFIG_HOME", buf, 1);
}

static void cleanup_isolated_cfg(const char *dir)
{
    char p[540];
    snprintf(p, sizeof(p), "%s/settings.ini", dir);
    unlink(p);
    rmdir(dir);
    unsetenv("TAM_EDIT_CONFIG_HOME");
}

static void test_regex_captures_and_replace(void)
{
    char err[128] = "";
    tam_regex_t *re = tam_regex_compile("(?P<key>[a-z]+)=([0-9]+)", 0, err, sizeof(err));
    CHECK(re != NULL);
    CHECK(tam_regex_group_index(re, "key") == 1);

    const char *s = "x=12 y=345";
    tam_regex_captures_t caps;
    CHECK(tam_regex_find_captures(re, s, strlen(s), 0, &caps));
    CHECK(caps.valid[0] && caps.valid[1] && caps.valid[2]);
    CHECK(caps.groups[1].start == 0 && caps.groups[1].end == 1);
    CHECK(caps.groups[2].start == 2 && caps.groups[2].end == 4);

    size_t rlen = 0;
    char *exp = search_expand_template(re, s, &caps, "${key}:$2($$0=$0)", &rlen);
    CHECK(exp != NULL);
    CHECK(strcmp(exp, "x:12($0=x=12)") == 0);
    free(exp);
    tam_regex_free(re);

    /* replaceall with capture groups + single undo */
    editor_t ed;
    editor_init(&ed);
    editor_insert_text(&ed, "a=1 b=2\nc=30", 12);

    prompt_execute_command(&ed, "replaceall \"([a-z]+)=([0-9]+)\" \"$2:$1\"");
    CHECK(strcmp(ed.doc.lines[0], "1:a 2:b") == 0);
    CHECK(strcmp(ed.doc.lines[1], "30:c") == 0);

    CHECK(editor_undo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "a=1 b=2") == 0);
    CHECK(strcmp(ed.doc.lines[1], "c=30") == 0);

    /* Interactive replace: y, n, a */
    ed.cx_line = 0;
    ed.cx_col = 0;
    prompt_execute_command(&ed, "replace \"([a-z]+)=([0-9]+)\" \"$1->$2\"");
    CHECK(ed.prompt.active && ed.prompt.kind == PROMPT_REPLACE_CONFIRM);
    tam_key_event_t ev_y = {.type = TAM_KEY_CHAR, .codepoint = 'y'};
    tam_key_event_t ev_n = {.type = TAM_KEY_CHAR, .codepoint = 'n'};
    tam_key_event_t ev_a = {.type = TAM_KEY_CHAR, .codepoint = 'a'};
    prompt_handle_key(&ed, &ev_y);
    CHECK(ed.prompt.active);
    prompt_handle_key(&ed, &ev_n);
    CHECK(ed.prompt.active);
    prompt_handle_key(&ed, &ev_a);
    CHECK(!ed.prompt.active);
    CHECK(strcmp(ed.doc.lines[0], "a->1 b=2") == 0);
    CHECK(strcmp(ed.doc.lines[1], "c->30") == 0);

    editor_free(&ed);
    printf("  [PASS] test_regex_captures_and_replace\n");
}

static void test_incsearch_and_hlsearch(void)
{
    editor_t ed;
    editor_init(&ed);
    editor_insert_text(&ed, "alpha beta alpha gamma", 22);
    ed.cx_line = 0;
    ed.cx_col = 0;

    prompt_open_find(&ed);
    tam_key_event_t ev_b = {.type = TAM_KEY_CHAR, .codepoint = 'b'};
    tam_key_event_t ev_e = {.type = TAM_KEY_CHAR, .codepoint = 'e'};
    prompt_handle_key(&ed, &ev_b);
    prompt_handle_key(&ed, &ev_e);
    /* incsearch jumps to "beta" at col 6 */
    CHECK(ed.cx_col == 6);

    /* Esc restores original cursor */
    tam_key_event_t ev_esc = {.type = TAM_KEY_ESC};
    prompt_handle_key(&ed, &ev_esc);
    CHECK(!ed.prompt.active);
    CHECK(ed.cx_col == 0);

    /* Literal search with regex metacharacters */
    prompt_open_find_literal(&ed);
    prompt_close(&ed);
    ed.cx_col = ed.doc.lens[0];
    editor_insert_text(&ed, " a+b", 4);
    snprintf(ed.search.query, sizeof(ed.search.query), "a+b");
    ed.search.literal = true;
    ed.cx_col = 0;
    search_find_next(&ed);
    CHECK(ed.cx_col == 23);

    /* hlsearch line matches */
    ed.config.hlsearch = true;
    snprintf(ed.search.query, sizeof(ed.search.query), "alpha");
    ed.search.literal = false;
    tam_regex_match_t ms[8];
    size_t n = search_line_matches(&ed, 0, ms, 8);
    CHECK(n == 2);
    CHECK(ms[0].start == 0 && ms[0].end == 5);
    CHECK(ms[1].start == 11 && ms[1].end == 16);

    editor_free(&ed);
    printf("  [PASS] test_incsearch_and_hlsearch\n");
}

static void test_encodings_roundtrip(void)
{
    CHECK(encoding_is_valid("utf-8"));
    CHECK(encoding_is_valid("latin1"));
    CHECK(encoding_is_valid("utf16be"));
    CHECK(!encoding_is_valid("definitely-not-an-encoding"));

    /* UTF-16LE with BOM decode -> save -> reload */
    char raw_u16le[] = { '\xFF', '\xFE', 'H', 0, 'i', 0, '\n', 0 };
    char path[256];
    snprintf(path, sizeof(path), "/tmp/tam-edit-enc-u16-%ld.txt", (long)getpid());
    CHECK(tam_fs_write_atomic(path, raw_u16le, sizeof(raw_u16le), 0644));

    buffer_t *b = buffer_open_with_encoding(path, "utf-8", 100);
    CHECK(b != NULL);
    CHECK(strcmp(b->encoding, "utf-16le") == 0);
    CHECK(b->has_bom);
    CHECK(strcmp(b->doc.lines[0], "Hi") == 0);
    buffer_ref(b);
    buffer_unref(b);
    unlink(path);

    /* Non-UTF8 latin1 fallback + unrepresentable save refusal */
    const unsigned char raw_lat1[] = { 'c', 'a', 'f', 0xE9, '\n' };
    snprintf(path, sizeof(path), "/tmp/tam-edit-enc-lat1-%ld.txt", (long)getpid());
    CHECK(tam_fs_write_atomic(path, raw_lat1, sizeof(raw_lat1), 0644));

    editor_t ed;
    editor_init(&ed);
    CHECK(editor_load(&ed, path));
    CHECK(ed.active_view->buf->used_fallback_enc);
    CHECK(strcmp(ed.doc.lines[0], "caf\xC3\xA9") == 0);

    /* Setlocal encoding us-ascii must refuse 'é' without corrupting on disk */
    prompt_execute_command(&ed, "setlocal encoding us-ascii");
    char err[192] = "";
    CHECK(!editor_save_configured(&ed, err, sizeof(err)));
    CHECK(strstr(err, "Cannot save") != NULL);

    /* Disk file still untouched */
    size_t disk_len = 0;
    char *disk = tam_fs_read_file(path, &disk_len);
    CHECK(disk != NULL && disk_len == sizeof(raw_lat1));
    CHECK(memcmp(disk, raw_lat1, sizeof(raw_lat1)) == 0);
    free(disk);

    editor_free(&ed);
    unlink(path);
    printf("  [PASS] test_encodings_roundtrip\n");
}

static void test_multicursor_and_line_ops(void)
{
    editor_t ed;
    editor_init(&ed);
    editor_insert_text(&ed, "foo 1\nfoo 2\nfoo 3", 17);
    ed.cx_line = 0;
    ed.cx_col = 0;

    /* SpawnMultiCursorDown twice -> 3 cursors */
    actions_execute(&ed, "SpawnMultiCursorDown");
    actions_execute(&ed, "SpawnMultiCursorDown");
    CHECK(ed.extra_count == 2);

    /* Type "bar_" at all 3 cursors in one undo group */
    editor_insert_text(&ed, "bar_", 4);
    CHECK(strcmp(ed.doc.lines[0], "bar_foo 1") == 0);
    CHECK(strcmp(ed.doc.lines[1], "bar_foo 2") == 0);
    CHECK(strcmp(ed.doc.lines[2], "bar_foo 3") == 0);

    /* Backspace once at all 3 cursors */
    editor_backspace(&ed);
    CHECK(strcmp(ed.doc.lines[0], "barfoo 1") == 0);
    CHECK(strcmp(ed.doc.lines[1], "barfoo 2") == 0);
    CHECK(strcmp(ed.doc.lines[2], "barfoo 3") == 0);

    /* Single undo reverts all 3 cursors cleanly */
    CHECK(editor_undo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "bar_foo 1") == 0);
    CHECK(editor_undo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "foo 1") == 0);
    CHECK(strcmp(ed.doc.lines[1], "foo 2") == 0);
    CHECK(strcmp(ed.doc.lines[2], "foo 3") == 0);

    /* SpawnMultiCursor match selection (Alt-N equivalent) */
    ed.cx_line = 0;
    ed.cx_col = 0;
    ed.extra_count = 0;
    ed.sel.active = false;
    actions_execute(&ed, "SpawnMultiCursor"); /* selects "foo" */
    CHECK(ed.sel.active);
    actions_execute(&ed, "SpawnMultiCursor"); /* adds line 1 "foo" */
    CHECK(ed.extra_count == 1);
    actions_execute(&ed, "SkipMultiCursor"); /* skips line 1, selects line 2 "foo" */
    CHECK(ed.extra_count == 1);
    CHECK(ed.extra_cursors[0].cx_line == 2);

    /* Replace selection text across remaining 2 cursors */
    editor_insert_text(&ed, "baz", 3);
    CHECK(strcmp(ed.doc.lines[0], "baz 1") == 0);
    CHECK(strcmp(ed.doc.lines[1], "foo 2") == 0);
    CHECK(strcmp(ed.doc.lines[2], "baz 3") == 0);

    actions_execute(&ed, "RemoveAllMultiCursors");
    CHECK(ed.extra_count == 0);

    /* MoveLinesDown / MoveLinesUp / DeleteLine / Duplicate */
    ed.cx_line = 0;
    ed.cx_col = 0;
    actions_execute(&ed, "MoveLinesDown");
    CHECK(strcmp(ed.doc.lines[0], "foo 2") == 0);
    CHECK(strcmp(ed.doc.lines[1], "baz 1") == 0);
    CHECK(ed.cx_line == 1);
    actions_execute(&ed, "MoveLinesUp");
    CHECK(strcmp(ed.doc.lines[0], "baz 1") == 0);
    CHECK(strcmp(ed.doc.lines[1], "foo 2") == 0);
    actions_execute(&ed, "DeleteLine");
    CHECK(ed.doc.count == 2);
    CHECK(strcmp(ed.doc.lines[0], "foo 2") == 0);

    /* Overwrite mode */
    ed.cx_line = 0;
    ed.cx_col = 0;
    actions_execute(&ed, "ToggleOverwriteMode");
    CHECK(ed.config.overwrite);
    editor_insert_codepoint(&ed, 'Z');
    CHECK(strcmp(ed.doc.lines[0], "Zoo 2") == 0);

    editor_free(&ed);
    printf("  [PASS] test_multicursor_and_line_ops\n");
}

static void test_delete_word_left(void)
{
    editor_t ed;
    editor_init(&ed);

    /* 1. Single-cursor mid-line UTF-8 deletion and undo/redo */
    editor_insert_text(&ed, "caf\xc3\xa9 latte", 11);
    CHECK(ed.cx_line == 0 && ed.cx_col == 10);
    actions_execute(&ed, "DeleteWordLeft");
    CHECK(strcmp(ed.doc.lines[0], "caf\xc3\xa9 ") == 0);
    CHECK(ed.cx_col == 5);
    CHECK(ed.dirty);

    CHECK(editor_undo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "caf\xc3\xa9 latte") == 0);
    CHECK(ed.cx_col == 10);
    CHECK(editor_redo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "caf\xc3\xa9 ") == 0);

    /* 2. Column 0 joins with previous line; (0,0) is a no-op */
    ed.doc.lines[0][0] = '\0';
    ed.doc.lens[0] = 0;
    ed.cx_line = 0;
    ed.cx_col = 0;
    editor_insert_text(&ed, "first\nsecond", 12);
    ed.cx_line = 1;
    ed.cx_col = 0;
    actions_execute(&ed, "DeleteWordLeft");
    CHECK(ed.doc.count == 1);
    CHECK(strcmp(ed.doc.lines[0], "firstsecond") == 0);
    CHECK(ed.cx_line == 0 && ed.cx_col == 5);

    ed.cx_line = 0;
    ed.cx_col = 0;
    actions_execute(&ed, "DeleteWordLeft");
    CHECK(strcmp(ed.doc.lines[0], "firstsecond") == 0);
    CHECK(ed.cx_line == 0 && ed.cx_col == 0);

    /* 3. Multi-cursor across multiple lines + single undo */
    editor_free(&ed);
    editor_init(&ed);
    editor_insert_text(&ed, "alpha one\nbeta two\ngamma three", 30);
    ed.cx_line = 0;
    ed.cx_col = 9;
    actions_execute(&ed, "SpawnMultiCursorDown");
    actions_execute(&ed, "SpawnMultiCursorDown");
    ed.extra_cursors[0].cx_col = 8;
    ed.extra_cursors[1].cx_col = 11;
    CHECK(ed.extra_count == 2);

    actions_execute(&ed, "DeleteWordLeft");
    CHECK(strcmp(ed.doc.lines[0], "alpha ") == 0);
    CHECK(strcmp(ed.doc.lines[1], "beta ") == 0);
    CHECK(strcmp(ed.doc.lines[2], "gamma ") == 0);
    CHECK(ed.cx_col == 6);
    CHECK(ed.extra_count == 2);
    CHECK(ed.extra_cursors[0].cx_col == 5);
    CHECK(ed.extra_cursors[1].cx_col == 6);

    CHECK(editor_undo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "alpha one") == 0);
    CHECK(strcmp(ed.doc.lines[1], "beta two") == 0);
    CHECK(strcmp(ed.doc.lines[2], "gamma three") == 0);

    /* 4. Multi-cursor on the same line */
    actions_execute(&ed, "RemoveAllMultiCursors");
    ed.cx_line = 0;
    ed.cx_col = 5; /* "alpha| one" */
    ed.extra_cursors[0] = (cursor_t){0, 9, 9, {{0, 0}, {0, 0}, false}}; /* "alpha one|" */
    ed.extra_count = 1;
    actions_execute(&ed, "DeleteWordLeft");
    CHECK(strcmp(ed.doc.lines[0], " ") == 0);

    /* 5. Active selection deletion + read-only guard */
    actions_execute(&ed, "RemoveAllMultiCursors");
    ed.cx_line = 0;
    ed.cx_col = 0;
    ed.sel.active = true;
    ed.sel.anchor = (loc_t){0, 0};
    ed.sel.cursor = (loc_t){1, 5};
    actions_execute(&ed, "DeleteWordLeft");
    CHECK(ed.doc.count == 2);
    CHECK(strcmp(ed.doc.lines[0], "two") == 0);

    if (ed.active_view && ed.active_view->buf) {
        ed.active_view->buf->readonly = true;
        ed.cx_line = 0;
        ed.cx_col = 3;
        actions_execute(&ed, "DeleteWordLeft");
        CHECK(strcmp(ed.doc.lines[0], "two") == 0);
        ed.active_view->buf->readonly = false;
    }

    editor_free(&ed);
    printf("  [PASS] test_delete_word_left\n");
}

static void test_shell_commands_and_completion(void)
{
    char cfg_dir[256];
    setup_isolated_cfg(cfg_dir, sizeof(cfg_dir));

    editor_t ed;
    editor_init(&ed);

    /* textfilter */
    editor_insert_text(&ed, "zebra\napple\nmango", 17);
    prompt_execute_command(&ed, "textfilter sort");
    CHECK(ed.doc.count == 3);
    CHECK(strcmp(ed.doc.lines[0], "apple") == 0);
    CHECK(strcmp(ed.doc.lines[1], "mango") == 0);
    CHECK(strcmp(ed.doc.lines[2], "zebra") == 0);

    /* undo textfilter in one step */
    CHECK(editor_undo(&ed));
    CHECK(strcmp(ed.doc.lines[0], "zebra") == 0);

    /* non-blocking background run via poll */
    prompt_execute_command(&ed, "run printf 'hello-bg'");
    CHECK(ed.bg_job.active);
    struct pollfd pfd = { .fd = ed.bg_job.pipe_fd, .events = POLLIN };
    for (int tries = 0; tries < 50 && ed.bg_job.active; tries++) {
        poll(&pfd, 1, 50);
        shell_bg_poll_read(&ed.bg_job);
    }
    CHECK(!ed.bg_job.active);
    CHECK(ed.bg_job.exit_code == 0);
    CHECK(ed.bg_job.output != NULL && strcmp(ed.bg_job.output, "hello-bg") == 0);

    /* bind / unbind + persistence */
    prompt_execute_command(&ed, "bind F5 DeleteLine");
    tam_key_event_t ev_f5 = {.type = TAM_KEY_F5};
    const char *desc = actions_lookup_key_description(&ed, &ev_f5);
    CHECK(desc != NULL && strcmp(desc, "DeleteLine") == 0);
    prompt_execute_command(&ed, "unbind F5");
    CHECK(actions_lookup_key_description(&ed, &ev_f5) == NULL);

    /* help topic scratch buffer */
    size_t tabs_before = ed.tabs.count;
    prompt_execute_command(&ed, "help commands");
    CHECK(ed.active_view != NULL && ed.active_view->buf->scratch);
    CHECK(ed.active_view->buf->readonly);
    CHECK(ed.active_view->buf->doc.count > 10);
    (void)tabs_before;

    /* retab */
    editor_add_tab(&ed, NULL);
    editor_insert_text(&ed, "\tfoo\n\t\tbar", 10);
    ed.config.tabsize = 4;
    ed.config.tabstospaces = true;
    prompt_execute_command(&ed, "retab");
    CHECK(strcmp(ed.doc.lines[0], "    foo") == 0);
    CHECK(strcmp(ed.doc.lines[1], "        bar") == 0);

    /* mkparents + save-as + eofnewline + rmtrailingws */
    char dir_path[256], file_path[300];
    snprintf(dir_path, sizeof(dir_path), "/tmp/tam-edit-mkp-%ld/sub", (long)getpid());
    snprintf(file_path, sizeof(file_path), "%s/test.txt", dir_path);
    ed.config.mkparents = true;
    ed.config.rmtrailingws = true;
    ed.config.eofnewline = true;
    editor_add_tab(&ed, NULL);
    editor_insert_text(&ed, "hello   ", 8);
    char err[192] = "";
    CHECK(editor_save_to_configured(&ed, file_path, err, sizeof(err)));
    size_t flen = 0;
    char *fdata = tam_fs_read_file(file_path, &flen);
    CHECK(fdata != NULL && strcmp(fdata, "hello\n") == 0);
    free(fdata);
    unlink(file_path);
    rmdir(dir_path);
    snprintf(dir_path, sizeof(dir_path), "/tmp/tam-edit-mkp-%ld", (long)getpid());
    rmdir(dir_path);

    /* tabmove / tabswitch */
    prompt_execute_command(&ed, "tabswitch 1");
    CHECK(ed.tabs.active == 0);
    prompt_execute_command(&ed, "tabmove 2");
    CHECK(ed.tabs.active == 1);

    /* prompt completion */
    prompt_open_command(&ed);
    snprintf(ed.prompt.buf, sizeof(ed.prompt.buf), "rep");
    ed.prompt.len = 3;
    ed.prompt.cx = 3;
    CHECK(prompt_complete_step(&ed, 1));
    CHECK(strcmp(ed.prompt.buf, "replace") == 0);
    CHECK(prompt_complete_step(&ed, 1));
    CHECK(strcmp(ed.prompt.buf, "replaceall") == 0);
    prompt_close(&ed);

    /* ls & jump */
    prompt_execute_command(&ed, "ls /tmp");
    CHECK(ed.active_view->buf->scratch);
    editor_free(&ed);
    cleanup_isolated_cfg(cfg_dir);
    printf("  [PASS] test_shell_commands_and_completion\n");
}

int run_milestone4_tests(void)
{
    printf("=== Milestone 4: Full Editing, Search, Encoding, Shell & Multi-Cursor Tests ===\n");
    test_regex_captures_and_replace();
    test_incsearch_and_hlsearch();
    test_encodings_roundtrip();
    test_multicursor_and_line_ops();
    test_delete_word_left();
    test_shell_commands_and_completion();
    printf("All Milestone 4 tests passed.\n");
    return 0;
}
