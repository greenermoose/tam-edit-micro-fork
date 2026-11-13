#include "buffer.h"
#include "colorscheme.h"
#include "colorschemes_data.h"
#include "config.h"
#include "editor.h"
#include "prompt.h"
#include "regex.h"
#include "render.h"
#include "syntax.h"
#include "syntax_data.h"

#include <tam.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__,      \
                    #cond);                                                  \
            abort();                                                         \
        }                                                                    \
    } while (0)

static double elapsed_ms(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) * 1000.0 +
           (double)(b.tv_nsec - a.tv_nsec) / 1000000.0;
}

static void test_regex_features(void)
{
    char err[128] = {0};
    tam_regex_match_t m;

    /* Inline flags (?i), (?-i), (?i:...) */
    tam_regex_t *re = tam_regex_compile("(?i)hello (?-i)World (?i:abc)", 0, err, sizeof(err));
    CHECK(re != NULL);
    CHECK(tam_regex_match(re, "HeLLo World AbC", 15));
    CHECK(!tam_regex_match(re, "HeLLo world AbC", 15));
    tam_regex_free(re);

    /* Non-capturing group, alternation, word boundaries \b and \B */
    re = tam_regex_compile("\\b(?:int|char)\\b\\B?", 0, err, sizeof(err));
    CHECK(re != NULL);
    CHECK(tam_regex_find(re, "uint int x", 10, 0, &m));
    CHECK(m.start == 5 && m.end == 8);
    CHECK(!tam_regex_match(re, "printing", 8));
    tam_regex_free(re);

    re = tam_regex_compile("\\Bfoo\\B", 0, err, sizeof(err));
    CHECK(re != NULL);
    CHECK(tam_regex_match(re, "xfooy", 5));
    CHECK(!tam_regex_match(re, "foo", 3));
    tam_regex_free(re);

    /* Shorthands \d \D \w \W \s \S and POSIX classes */
    re = tam_regex_compile("^[[:alpha:]]+_[[:xdigit:]]+\\s+\\d+$", 0, err, sizeof(err));
    CHECK(re != NULL);
    CHECK(tam_regex_match(re, "var_deadBEEF   42", 17));
    CHECK(!tam_regex_match(re, "var_xyz 42", 10));
    tam_regex_free(re);

    /* Lazy vs greedy */
    re = tam_regex_compile("\".*?\"", 0, err, sizeof(err));
    CHECK(re != NULL);
    const char *s = "\"a\" + \"b\"";
    CHECK(tam_regex_find(re, s, strlen(s), 0, &m));
    CHECK(m.start == 0 && m.end == 3);
    tam_regex_free(re);

    /* Bounded quantifiers {m,n} */
    re = tam_regex_compile("\\b[[:digit:]]{2,4}\\b", 0, err, sizeof(err));
    CHECK(re != NULL);
    CHECK(!tam_regex_match(re, "1", 1));
    CHECK(tam_regex_match(re, "12", 2));
    CHECK(tam_regex_match(re, "1234", 4));
    CHECK(!tam_regex_match(re, "12345", 5));
    tam_regex_free(re);

    /* Unicode Sm category (Haskell uses \p{Sm}) */
    re = tam_regex_compile("\\p{Sm}", 0, err, sizeof(err));
    CHECK(re != NULL);
    CHECK(tam_regex_match(re, "a \xe2\x88\x80 b", 7)); /* U+2200 FOR ALL */
    CHECK(tam_regex_match(re, "a + b", 5));
    CHECK(!tam_regex_match(re, "abc", 3));
    tam_regex_free(re);

    /* Linear-time guarantee on pathological (a+)+b */
    re = tam_regex_compile("(a+)+b", 0, err, sizeof(err));
    CHECK(re != NULL);
    char big[128];
    memset(big, 'a', 80);
    big[80] = '\0';
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(!tam_regex_match(re, big, 80));
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK(elapsed_ms(t0, t1) < 20.0);
    tam_regex_free(re);

    printf("  [PASS] test_regex_features\n");
}

