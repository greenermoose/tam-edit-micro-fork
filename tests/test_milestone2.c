#include "buffer.h"
#include "colorscheme.h"
#include "config.h"
#include "editor.h"
#include "pane.h"

#include <tam.h>
#include <tam_uni.h>

#include <assert.h>
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
    snprintf(buf, sizeof(buf), "%s/tam-edit-m2-%s-%ld", dir, name, (long)getpid());
    return buf;
}

static void test_pane_split_tree(void)
{
    buffer_t *b1 = buffer_new(NULL);
    view_t *v1 = view_new(b1);
    pane_node_t *root = pane_create_leaf(v1);
    CHECK(root != NULL);
    CHECK(root->type == PANE_LEAF);

    pane_layout(root, 0, 0, 80, 24);
    CHECK(v1->w == 80 && v1->h == 24);

    /* Vertical split */
    buffer_t *b2 = buffer_new(NULL);
    view_t *v2 = view_new(b2);
    pane_node_t *n2 = pane_split(root, PANE_SPLIT_V, v2);
    CHECK(n2 != NULL);
    CHECK(root->type == PANE_SPLIT_V);
    CHECK(root->child1 != NULL && root->child2 != NULL);

    pane_layout(root, 0, 0, 80, 24);
    CHECK(v1->w + v2->w + 1 == 80);
    CHECK(v1->h == 24 && v2->h == 24);
    CHECK(v2->x == v1->w + 1);

    /* Navigation */
    CHECK(pane_next_view(root, v1) == v2);
    CHECK(pane_next_view(root, v2) == v1);
    CHECK(pane_prev_view(root, v2) == v1);

    /* Hit testing */
    CHECK(pane_hit_test(root, 10, 10) == v1);
    CHECK(pane_hit_test(root, 60, 10) == v2);

    /* Horizontal split inside v2 */
    buffer_t *b3 = buffer_new(NULL);
    view_t *v3 = view_new(b3);
    pane_node_t *n3 = pane_split(n2, PANE_SPLIT_H, v3);
    CHECK(n3 != NULL);

    pane_layout(root, 0, 0, 80, 24);
    CHECK(v2->h + v3->h == 24);
    CHECK(pane_next_view(root, v2) == v3);

    /* Close split v3 */
    pane_node_t *new_root = NULL;
    pane_close(root, n3, &new_root);
    CHECK(new_root == root);
    CHECK(root->child2->type == PANE_LEAF);
    CHECK(root->child2->view == v2);

    pane_free_tree(root);
}

static void test_tabs_lifecycle(void)
{
    tab_list_t tl;
    tab_list_init(&tl);

    buffer_t *b1 = buffer_new("/tmp/file1.txt");
    view_t *v1 = view_new(b1);
    tab_t *t1 = tab_list_add(&tl, v1);
    CHECK(t1 != NULL);
    CHECK(tl.count == 1);
    CHECK(tl.active == 0);

    char title[64];
    CHECK(strcmp(tab_get_title(t1, title, sizeof(title)), "file1.txt") == 0);

    /* Add second tab */
    buffer_t *b2 = buffer_new("/tmp/file2.c");
    view_t *v2 = view_new(b2);
    tab_t *t2 = tab_list_add(&tl, v2);
    CHECK(t2 != NULL);
    CHECK(tl.count == 2);
    CHECK(tl.active == 1);
    CHECK(strcmp(tab_get_title(t2, title, sizeof(title)), "file2.c") == 0);

    /* Cycling */
    tab_list_next(&tl);
    CHECK(tl.active == 0);
    tab_list_prev(&tl);
    CHECK(tl.active == 1);

    /* Close tab */
    CHECK(tab_list_close(&tl, 0));
    CHECK(tl.count == 1);
    CHECK(tl.active == 0);

    tab_list_free(&tl);
}

static void test_softwrap_layout(void)
{
    const char *line = "The quick brown fox jumps over the lazy dog and runs away.";
    size_t len = strlen(line);

    /* At width 20, line should wrap into multiple sublines */
    size_t sublines = edit_softwrap_line_sublines(line, len, 20, 4, true);
    CHECK(sublines >= 3);

    int subline = 0, vx = 0;
    edit_softwrap_loc_to_vloc(line, len, 0, 20, 4, true, &subline, &vx);
    CHECK(subline == 0 && vx == 0);

    /* Round-trip conversion */
    size_t rune_col = 25;
    edit_softwrap_loc_to_vloc(line, len, rune_col, 20, 4, true, &subline, &vx);
    size_t back_rune = edit_softwrap_vloc_to_rune(line, len, subline, vx, 20, 4, true);
    CHECK(back_rune == rune_col);
}

