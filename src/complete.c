#include "complete.h"

#include "colorschemes_data.h"
#include "help_data.h"
#include "syntax_data.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const k_commands[] = {
    "bind", "cd", "colorscheme", "delete", "goto", "help", "hsplit", "jump",
    "ls", "open", "pwd", "quit", "rename", "reopen", "replace", "replaceall",
    "reset", "retab", "run", "save", "save-as", "set", "setlocal", "show",
    "showkey", "tab", "tabmove", "tabswitch", "textfilter", "toggle",
    "togglelocal", "unbind", "vsplit", "write"
};

static const char *const k_encodings[] = {
    "euc-jp", "iso-8859-1", "iso-8859-15", "shift_jis",
    "us-ascii", "utf-16be", "utf-16le", "utf-8",
    "windows-1251", "windows-1252"
};

void prompt_comp_clear(prompt_t *p)
{
    if (!p) return;
    if (p->comp_items) {
        for (size_t i = 0; i < p->comp_count; i++) {
            free(p->comp_items[i]);
        }
        free(p->comp_items);
    }
    p->comp_items = NULL;
    p->comp_count = 0;
    p->comp_idx = 0;
    p->comp_token_start = 0;
    p->comp_active = false;
}

static int cmp_str_ptr(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

static bool add_comp_item(prompt_t *p, const char *s)
{
    if (!s) return false;
    for (size_t i = 0; i < p->comp_count; i++) {
        if (strcmp(p->comp_items[i], s) == 0) return true;
    }
    char **ni = realloc(p->comp_items, (p->comp_count + 1) * sizeof(char *));
    if (!ni) return false;
    p->comp_items = ni;
    p->comp_items[p->comp_count] = strdup(s);
    if (!p->comp_items[p->comp_count]) return false;
    p->comp_count++;
    return true;
}

static void add_matching_from_list(prompt_t *p, const char *const *list, size_t n,
                                   const char *prefix)
{
    size_t plen = strlen(prefix);
    for (size_t i = 0; i < n; i++) {
        if (strncmp(list[i], prefix, plen) == 0) {
            add_comp_item(p, list[i]);
        }
    }
}

static void add_file_completions(prompt_t *p, const char *prefix)
{
    char expanded[512];
    const char *home = getenv("HOME");
    bool tilde_prefix = false;

    if (prefix[0] == '~' && (prefix[1] == '/' || prefix[1] == '\0') && home && home[0]) {
        snprintf(expanded, sizeof(expanded), "%s%s", home, prefix + 1);
        tilde_prefix = true;
    } else {
        snprintf(expanded, sizeof(expanded), "%s", prefix);
    }

    char dir_path[512] = ".";
    const char *base_prefix = prefix;
    const char *slash_orig = strrchr(prefix, '/');
    const char *slash_exp = strrchr(expanded, '/');

    char display_dir[512] = "";
    if (slash_exp && slash_orig) {
        size_t dlen = (size_t)(slash_exp - expanded);
        if (dlen == 0) {
            snprintf(dir_path, sizeof(dir_path), "/");
        } else {
            if (dlen >= sizeof(dir_path)) dlen = sizeof(dir_path) - 1;
            memcpy(dir_path, expanded, dlen);
            dir_path[dlen] = '\0';
        }
        size_t orig_dlen = (size_t)(slash_orig - prefix) + 1;
        if (orig_dlen >= sizeof(display_dir)) orig_dlen = sizeof(display_dir) - 1;
        memcpy(display_dir, prefix, orig_dlen);
        display_dir[orig_dlen] = '\0';
        base_prefix = slash_orig + 1;
    } else {
        (void)tilde_prefix;
    }

    DIR *d = opendir(dir_path);
    if (!d) return;

    size_t blen = strlen(base_prefix);
    bool show_hidden = (base_prefix[0] == '.');
    struct dirent *de;

    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (!show_hidden && de->d_name[0] == '.') continue;
        if (strncmp(de->d_name, base_prefix, blen) != 0) continue;

        char full[1024];
        if (strcmp(dir_path, "/") == 0) {
            snprintf(full, sizeof(full), "/%s", de->d_name);
        } else if (strcmp(dir_path, ".") == 0 && !slash_orig) {
            snprintf(full, sizeof(full), "%s", de->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
        }

        struct stat st;
        bool is_dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));

        char candidate[1024];
        snprintf(candidate, sizeof(candidate), "%s%s%s",
                 display_dir, de->d_name, is_dir ? "/" : "");
        add_comp_item(p, candidate);
    }
    closedir(d);

    if (p->comp_count > 1) {
        qsort(p->comp_items, p->comp_count, sizeof(char *), cmp_str_ptr);
    }
}