static void test_compile_all_embedded_syntaxes(void)
{
    syntax_registry_init();
    CHECK(g_embedded_syntax_defs_count >= 158);
    for (size_t i = 0; i < g_embedded_syntax_defs_count; i++) {
        syntax_def_t *def = syntax_find_by_filetype(g_embedded_syntax_defs[i].filetype);
        CHECK(def != NULL);
        CHECK(def->compiled);
    }
    printf("  [PASS] test_compile_all_embedded_syntaxes (%zu defs)\n",
           g_embedded_syntax_defs_count);
}

static void test_filetype_detection(void)
{
    /* Filename detection */
    const char *c_lines[] = { "int main(void) { return 0; }" };
    size_t c_lens[] = { strlen(c_lines[0]) };
    syntax_def_t *d = syntax_detect("foo/bar.c", NULL, c_lines, c_lens, 1, 100);
    CHECK(d != NULL && strcmp(d->filetype, "c") == 0);

    /* Header / shebang detection with no extension */
    const char *py_lines[] = { "#!/usr/bin/env python3", "print('hi')" };
    size_t py_lens[] = { strlen(py_lines[0]), strlen(py_lines[1]) };
    d = syntax_detect("script_no_ext", NULL, py_lines, py_lens, 2, 100);
    CHECK(d != NULL && strcmp(d->filetype, "python") == 0);

    /* Detectlimit + unknown fallback */
    d = syntax_detect("unknown_blob", NULL, (const char *const[]){"plain text"}, (size_t[]){10}, 1, 10);
    CHECK(d == NULL || strcmp(d->filetype, "unknown") == 0);

    /* Explicit requested filetype override */
    d = syntax_detect("file.txt", "rust", (const char *const[]){"fn main() {}"}, (size_t[]){12}, 1, 100);
    CHECK(d != NULL && strcmp(d->filetype, "rust") == 0);

    printf("  [PASS] test_filetype_detection\n");
}

static void test_highlight_regions_includes_skips(void)
{
    /* Transitive includes: jinja2 -> html -> javascript */
    syntax_def_t *jinja = syntax_find_by_filetype("jinja2");
    CHECK(jinja != NULL);

    const char *jline = "<div id=a><script>let s = \"hi\";</script> <!-- TODO --> {{ user }}";
    uint16_t groups[256];
    size_t nrunes = 0;
    const syntax_region_t *st = syntax_highlight_line(jinja, NULL, jline, strlen(jline),
                                                      false, groups, 256, &nrunes);
    CHECK(st == NULL);
    CHECK(nrunes == strlen(jline));

    uint16_t gid_tag = syntax_intern_group("symbol.tag");
    uint16_t gid_id  = syntax_intern_group("identifier");
    uint16_t gid_str = syntax_intern_group("constant.string");
    uint16_t gid_spl = syntax_intern_group("special");
    uint16_t gid_todo = syntax_intern_group("todo");
    size_t idx_str  = (size_t)(strstr(jline, "\"hi\"") - jline);
    size_t idx_todo = (size_t)(strstr(jline, "TODO") - jline);
    size_t idx_spl  = (size_t)(strstr(jline, "{{") - jline);
    /* "<div " from html opening tag, "id" attribute identifier inside tag */
    CHECK(groups[0] == gid_tag);
    CHECK(groups[5] == gid_id && groups[6] == gid_id);
    /* "\"hi\"" inside <script> (3-level transitive jinja2 -> html -> javascript) */
    CHECK(groups[idx_str] == gid_str && groups[idx_str + 3] == gid_str);
    /* "TODO" inside html comment.block */
    CHECK(groups[idx_todo] == gid_todo && groups[idx_todo + 3] == gid_todo);
    /* "{{" / "}}" limit-group: special in jinja2 */
    CHECK(groups[idx_spl] == gid_spl && groups[nrunes - 1] == gid_spl);

    /* Escaped quotes / skip inside C string literal */
    syntax_def_t *cdef = syntax_find_by_filetype("c");
    CHECK(cdef != NULL);
    const char *cline = "const char *s = \"a\\\"/*not comment*/b\"; // ok";
    st = syntax_highlight_line(cdef, NULL, cline, strlen(cline),
                               false, groups, 256, &nrunes);
    CHECK(st == NULL);
    uint16_t gid_cmt = syntax_intern_group("comment");
    /* Inside string at index 21 ('*') must still be constant.string, not comment */
    CHECK(groups[21] == gid_str);
    /* Trailing "// ok" must be comment */
    CHECK(groups[nrunes - 1] == gid_cmt);

    printf("  [PASS] test_highlight_regions_includes_skips\n");
}