static void test_tam_ini_config(void)
{
    config_t cfg;
    config_init_defaults(&cfg);

    CHECK(cfg.softwrap == false);
    CHECK(cfg.tabsize == 4);

    CHECK(config_set(&cfg, "softwrap", "true"));
    CHECK(cfg.softwrap == true);

    CHECK(config_set(&cfg, "tabsize", "8"));
    CHECK(cfg.tabsize == 8);

    CHECK(config_set(&cfg, "colorscheme", "monokai"));
    CHECK(strcmp(cfg.colorscheme, "monokai") == 0);

    /* Keybinding parsing */
    tam_key_type_t kt = TAM_KEY_NONE;
    uint32_t cp = 0;
    unsigned mods = 0;

    CHECK(keybind_parse_string("Ctrl-s", &kt, &cp, &mods));
    CHECK(kt == TAM_KEY_CHAR && cp == 's' && (mods & TAM_MOD_CTRL));

    CHECK(keybind_parse_string("Alt-w", &kt, &cp, &mods));
    CHECK(kt == TAM_KEY_CHAR && cp == 'w' && (mods & TAM_MOD_ALT));

    CHECK(keybind_parse_string("CtrlPageUp", &kt, &cp, &mods));
    CHECK(kt == TAM_KEY_PAGE_UP && (mods & TAM_MOD_CTRL));

    /* INI file check */
    const char *ini_path = test_path("settings.ini");
    const char *ini_content =
        "[settings]\n"
        "softwrap = true\n"
        "tabsize = 4\n"
        "colorscheme = default\n"
        "\n"
        "[bindings]\n"
        "Alt-w = togglelocal softwrap\n"
        "Ctrl-t = AddTab\n";
    CHECK(tam_fs_write_atomic(ini_path, ini_content, strlen(ini_content), 0644));

    custom_keybind_t *kb = NULL;
    CHECK(config_load_file(ini_path, &cfg, &kb));
    CHECK(cfg.softwrap == true);
    CHECK(kb != NULL);
    keybinds_free(kb);

    CHECK(config_check(ini_path, NULL));
}

static void test_micro_importer_json5(void)
{
    char dir[600];
    snprintf(dir, sizeof(dir), "%s-micro-cfg", test_path("dir"));
    tam_fs_mkdir_p(dir, 0755);

    char s_json[1024], b_json[1024], out_ini[1024];
    snprintf(s_json, sizeof(s_json), "%s/settings.json", dir);
    snprintf(b_json, sizeof(b_json), "%s/bindings.json", dir);
    snprintf(out_ini, sizeof(out_ini), "%s/settings.ini", dir);

    /* Realistic micro JSON5 with comments and trailing comma */
    const char *s_text =
        "{\n"
        "    // Softwrap enabled\n"
        "    \"softwrap\": true,\n"
        "    \"wordwrap\": true,\n"
        "}\n";
    const char *b_text =
        "{\n"
        "    \"Alt-w\": \"command:togglelocal softwrap\",\n"
        "    \"Ctrl-t\": \"AddTab\",\n"
        "}\n";

    CHECK(tam_fs_write_atomic(s_json, s_text, strlen(s_text), 0644));
    CHECK(tam_fs_write_atomic(b_json, b_text, strlen(b_text), 0644));

    CHECK(config_import_micro(dir, out_ini, NULL));

    size_t len = 0;
    char *res = tam_fs_read_file(out_ini, &len);
    CHECK(res != NULL);
    CHECK(strstr(res, "softwrap = true") != NULL);
    CHECK(strstr(res, "wordwrap = true") != NULL);
    CHECK(strstr(res, "Alt-w = togglelocal softwrap") != NULL);
    free(res);

    CHECK(config_check(out_ini, NULL));
}

static void test_colorscheme_parser(void)
{
    style_t st = style_parse("bold #CB4B16,#282828");
    CHECK(st.attr & ATTR_BOLD);
    CHECK(COLOR_IS_RGB(st.fg));
    CHECK(COLOR_R(st.fg) == 0xCB && COLOR_G(st.fg) == 0x4B && COLOR_B(st.fg) == 0x16);
    CHECK(COLOR_IS_RGB(st.bg));
    CHECK(COLOR_R(st.bg) == 0x28 && COLOR_G(st.bg) == 0x28 && COLOR_B(st.bg) == 0x28);

    style_t st2 = style_parse("red,blue");
    CHECK(COLOR_IS_ANSI(st2.fg) && COLOR_ANSI_NUM(st2.fg) == 1);
    CHECK(COLOR_IS_ANSI(st2.bg) && COLOR_ANSI_NUM(st2.bg) == 4);

    colorscheme_t cs;
    colorscheme_init(&cs);

    style_t def = colorscheme_get(&cs, "default");
    CHECK(COLOR_IS_RGB(def.fg) || COLOR_IS_DEFAULT(def.fg));

    style_t com = colorscheme_get(&cs, "comment");
    CHECK(COLOR_IS_RGB(com.fg));

    /* Hierarchical fallback: constant.string.special falls back to constant.string */
    style_t fb = colorscheme_get(&cs, "constant.string.special");
    style_t cs_str = colorscheme_get(&cs, "constant.string");
    CHECK(fb.fg == cs_str.fg);

    /* constant.number.hex falls back to constant */
    style_t fb2 = colorscheme_get(&cs, "constant.number.hex");
    style_t cst = colorscheme_get(&cs, "constant");
    CHECK(fb2.fg == cst.fg);

    /* Load actual monokai file */
    CHECK(colorscheme_load(&cs, "monokai"));
    style_t sl = colorscheme_get(&cs, "statusline");
    CHECK(COLOR_IS_RGB(sl.fg) && COLOR_IS_RGB(sl.bg));

    colorscheme_free(&cs);
}

int run_milestone2_tests(void)
{
    printf("\ntam-edit milestone 2 tests\n");
    test_pane_split_tree();
    printf("  [PASS] test_pane_split_tree\n");

    test_tabs_lifecycle();
    printf("  [PASS] test_tabs_lifecycle\n");

    test_softwrap_layout();
    printf("  [PASS] test_softwrap_layout\n");

    test_tam_ini_config();
    printf("  [PASS] test_tam_ini_config\n");

    test_micro_importer_json5();
    printf("  [PASS] test_micro_importer_json5\n");

    test_colorscheme_parser();
    printf("  [PASS] test_colorscheme_parser\n");

    printf("milestone 2 tests passed\n");
    return 0;
}