static bool cmd_takes_file(const char *cmd)
{
    return strcmp(cmd, "open") == 0 || strcmp(cmd, "vsplit") == 0 ||
           strcmp(cmd, "hsplit") == 0 || strcmp(cmd, "tab") == 0 ||
           strcmp(cmd, "save") == 0 || strcmp(cmd, "save-as") == 0 ||
           strcmp(cmd, "write") == 0 || strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "ls") == 0 || strcmp(cmd, "rename") == 0 ||
           strcmp(cmd, "delete") == 0;
}

static void build_option_value_completions(prompt_t *p, const char *opt, const char *prefix)
{
    size_t plen = strlen(prefix);
    if (strcmp(opt, "colorscheme") == 0) {
        for (size_t i = 0; i < g_embedded_colorschemes_count; i++) {
            if (strncmp(g_embedded_colorschemes[i].name, prefix, plen) == 0) {
                add_comp_item(p, g_embedded_colorschemes[i].name);
            }
        }
    } else if (strcmp(opt, "filetype") == 0) {
        for (size_t i = 0; i < g_embedded_syntax_defs_count; i++) {
            if (strncmp(g_embedded_syntax_defs[i].filetype, prefix, plen) == 0) {
                add_comp_item(p, g_embedded_syntax_defs[i].filetype);
            }
        }
        if (p->comp_count > 1) {
            qsort(p->comp_items, p->comp_count, sizeof(char *), cmp_str_ptr);
        }
    } else if (strcmp(opt, "encoding") == 0) {
        add_matching_from_list(p, k_encodings, sizeof(k_encodings) / sizeof(k_encodings[0]), prefix);
    } else if (strcmp(opt, "clipboard") == 0) {
        const char *vals[] = {"external", "internal", "terminal"};
        add_matching_from_list(p, vals, 3, prefix);
    } else if (strcmp(opt, "reload") == 0) {
        const char *vals[] = {"auto", "disabled", "prompt"};
        add_matching_from_list(p, vals, 3, prefix);
    } else if (strcmp(opt, "helpsplit") == 0) {
        const char *vals[] = {"hsplit", "vsplit"};
        add_matching_from_list(p, vals, 2, prefix);
    } else if (strcmp(opt, "tabbar") == 0 || strcmp(opt, "truecolor") == 0) {
        const char *vals[] = {"auto", "false", "true"};
        add_matching_from_list(p, vals, 3, prefix);
    } else if (strcmp(opt, "fileformat") == 0) {
        const char *vals[] = {"dos", "unix"};
        add_matching_from_list(p, vals, 2, prefix);
    } else if (strcmp(opt, "tabsize") == 0) {
        const char *vals[] = {"2", "4", "8"};
        add_matching_from_list(p, vals, 3, prefix);
    } else {
        const char *bools[] = {"false", "true"};
        add_matching_from_list(p, bools, 2, prefix);
    }
}