static void test_incremental_edit_convergence(void)
{
    editor_t ed;
    editor_init(&ed);
    const char *text = "int a = 1;\nint b = 2;\nint c = 3;\nint d = 4;\n";
    editor_insert_text(&ed, text, strlen(text));
    prompt_execute_command(&ed, "setlocal filetype c");

    buffer_t *b = ed.active_view->buf;
    CHECK(b != NULL && b->syntax_enabled);
    buffer_syntax_update_to(b, 4);

    uint16_t gid_cmt = syntax_intern_group("comment");
    size_t nr = 0;
    const uint16_t *g3 = buffer_syntax_get_line_groups(b, 3, &nr);
    CHECK(g3 != NULL && nr > 0 && g3[0] != gid_cmt);

    /* Open block comment on line 0 */
    editor_raw_insert(&ed, 0, 0, "/* ", 3);
    g3 = buffer_syntax_get_line_groups(b, 3, &nr);
    CHECK(g3 != NULL && nr > 0 && g3[0] == gid_cmt);

    /* Close block comment on line 1 -> lines 2 and 3 must converge back */
    editor_raw_insert(&ed, 1, b->doc.lens[1], " */", 3);
    g3 = buffer_syntax_get_line_groups(b, 3, &nr);
    CHECK(g3 != NULL && nr > 0 && g3[0] != gid_cmt);

    editor_free(&ed);
    printf("  [PASS] test_incremental_edit_convergence\n");
}

static void test_flat_yaml_override(void)
{
    const char *yaml =
        "filetype: mycustomlang\n"
        "detect:\n"
        "    filename: \"\\.mcl$\"\n"
        "    header: \"^#!.*mycustom\"\n"
        "rules:\n"
        "    - statement: \"\\\\b(let|fn|ret)\\\\b\"\n"
        "    - constant.number: \"\\\\b[0-9]+\\\\b\"\n"
        "    - identifier.var:\n"
        "        start: \"\\\\$\"\n"
        "        end: \"\\\\b\"\n"
        "        rules: []\n";

    char err[256] = {0};
    CHECK(syntax_registry_load_yaml_string(yaml, "inline.yaml", err, sizeof(err)));

    syntax_def_t *def = syntax_find_by_filetype("mycustomlang");
    CHECK(def != NULL && def->is_override);

    const char *sample = "let x = 42";
    uint16_t groups[64];
    size_t nr = 0;
    syntax_highlight_line(def, NULL, sample, strlen(sample), false, groups, 64, &nr);
    CHECK(nr == strlen(sample));
    CHECK(groups[0] == syntax_intern_group("statement"));
    CHECK(groups[8] == syntax_intern_group("constant.number"));

    /* Directory override via TAM_EDIT_CONFIG_HOME */
    char dir_tmpl[] = "/tmp/tam-cfg-XXXXXX";
    char *tmp_cfg = mkdtemp(dir_tmpl);
    CHECK(tmp_cfg != NULL);

    char syn_dir[512];
    snprintf(syn_dir, sizeof(syn_dir), "%s/syntax", tmp_cfg);
    CHECK(mkdir(syn_dir, 0755) == 0);

    char yaml_path[1024];
    snprintf(yaml_path, sizeof(yaml_path), "%s/c.yaml", syn_dir);
    FILE *fp = fopen(yaml_path, "w");
    CHECK(fp != NULL);
    fprintf(fp,
            "filetype: c_override_probe\n"
            "detect:\n"
            "    filename: \"\\\\.probe99$\"\n"
            "rules:\n"
            "    - special: \"\\\\bMAGIC\\\\b\"\n");
    fclose(fp);

    syntax_registry_free();
    setenv("TAM_EDIT_CONFIG_HOME", tmp_cfg, 1);
    syntax_registry_init();
    syntax_def_t *probe = syntax_find_by_filetype("c_override_probe");
    CHECK(probe != NULL && probe->is_override);

    unlink(yaml_path);
    rmdir(syn_dir);
    rmdir(tmp_cfg);
    unsetenv("TAM_EDIT_CONFIG_HOME");
    syntax_registry_free();
    syntax_registry_init();

    printf("  [PASS] test_flat_yaml_override\n");
}