static void build_completions(editor_t *ed)
{
    prompt_t *p = &ed->prompt;
    prompt_comp_clear(p);

    if (p->kind == PROMPT_SAVE_AS) {
        p->comp_token_start = 0;
        add_file_completions(p, p->buf);
        return;
    }

    if (p->kind != PROMPT_COMMAND) return;

    /* Count words before cursor/end */
    const char *s = p->buf;
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;

    /* Find token boundaries up to p->len */
    const char *tokens[4] = {0};
    size_t tok_starts[4] = {0};
    size_t tok_lens[4] = {0};
    int ntok = 0;

    while (s[i] && ntok < 4) {
        size_t start = i;
        while (s[i] && !isspace((unsigned char)s[i])) i++;
        tokens[ntok] = s + start;
        tok_starts[ntok] = start;
        tok_lens[ntok] = i - start;
        ntok++;
        while (s[i] && isspace((unsigned char)s[i])) i++;
    }

    bool trailing_space = (p->len > 0 && isspace((unsigned char)s[p->len - 1]));

    if (ntok == 0 || (ntok == 1 && !trailing_space)) {
        char prefix[128] = "";
        if (ntok == 1) {
            size_t cl = tok_lens[0] < sizeof(prefix) - 1 ? tok_lens[0] : sizeof(prefix) - 1;
            memcpy(prefix, tokens[0], cl);
            prefix[cl] = '\0';
            p->comp_token_start = tok_starts[0];
        } else {
            p->comp_token_start = p->len;
        }
        add_matching_from_list(p, k_commands, sizeof(k_commands) / sizeof(k_commands[0]), prefix);
        return;
    }

    char cmd[64] = "";
    size_t clen = tok_lens[0] < sizeof(cmd) - 1 ? tok_lens[0] : sizeof(cmd) - 1;
    memcpy(cmd, tokens[0], clen);
    cmd[clen] = '\0';

    int arg_idx = trailing_space ? ntok : (ntok - 1);
    char prefix[256] = "";
    if (!trailing_space && arg_idx < ntok) {
        size_t pl = tok_lens[arg_idx] < sizeof(prefix) - 1 ? tok_lens[arg_idx] : sizeof(prefix) - 1;
        memcpy(prefix, tokens[arg_idx], pl);
        prefix[pl] = '\0';
        p->comp_token_start = tok_starts[arg_idx];
    } else {
        p->comp_token_start = p->len;
    }

    if (arg_idx == 1) {
        if (cmd_takes_file(cmd)) {
            add_file_completions(p, prefix);
        } else if (strcmp(cmd, "help") == 0) {
            size_t plen = strlen(prefix);
            for (size_t k = 0; k < g_embedded_help_count; k++) {
                if (strncmp(g_embedded_help[k].name, prefix, plen) == 0) {
                    add_comp_item(p, g_embedded_help[k].name);
                }
            }
        } else if (strcmp(cmd, "colorscheme") == 0) {
            build_option_value_completions(p, "colorscheme", prefix);
        } else if (strcmp(cmd, "reopen") == 0) {
            build_option_value_completions(p, "encoding", prefix);
        } else if (strcmp(cmd, "set") == 0 || strcmp(cmd, "setlocal") == 0 ||
                   strcmp(cmd, "toggle") == 0 || strcmp(cmd, "togglelocal") == 0 ||
                   strcmp(cmd, "reset") == 0 || strcmp(cmd, "show") == 0) {
            size_t opt_n = 0;
            const char *const *opts = config_option_names(&opt_n);
            add_matching_from_list(p, opts, opt_n, prefix);
            if (strcmp(cmd, "set") == 0 || strcmp(cmd, "setlocal") == 0 || strcmp(cmd, "show") == 0) {
                const char *extra[] = {"fileformat", "filetype", "readonly"};
                add_matching_from_list(p, extra, 3, prefix);
            } else if (strcmp(cmd, "toggle") == 0 || strcmp(cmd, "togglelocal") == 0) {
                if (strncmp("readonly", prefix, strlen(prefix)) == 0) {
                    add_comp_item(p, "readonly");
                }
            }
            if (p->comp_count > 1) {
                qsort(p->comp_items, p->comp_count, sizeof(char *), cmp_str_ptr);
            }
        }
    } else if (arg_idx == 2) {
        if (strcmp(cmd, "set") == 0 || strcmp(cmd, "setlocal") == 0) {
            char opt[64] = "";
            size_t ol = tok_lens[1] < sizeof(opt) - 1 ? tok_lens[1] : sizeof(opt) - 1;
            memcpy(opt, tokens[1], ol);
            opt[ol] = '\0';
            build_option_value_completions(p, opt, prefix);
        } else if (strcmp(cmd, "rename") == 0) {
            add_file_completions(p, prefix);
        }
    }
}

bool prompt_complete_step(editor_t *ed, int dir)
{
    if (!ed || !ed->prompt.active) return false;
    prompt_t *p = &ed->prompt;

    if (!p->comp_active) {
        build_completions(ed);
        if (p->comp_count == 0) return false;
        p->comp_active = true;
        p->comp_idx = (dir >= 0) ? 0 : (p->comp_count - 1);
    } else {
        if (p->comp_count == 0) return false;
        if (dir >= 0) {
            p->comp_idx = (p->comp_idx + 1) % p->comp_count;
        } else {
            p->comp_idx = (p->comp_idx + p->comp_count - 1) % p->comp_count;
        }
    }

    const char *cand = p->comp_items[p->comp_idx];
    size_t start = p->comp_token_start;
    if (start > sizeof(p->buf) - 1) start = sizeof(p->buf) - 1;

    snprintf(p->buf + start, sizeof(p->buf) - start, "%s", cand);
    p->len = strlen(p->buf);
    p->cx = edit_line_runes(p->buf, p->len);

    if (p->comp_count > 1) {
        editor_set_message(ed, "[%zu/%zu] %s", p->comp_idx + 1, p->comp_count, cand);
    } else {
        ed->message[0] = '\0';
    }
    return true;
}