static void test_all_embedded_colorschemes_and_prompt(void)
{
    CHECK(g_embedded_colorschemes_count >= 25);
    colorscheme_t cs;
    colorscheme_init(&cs);
    for (size_t i = 0; i < g_embedded_colorschemes_count; i++) {
        CHECK(colorscheme_load(&cs, g_embedded_colorschemes[i].name));
        syntax_build_style_cache(&cs, cs.group_cache);
        cs.cache_valid = true;
    }
    colorscheme_free(&cs);

    /* Prompt commands: setlocal filetype, set syntax, togglelocal syntax */
    editor_t ed;
    editor_init(&ed);
    prompt_execute_command(&ed, "setlocal filetype c");
    CHECK(strcmp(ed.active_view->buf->filetype, "c") == 0);
    prompt_execute_command(&ed, "togglelocal syntax");
    CHECK(ed.active_view->buf->syntax_enabled == false);
    prompt_execute_command(&ed, "setlocal syntax true");
    CHECK(ed.active_view->buf->syntax_enabled == true);
    prompt_execute_command(&ed, "set syntax false");
    CHECK(ed.config.syntax == false);
    prompt_execute_command(&ed, "set syntax true");
    editor_free(&ed);

    printf("  [PASS] test_all_embedded_colorschemes_and_prompt (%zu schemes)\n",
           g_embedded_colorschemes_count);
}

static void test_perf_10k_lines(void)
{
    editor_t ed;
    editor_init(&ed);

    const char *sample = "static int compute_sum(int a, int b) { /* add */ return a + b + 42; }\n";
    size_t slen = strlen(sample);
    size_t nlines = 10000;
    char *big = malloc(nlines * slen + 1);
    CHECK(big != NULL);
    for (size_t i = 0; i < nlines; i++) {
        memcpy(big + i * slen, sample, slen);
    }
    big[nlines * slen] = '\0';

    editor_insert_text(&ed, big, nlines * slen);
    free(big);

    prompt_execute_command(&ed, "setlocal filetype c");
    buffer_t *b = ed.active_view->buf;
    CHECK(b != NULL && b->doc.count >= 10000);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    buffer_syntax_update_to(b, 9999);
    for (size_t l = 9950; l < 10000; l++) {
        size_t nr = 0;
        const uint16_t *g = buffer_syntax_get_line_groups(b, l, &nr);
        CHECK(g != NULL && nr == slen - 1);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = elapsed_ms(t0, t1);
    printf("  [INFO] 10,000-line C state scan + viewport highlight: %.2f ms\n", ms);
    CHECK(ms < 500.0); /* Generous ceiling for both -O2 and -fsanitize=address,undefined */

    editor_free(&ed);
    printf("  [PASS] test_perf_10k_lines\n");
}

int run_milestone3_tests(void)
{
    printf("=== Milestone 3: Syntax Highlighting Tests ===\n");
    test_regex_features();
    test_compile_all_embedded_syntaxes();
    test_filetype_detection();
    test_highlight_regions_includes_skips();
    test_incremental_edit_convergence();
    test_flat_yaml_override();
    test_all_embedded_colorschemes_and_prompt();
    test_perf_10k_lines();
    printf("All Milestone 3 tests passed.\n\n");
    return 0;
}
