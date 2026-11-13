#include "prompt.h"

#include "actions.h"
#include "buffer.h"
#include "complete.h"
#include "cursor.h"
#include "encoding.h"
#include "help_data.h"
#include "search.h"
#include "shell.h"
#include "syntax.h"

#include <tam.h>
#include <tam_uni.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void apply_buf_syntax_toggle(editor_t *ed, buffer_t *b, bool enabled)
{
    if (b == NULL) return;
    b->syntax_enabled = enabled;
    if (enabled) {
        const char *req = (b->filetype[0] != '\0' && strcmp(b->filetype, "unknown") != 0)
                              ? b->filetype : NULL;
        buffer_syntax_init(b, req, ed->config.detectlimit);
    }
}

static void history_push(prompt_history_t *h, const char *line)
{
    if (!h || !line || !line[0]) return;
    if (h->count > 0 && strcmp(h->items[h->count - 1], line) == 0) {
        h->cursor = -1;
        return;
    }
    if (h->count >= PROMPT_HIST_MAX) {
        free(h->items[0]);
        memmove(h->items, h->items + 1, (PROMPT_HIST_MAX - 1) * sizeof(char *));
        h->count = PROMPT_HIST_MAX - 1;
    }
    h->items[h->count] = strdup(line);
    if (h->items[h->count]) h->count++;
    h->cursor = -1;
}

static prompt_history_t *history_for_kind(editor_t *ed, prompt_kind_t kind)
{
    switch (kind) {
    case PROMPT_COMMAND:      return &ed->hist_cmd;
    case PROMPT_FIND:
    case PROMPT_FIND_LITERAL: return &ed->hist_find;
    case PROMPT_SHELL:        return &ed->hist_shell;
    default:                  return NULL;
    }
}

static void history_step(editor_t *ed, int dir)
{
    prompt_t *p = &ed->prompt;
    prompt_history_t *h = history_for_kind(ed, p->kind);
    if (!h || h->count == 0) return;

    if (dir < 0) {
        if (h->cursor == -1) {
            snprintf(h->saved_cur, sizeof(h->saved_cur), "%s", p->buf);
            h->cursor = h->count - 1;
        } else if (h->cursor > 0) {
            h->cursor--;
        } else {
            return;
        }
        snprintf(p->buf, sizeof(p->buf), "%s", h->items[h->cursor]);
    } else {
        if (h->cursor == -1) return;
        if (h->cursor + 1 < h->count) {
            h->cursor++;
            snprintf(p->buf, sizeof(p->buf), "%s", h->items[h->cursor]);
        } else {
            h->cursor = -1;
            snprintf(p->buf, sizeof(p->buf), "%s", h->saved_cur);
        }
    }
    p->len = strlen(p->buf);
    p->cx = edit_line_runes(p->buf, p->len);
    prompt_comp_clear(p);
    if (p->kind == PROMPT_FIND || p->kind == PROMPT_FIND_LITERAL) {
        search_inc_update(ed, true);
    }
}

void prompt_close(editor_t *ed)
{
    prompt_comp_clear(&ed->prompt);
    memset(&ed->prompt, 0, sizeof(ed->prompt));
}

void prompt_open_command(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_COMMAND;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "> ");
    ed->hist_cmd.cursor = -1;
}

void prompt_open_command_prefill(editor_t *ed, const char *prefill)
{
    prompt_open_command(ed);
    if (prefill && prefill[0]) {
        snprintf(ed->prompt.buf, sizeof(ed->prompt.buf), "%s", prefill);
        ed->prompt.len = strlen(ed->prompt.buf);
        ed->prompt.cx = edit_line_runes(ed->prompt.buf, ed->prompt.len);
    }
}

void prompt_open_find(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_FIND;
    ed->prompt.active = true;
    ed->search.literal = false;
    ed->hist_find.cursor = -1;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "Find: ");
    search_inc_begin(ed);
}

void prompt_open_find_literal(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_FIND_LITERAL;
    ed->prompt.active = true;
    ed->search.literal = true;
    ed->hist_find.cursor = -1;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "FindLiteral: ");
    search_inc_begin(ed);
}

void prompt_open_goto(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_GOTO;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "Goto line: ");
}

void prompt_open_save_as(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_SAVE_AS;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "Save as: ");
}

void prompt_open_quit_confirm(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_QUIT_CONFIRM;
    ed->prompt.active = true;
    const char *name = ed->path != NULL ? ed->path : "[No Name]";
    snprintf(ed->prompt.label, sizeof(ed->prompt.label),
             "Save changes to %s? (y/n/esc): ", name);
}

void prompt_open_shell(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_SHELL;
    ed->prompt.active = true;
    ed->hist_shell.cursor = -1;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "$ ");
}

void prompt_open_replace_confirm(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_REPLACE_CONFIRM;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label),
             "Replace? (y)es / (n)o / (a)ll / (esc)quit: ");
}

void prompt_open_confirm_reload(editor_t *ed, const char *path)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_CONFIRM_RELOAD;
    ed->prompt.active = true;
    if (path) {
        snprintf(ed->prompt.payload, sizeof(ed->prompt.payload), "%s", path);
    }
    const char *base = path ? strrchr(path, '/') : NULL;
    const char *disp = base ? (base + 1) : (path ? path : "file");
    snprintf(ed->prompt.label, sizeof(ed->prompt.label),
             "'%s' changed on disk. Reload? (y/n): ", disp);
}

void prompt_open_show_key(editor_t *ed)
{
    prompt_close(ed);
    ed->prompt.kind = PROMPT_SHOW_KEY;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "Press any key: ");
}

static void expand_tilde_path(const char *in, char *out, size_t out_size)
{
    if (in && in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && home[0]) {
            snprintf(out, out_size, "%s%s", home, in + 1);
            return;
        }
    }
    snprintf(out, out_size, "%s", in ? in : "");
}

static const char *skip_first_word(const char *cmd)
{
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    while (*cmd && !isspace((unsigned char)*cmd)) cmd++;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    return cmd;
}

static bool has_dirty_non_scratch_tab(editor_t *ed)
{
    for (size_t i = 0; i < ed->tabs.count; i++) {
        if (tab_is_dirty(ed->tabs.tabs[i])) return true;
    }
    return ed->dirty;
}

static void apply_setting_side_effects(editor_t *ed, const char *opt, const char *val)
{
    if (strcmp(opt, "colorscheme") == 0) {
        colorscheme_load(&ed->colorscheme, val);
    } else if (strcmp(opt, "syntax") == 0) {
        if (ed->active_view) {
            apply_buf_syntax_toggle(ed, ed->active_view->buf, ed->config.syntax);
        }
    } else if (strcmp(opt, "detectlimit") == 0) {
        if (ed->active_view && ed->active_view->buf &&
            strcmp(ed->active_view->buf->filetype, "unknown") == 0) {
            buffer_syntax_init(ed->active_view->buf, NULL, ed->config.detectlimit);
        }
    } else if (strcmp(opt, "softwrap") == 0) {
        if (ed->active_view) ed->active_view->softwrap = ed->config.softwrap;
    } else if (strcmp(opt, "wordwrap") == 0) {
        if (ed->active_view) ed->active_view->wordwrap = ed->config.wordwrap;
    } else if (strcmp(opt, "ignorecase") == 0) {
        search_invalidate(ed);
        ed->search.ignorecase = ed->config.ignorecase;
    }
}

static bool is_bool_setting(const editor_t *ed, const char *opt, bool *cur_val)
{
    char buf[64] = "";
    if (!config_get(&ed->config, opt, buf, sizeof(buf))) return false;
    if (strcmp(buf, "true") == 0) {
        *cur_val = true;
        return true;
    }
    if (strcmp(buf, "false") == 0) {
        *cur_val = false;
        return true;
    }
    return false;
}

static void cmd_set(editor_t *ed, int argc, char **argv, bool local)
{
    if (argc < 3) {
        editor_set_message(ed, "Usage: %s <option> <value>", argv[0]);
        return;
    }
    const char *opt = argv[1];
    const char *val = argv[2];

    if (local) {
        if (strcmp(opt, "softwrap") == 0) {
            bool b = (strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
            if (ed->active_view) ed->active_view->softwrap = b;
            editor_set_message(ed, "Setlocal softwrap = %s", b ? "true" : "false");
        } else if (strcmp(opt, "wordwrap") == 0) {
            bool b = (strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
            if (ed->active_view) ed->active_view->wordwrap = b;
            editor_set_message(ed, "Setlocal wordwrap = %s", b ? "true" : "false");
        } else if (strcmp(opt, "syntax") == 0) {
            bool b = (strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
            if (ed->active_view) apply_buf_syntax_toggle(ed, ed->active_view->buf, b);
            editor_set_message(ed, "Setlocal syntax = %s", b ? "true" : "false");
        } else if (strcmp(opt, "filetype") == 0) {
            if (ed->active_view && ed->active_view->buf) {
                buffer_syntax_init(ed->active_view->buf, val, ed->config.detectlimit);
                editor_set_message(ed, "Setlocal filetype = %s",
                                   ed->active_view->buf->filetype);
            }
        } else if (strcmp(opt, "fileformat") == 0) {
            if (strcasecmp(val, "dos") == 0 || strcasecmp(val, "crlf") == 0) {
                ed->crlf = true;
                editor_sync_to_active(ed);
                editor_set_message(ed, "Setlocal fileformat = dos");
            } else if (strcasecmp(val, "unix") == 0 || strcasecmp(val, "lf") == 0) {
                ed->crlf = false;
                editor_sync_to_active(ed);
                editor_set_message(ed, "Setlocal fileformat = unix");
            } else {
                editor_set_message(ed, "Invalid fileformat '%s' (use unix or dos)", val);
            }
        } else if (strcmp(opt, "readonly") == 0) {
            if (ed->active_view && ed->active_view->buf) {
                bool b = (strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0);
                ed->active_view->buf->readonly = b;
                editor_set_message(ed, "Setlocal readonly = %s", b ? "true" : "false");
            }
        } else if (strcmp(opt, "encoding") == 0) {
            if (!encoding_is_valid(val)) {
                editor_set_message(ed, "Unknown encoding '%s'", val);
                return;
            }
            if (ed->active_view && ed->active_view->buf) {
                encoding_canonical_name(val, ed->active_view->buf->encoding,
                                        sizeof(ed->active_view->buf->encoding));
                ed->active_view->buf->dirty = true;
                ed->dirty = true;
                editor_set_message(ed, "Setlocal encoding = %s",
                                   ed->active_view->buf->encoding);
            }
        } else {
            editor_set_message(ed, "Unsupported local option '%s'", opt);
        }
        return;
    }

    /* Global set */
    if (strcmp(opt, "filetype") == 0 || strcmp(opt, "readonly") == 0 ||
        strcmp(opt, "fileformat") == 0) {
        cmd_set(ed, argc, argv, true);
        return;
    }

    if (config_set(&ed->config, opt, val)) {
        char canonical[64] = "";
        config_get(&ed->config, opt, canonical, sizeof(canonical));
        apply_setting_side_effects(ed, opt, canonical[0] ? canonical : val);
        if (strcmp(opt, "encoding") == 0 && ed->active_view && ed->active_view->buf &&
            !ed->active_view->buf->dirty) {
            snprintf(ed->active_view->buf->encoding,
                     sizeof(ed->active_view->buf->encoding), "%s", ed->config.encoding);
        }
        config_save_setting(opt, canonical[0] ? canonical : val);
        editor_set_message(ed, "Set %s = %s", opt, canonical[0] ? canonical : val);
    } else {
        editor_set_message(ed, "Invalid option or value '%s %s'", opt, val);
    }
}

static void cmd_toggle(editor_t *ed, int argc, char **argv, bool local)
{
    if (argc < 2) {
        editor_set_message(ed, "Usage: %s <option>", argv[0]);
        return;
    }
    const char *opt = argv[1];

    if (local) {
        if (strcmp(opt, "softwrap") == 0 && ed->active_view) {
            ed->active_view->softwrap = !ed->active_view->softwrap;
            editor_set_message(ed, "Local softwrap toggled %s",
                               ed->active_view->softwrap ? "on" : "off");
        } else if (strcmp(opt, "wordwrap") == 0 && ed->active_view) {
            ed->active_view->wordwrap = !ed->active_view->wordwrap;
            editor_set_message(ed, "Local wordwrap toggled %s",
                               ed->active_view->wordwrap ? "on" : "off");
        } else if (strcmp(opt, "syntax") == 0 && ed->active_view && ed->active_view->buf) {
            bool b = !ed->active_view->buf->syntax_enabled;
            apply_buf_syntax_toggle(ed, ed->active_view->buf, b);
            editor_set_message(ed, "Local syntax toggled %s", b ? "on" : "off");
        } else if (strcmp(opt, "readonly") == 0 && ed->active_view && ed->active_view->buf) {
            ed->active_view->buf->readonly = !ed->active_view->buf->readonly;
            editor_set_message(ed, "Local readonly toggled %s",
                               ed->active_view->buf->readonly ? "on" : "off");
        } else if (strcmp(opt, "fileformat") == 0) {
            ed->crlf = !ed->crlf;
            editor_sync_to_active(ed);
            editor_set_message(ed, "Local fileformat toggled %s", ed->crlf ? "dos" : "unix");
        } else {
            editor_set_message(ed, "Cannot local-toggle '%s'", opt);
        }
        return;
    }

    if (strcmp(opt, "readonly") == 0 || strcmp(opt, "fileformat") == 0) {
        cmd_toggle(ed, argc, argv, true);
        return;
    }

    bool cur = false;
    if (!is_bool_setting(ed, opt, &cur)) {
        editor_set_message(ed, "Option '%s' cannot be toggled", opt);
        return;
    }
    const char *next_val = cur ? "false" : "true";
    if (config_set(&ed->config, opt, next_val)) {
        apply_setting_side_effects(ed, opt, next_val);
        config_save_setting(opt, next_val);
        editor_set_message(ed, "%s toggled %s", opt, cur ? "off" : "on");
    }
}

static void cmd_ls(editor_t *ed, const char *dir_arg)
{
    char path[512];
    expand_tilde_path(dir_arg && dir_arg[0] ? dir_arg : ".", path, sizeof(path));

    DIR *d = opendir(path);
    if (!d) {
        editor_set_message(ed, "ls: cannot open '%s': %s", path, strerror(errno));
        return;
    }

    size_t cap = 64, n = 0;
    char **names = malloc(cap * sizeof(char *));
    if (!names) {
        closedir(d);
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        struct stat st;
        bool is_dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        if (n >= cap) {
            cap *= 2;
            char **nn = realloc(names, cap * sizeof(char *));
            if (!nn) break;
            names = nn;
        }
        size_t nl = strlen(de->d_name);
        names[n] = malloc(nl + 2);
        if (names[n]) {
            snprintf(names[n], nl + 2, "%s%s", de->d_name, is_dir ? "/" : "");
            n++;
        }
    }
    closedir(d);

    /* Insertion sort */
    for (size_t i = 1; i < n; i++) {
        char *key = names[i];
        size_t j = i;
        while (j > 0 && strcmp(names[j - 1], key) > 0) {
            names[j] = names[j - 1];
            j--;
        }
        names[j] = key;
    }

    tam_str_t out = tam_str_new("");
    if (out) {
        for (size_t i = 0; i < n; i++) {
            out = tam_str_cat_printf(out, "%s\n", names[i]);
            free(names[i]);
        }
        free(names);
        editor_open_scratch(ed, "[ls]", out, false);
        tam_str_free(out);
    } else {
        for (size_t i = 0; i < n; i++) free(names[i]);
        free(names);
    }
}

static void cmd_bind(editor_t *ed, const char *key_str, int argc, char **argv)
{
    tam_key_type_t type = TAM_KEY_NONE;
    uint32_t cp = 0;
    unsigned mods = 0;
    if (!keybind_parse_string(key_str, &type, &cp, &mods)) {
        editor_set_message(ed, "Invalid key specifier '%s'", key_str);
        return;
    }

    tam_str_t act = tam_str_new("");
    if (!act) return;
    for (int i = 2; i < argc; i++) {
        if (i > 2) act = tam_str_cat(act, " ");
        act = tam_str_cat(act, argv[i]);
    }

    /* Update existing or prepend new custom binding */
    for (custom_keybind_t *kb = ed->keybindings; kb != NULL; kb = kb->next) {
        if (kb->type == type && kb->codepoint == cp && kb->mods == mods) {
            free(kb->action);
            kb->action = strdup(act);
            config_save_binding(key_str, act);
            editor_set_message(ed, "Bound %s -> %s", key_str, act);
            tam_str_free(act);
            return;
        }
    }

    custom_keybind_t *kb = malloc(sizeof(*kb));
    if (kb) {
        kb->type = type;
        kb->codepoint = cp;
        kb->mods = mods;
        kb->action = strdup(act);
        kb->next = ed->keybindings;
        ed->keybindings = kb;
        config_save_binding(key_str, act);
        editor_set_message(ed, "Bound %s -> %s", key_str, act);
    }
    tam_str_free(act);
}

static void cmd_unbind(editor_t *ed, const char *key_str)
{
    tam_key_type_t type = TAM_KEY_NONE;
    uint32_t cp = 0;
    unsigned mods = 0;
    if (!keybind_parse_string(key_str, &type, &cp, &mods)) {
        editor_set_message(ed, "Invalid key specifier '%s'", key_str);
        return;
    }

    custom_keybind_t **pp = &ed->keybindings;
    bool removed = false;
    while (*pp) {
        custom_keybind_t *cur = *pp;
        if (cur->type == type && cur->codepoint == cp && cur->mods == mods) {
            *pp = cur->next;
            free(cur->action);
            free(cur);
            removed = true;
            continue;
        }
        pp = &cur->next;
    }

    bool removed_ini = config_remove_binding(key_str);
    if (removed || removed_ini) {
        editor_set_message(ed, "Unbound %s", key_str);
    } else {
        editor_set_message(ed, "No custom binding for %s", key_str);
    }
}

void prompt_execute_command(editor_t *ed, const char *cmd)
{
    if (!ed || !cmd) return;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return;

    /* Preserve raw tail commands before shell_split */
    if (strncmp(cmd, "run", 3) == 0 && (cmd[3] == '\0' || isspace((unsigned char)cmd[3]))) {
        const char *rest = skip_first_word(cmd);
        bool interactive = false;
        if (strncmp(rest, "-i", 2) == 0 && (rest[2] == '\0' || isspace((unsigned char)rest[2]))) {
            interactive = true;
            rest += 2;
            while (*rest && isspace((unsigned char)*rest)) rest++;
        }
        if (!*rest && !interactive) {
            editor_set_message(ed, "Usage: run [-i] <command>");
            return;
        }
        if (interactive) {
            if (!ed->tty) {
                editor_set_message(ed, "Interactive shell unavailable without TTY");
                return;
            }
            int rc = shell_run_interactive(ed->tty, rest, true, true);
            editor_set_message(ed, "Shell exited (%d)", rc);
        } else {
            char err[192] = "";
            if (shell_bg_start(&ed->bg_job, rest, 30000, err, sizeof(err))) {
                editor_set_message(ed, "Running: %s", rest);
            } else {
                editor_set_message(ed, "%s", err[0] ? err : "Failed to start command");
            }
        }
        return;
    }

    if (strncmp(cmd, "textfilter", 10) == 0 &&
        (cmd[10] == '\0' || isspace((unsigned char)cmd[10]))) {
        const char *rest = skip_first_word(cmd);
        if (!*rest) {
            editor_set_message(ed, "Usage: textfilter <command>");
            return;
        }
        editor_textfilter(ed, rest);
        return;
    }

    char **argv = NULL;
    int argc = 0;
    if (!shell_split(cmd, &argv, &argc)) {
        editor_set_message(ed, "Unterminated quote in command");
        return;
    }
    if (argc == 0) {
        shell_split_free(argv, argc);
        return;
    }

    const char *op = argv[0];

    if (strcmp(op, "quit") == 0 || strcmp(op, "q") == 0) {
        if (has_dirty_non_scratch_tab(ed)) {
            prompt_open_quit_confirm(ed);
        } else {
            ed->running = false;
        }
    } else if (strcmp(op, "q!") == 0) {
        ed->running = false;
    } else if (strcmp(op, "save") == 0 || strcmp(op, "w") == 0 || strcmp(op, "write") == 0) {
        char err[192] = "";
        if (argc > 1) {
            char path[512];
            expand_tilde_path(argv[1], path, sizeof(path));
            editor_save_to_configured(ed, path, err, sizeof(err));
        } else {
            editor_save_configured(ed, err, sizeof(err));
        }
    } else if (strcmp(op, "save-as") == 0) {
        if (argc > 1) {
            char path[512], err[192] = "";
            expand_tilde_path(argv[1], path, sizeof(path));
            editor_save_to_configured(ed, path, err, sizeof(err));
        } else {
            prompt_open_save_as(ed);
        }
    } else if (strcmp(op, "open") == 0 || strcmp(op, "e") == 0) {
        if (argc > 1) {
            char path[512];
            expand_tilde_path(argv[1], path, sizeof(path));
            editor_load(ed, path);
        } else {
            editor_set_message(ed, "Usage: open <path>");
        }
    } else if (strcmp(op, "reopen") == 0) {
        editor_reopen(ed, argc > 1 ? argv[1] : NULL);
    } else if (strcmp(op, "cd") == 0) {
        char path[512];
        const char *target = (argc > 1) ? argv[1] : getenv("HOME");
        if (!target || !target[0]) target = ".";
        expand_tilde_path(target, path, sizeof(path));
        if (chdir(path) == 0) {
            char cwd[512];
            if (getcwd(cwd, sizeof(cwd))) {
                editor_set_message(ed, "cd: %s", cwd);
            }
        } else {
            editor_set_message(ed, "cd failed: %s", strerror(errno));
        }
    } else if (strcmp(op, "pwd") == 0) {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd))) {
            editor_set_message(ed, "%s", cwd);
        } else {
            editor_set_message(ed, "pwd failed: %s", strerror(errno));
        }
    } else if (strcmp(op, "rename") == 0) {
        if (argc < 2) {
            editor_set_message(ed, "Usage: rename <newpath>");
        } else if (!ed->active_view || !ed->active_view->buf || !ed->active_view->buf->path) {
            editor_set_message(ed, "No current file path to rename");
        } else {
            char newp[512];
            expand_tilde_path(argv[1], newp, sizeof(newp));
            buffer_t *b = ed->active_view->buf;
            if (rename(b->path, newp) == 0) {
                free(b->path);
                b->path = strdup(newp);
                ed->path = b->path;
                buffer_syntax_init(b, NULL, ed->config.detectlimit);
                editor_set_message(ed, "Renamed to %s", newp);
            } else {
                editor_set_message(ed, "Rename failed: %s", strerror(errno));
            }
        }
    } else if (strcmp(op, "delete") == 0) {
        char target[512] = "";
        if (argc > 1) {
            expand_tilde_path(argv[1], target, sizeof(target));
        } else if (ed->path) {
            snprintf(target, sizeof(target), "%s", ed->path);
        }
        if (!target[0]) {
            editor_set_message(ed, "Usage: delete <path>");
        } else if (unlink(target) == 0) {
            editor_set_message(ed, "Deleted %s", target);
        } else {
            editor_set_message(ed, "Delete failed: %s", strerror(errno));
        }
    } else if (strcmp(op, "ls") == 0) {
        cmd_ls(ed, argc > 1 ? argv[1] : ".");
    } else if (strcmp(op, "goto") == 0) {
        if (argc > 1) {
            long target = atol(argv[1]);
            if (target > 0) {
                ed->cx_line = (size_t)(target - 1);
                ed->cx_col = 0;
                ed->goal_col = 0;
                ed->sel.active = false;
                cursor_clamp(ed);
                editor_sync_to_active(ed);
                editor_set_message(ed, "Jumped to line %ld", target);
            }
        } else {
            prompt_open_goto(ed);
        }
    } else if (strcmp(op, "jump") == 0) {
        if (argc < 2) {
            editor_set_message(ed, "Usage: jump <+/-offset>");
        } else {
            long delta = atol(argv[1]);
            long nl = (long)ed->cx_line + delta;
            if (nl < 0) nl = 0;
            ed->cx_line = (size_t)nl;
            ed->cx_col = 0;
            ed->goal_col = 0;
            ed->sel.active = false;
            cursor_clamp(ed);
            editor_sync_to_active(ed);
            editor_set_message(ed, "Jumped to line %zu", ed->cx_line + 1);
        }
    } else if (strcmp(op, "retab") == 0) {
        if (ed->active_view && ed->active_view->buf) {
            if (ed->active_view->buf->readonly) {
                editor_set_message(ed, "Buffer is read-only");
            } else {
                editor_sync_to_active(ed);
                size_t chg = buffer_retab(ed->active_view->buf,
                                          ed->config.tabsize,
                                          ed->config.tabstospaces);
                editor_sync_from_active(ed);
                cursor_clamp(ed);
                editor_set_message(ed, "Retabbed %zu line(s)", chg);
            }
        }
    } else if (strcmp(op, "replace") == 0 || strcmp(op, "replaceall") == 0) {
        bool is_all = (strcmp(op, "replaceall") == 0);
        bool lit = false;
        bool in_sel = ed->sel.active;
        int pos = 1;
        while (pos < argc && argv[pos][0] == '-') {
            if (strcmp(argv[pos], "-l") == 0) lit = true;
            else if (strcmp(argv[pos], "-s") == 0) in_sel = true;
            else if (strcmp(argv[pos], "-a") == 0) is_all = true;
            pos++;
        }
        if (argc - pos < 2) {
            editor_set_message(ed, "Usage: %s [-l] [-s] <pattern> <replacement>", op);
        } else {
            const char *pat = argv[pos];
            const char *rep = argv[pos + 1];
            char err[128] = "";
            if (is_all) {
                long n = search_replace_all(ed, pat, rep, in_sel, lit, err, sizeof(err));
                if (n < 0) {
                    editor_set_message(ed, "Invalid regex: %s", err);
                } else {
                    editor_set_message(ed, "Replaced %ld occurrence(s)", n);
                }
            } else {
                if (search_replace_begin(ed, pat, rep, in_sel, err, sizeof(err))) {
                    prompt_open_replace_confirm(ed);
                } else if (err[0]) {
                    editor_set_message(ed, "Invalid regex: %s", err);
                }
            }
        }
    } else if (strcmp(op, "vsplit") == 0) {
        char path[512] = "";
        if (argc > 1) expand_tilde_path(argv[1], path, sizeof(path));
        editor_vsplit(ed, path[0] ? path : NULL);
    } else if (strcmp(op, "hsplit") == 0) {
        char path[512] = "";
        if (argc > 1) expand_tilde_path(argv[1], path, sizeof(path));
        editor_hsplit(ed, path[0] ? path : NULL);
    } else if (strcmp(op, "unsplit") == 0) {
        editor_unsplit(ed);
    } else if (strcmp(op, "tab") == 0) {
        char path[512] = "";
        if (argc > 1) expand_tilde_path(argv[1], path, sizeof(path));
        editor_add_tab(ed, path[0] ? path : NULL);
    } else if (strcmp(op, "tabnext") == 0) {
        editor_next_tab(ed);
    } else if (strcmp(op, "tabprev") == 0) {
        editor_prev_tab(ed);
    } else if (strcmp(op, "tabclose") == 0) {
        editor_close_tab(ed);
    } else if (strcmp(op, "tabmove") == 0) {
        if (argc < 2 || ed->tabs.count <= 1) {
            editor_set_message(ed, "Usage: tabmove <index | +n | -n>");
        } else {
            long target = 0;
            if (argv[1][0] == '+' || argv[1][0] == '-') {
                target = (long)ed->tabs.active + atol(argv[1]);
            } else {
                target = atol(argv[1]) - 1;
            }
            if (target < 0) target = 0;
            if ((size_t)target >= ed->tabs.count) target = (long)(ed->tabs.count - 1);
            tab_list_move(&ed->tabs, ed->tabs.active, (size_t)target);
            editor_set_message(ed, "Moved tab to %zu", ed->tabs.active + 1);
        }
    } else if (strcmp(op, "tabswitch") == 0) {
        if (argc < 2) {
            editor_set_message(ed, "Usage: tabswitch <index|name>");
        } else {
            char *end = NULL;
            long idx = strtol(argv[1], &end, 10);
            if (end != argv[1] && *end == '\0' && idx >= 1 && (size_t)idx <= ed->tabs.count) {
                ed->tabs.active = (size_t)(idx - 1);
                editor_set_active_view(ed, ed->tabs.tabs[ed->tabs.active]->active_view);
            } else {
                bool switched = false;
                for (size_t i = 0; i < ed->tabs.count; i++) {
                    char title[128] = "";
                    tab_get_title(ed->tabs.tabs[i], title, sizeof(title));
                    if (strstr(title, argv[1]) != NULL) {
                        ed->tabs.active = i;
                        editor_set_active_view(ed, ed->tabs.tabs[i]->active_view);
                        switched = true;
                        break;
                    }
                }
                if (!switched) {
                    editor_set_message(ed, "No tab matching '%s'", argv[1]);
                }
            }
        }
    } else if (strcmp(op, "colorscheme") == 0) {
        if (argc > 1) {
            if (colorscheme_load(&ed->colorscheme, argv[1])) {
                snprintf(ed->config.colorscheme, sizeof(ed->config.colorscheme), "%s", argv[1]);
                config_save_setting("colorscheme", argv[1]);
                editor_set_message(ed, "Loaded colorscheme '%s'", argv[1]);
            } else {
                editor_set_message(ed, "Could not load colorscheme '%s'", argv[1]);
            }
        }
    } else if (strcmp(op, "set") == 0) {
        cmd_set(ed, argc, argv, false);
    } else if (strcmp(op, "setlocal") == 0) {
        cmd_set(ed, argc, argv, true);
    } else if (strcmp(op, "toggle") == 0) {
        cmd_toggle(ed, argc, argv, false);
    } else if (strcmp(op, "togglelocal") == 0) {
        cmd_toggle(ed, argc, argv, true);
    } else if (strcmp(op, "reset") == 0) {
        if (argc < 2) {
            editor_set_message(ed, "Usage: reset <option>");
        } else {
            char defval[64] = "";
            if (config_get_default(argv[1], defval, sizeof(defval)) &&
                config_set(&ed->config, argv[1], defval)) {
                apply_setting_side_effects(ed, argv[1], defval);
                config_save_setting(argv[1], defval);
                editor_set_message(ed, "Reset %s = %s", argv[1], defval);
            } else {
                editor_set_message(ed, "Unknown option '%s'", argv[1]);
            }
        }
    } else if (strcmp(op, "show") == 0) {
        if (argc < 2) {
            editor_set_message(ed, "Usage: show <option>");
        } else {
            const char *opt = argv[1];
            char val[128] = "";
            if (strcmp(opt, "filetype") == 0 && ed->active_view && ed->active_view->buf) {
                editor_set_message(ed, "filetype = %s", ed->active_view->buf->filetype);
            } else if (strcmp(opt, "fileformat") == 0) {
                editor_set_message(ed, "fileformat = %s", ed->crlf ? "dos" : "unix");
            } else if (strcmp(opt, "readonly") == 0 && ed->active_view && ed->active_view->buf) {
                editor_set_message(ed, "readonly = %s",
                                   ed->active_view->buf->readonly ? "true" : "false");
            } else if (strcmp(opt, "encoding") == 0 && ed->active_view && ed->active_view->buf) {
                editor_set_message(ed, "encoding = %s", ed->active_view->buf->encoding);
            } else if (config_get(&ed->config, opt, val, sizeof(val))) {
                editor_set_message(ed, "%s = %s", opt, val);
            } else {
                editor_set_message(ed, "Unknown option '%s'", opt);
            }
        }
    } else if (strcmp(op, "bind") == 0) {
        if (argc < 3) {
            editor_set_message(ed, "Usage: bind <key> <action>");
        } else {
            cmd_bind(ed, argv[1], argc, argv);
        }
    } else if (strcmp(op, "unbind") == 0) {
        if (argc < 2) {
            editor_set_message(ed, "Usage: unbind <key>");
        } else {
            cmd_unbind(ed, argv[1]);
        }
    } else if (strcmp(op, "showkey") == 0) {
        prompt_open_show_key(ed);
    } else if (strcmp(op, "help") == 0) {
        const char *topic = (argc > 1) ? argv[1] : "help";
        if (strcmp(topic, "keybindings") == 0 || strcmp(topic, "keys") == 0) {
            actions_open_keybindings_help(ed);
            shell_split_free(argv, argc);
            return;
        }
        const char *found = NULL;
        for (size_t i = 0; i < g_embedded_help_count; i++) {
            if (strcmp(g_embedded_help[i].name, topic) == 0) {
                found = g_embedded_help[i].data;
                break;
            }
        }
        if (found) {
            char title[96];
            snprintf(title, sizeof(title), "help:%s", topic);
            editor_open_scratch(ed, title, found, true);
        } else {
            editor_set_message(ed, "Unknown help topic '%s'", topic);
        }
    } else {
        editor_set_message(ed, "Unknown command: %s", op);
    }

    shell_split_free(argv, argc);
}

static void submit_prompt(editor_t *ed)
{
    prompt_t *p = &ed->prompt;
    prompt_kind_t kind = p->kind;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", p->buf);

    prompt_history_t *h = history_for_kind(ed, kind);
    if (h && buf[0]) history_push(h, buf);

    prompt_close(ed);

    switch (kind) {
    case PROMPT_COMMAND:
        prompt_execute_command(ed, buf);
        break;
    case PROMPT_SHELL:
        if (buf[0]) {
            char full[520];
            snprintf(full, sizeof(full), "run %s", buf);
            prompt_execute_command(ed, full);
        }
        break;
    case PROMPT_FIND:
    case PROMPT_FIND_LITERAL:
        if (buf[0] != '\0') {
            snprintf(ed->search.query, sizeof(ed->search.query), "%.255s", buf);
            ed->search.literal = (kind == PROMPT_FIND_LITERAL);
        }
        search_inc_commit(ed);
        if (ed->search.query[0] != '\0') {
            if (!ed->config.incsearch || buf[0] == '\0') {
                search_find_next(ed);
            } else if (!ed->search.has_match) {
                editor_set_message(ed, "Pattern not found: %s", ed->search.query);
            }
        }
        break;
    case PROMPT_GOTO: {
        long target = atol(buf);
        if (target > 0 && ed->doc.count > 0) {
            ed->cx_line = (size_t)(target - 1);
            ed->cx_col  = 0;
            ed->goal_col = 0;
            ed->sel.active = false;
            cursor_clamp(ed);
            editor_sync_to_active(ed);
            editor_set_message(ed, "Jumped to line %ld", target);
        }
        break;
    }
    case PROMPT_SAVE_AS:
        if (buf[0] != '\0') {
            char path[512], err[192] = "";
            expand_tilde_path(buf, path, sizeof(path));
            editor_save_to_configured(ed, path, err, sizeof(err));
        }
        break;
    default:
        break;
    }
}

bool prompt_handle_key(editor_t *ed, const tam_key_event_t *ev)
{
    prompt_t *p = &ed->prompt;
    if (!p->active) return false;

    if (p->kind == PROMPT_SHOW_KEY) {
        char kname[64] = "";
        keybind_format_string(ev->type, ev->codepoint, ev->mods, kname, sizeof(kname));
        const char *act = actions_lookup_key_description(ed, ev);
        prompt_close(ed);
        if (act) {
            editor_set_message(ed, "%s -> %s", kname, act);
        } else {
            editor_set_message(ed, "%s -> (unbound)", kname);
        }
        return true;
    }

    if (p->kind == PROMPT_REPLACE_CONFIRM) {
        if (ev->type == TAM_KEY_ESC) {
            search_replace_step(ed, 27);
            return true;
        }
        if (ev->type == TAM_KEY_CHAR && ev->mods == 0) {
            search_replace_step(ed, (char)ev->codepoint);
            return true;
        }
        return true;
    }

    if (p->kind == PROMPT_CONFIRM_RELOAD) {
        if (ev->type == TAM_KEY_ESC ||
            (ev->type == TAM_KEY_CHAR && (ev->codepoint == 'n' || ev->codepoint == 'N'))) {
            if (ed->active_view && ed->active_view->buf) {
                ed->active_view->buf->mtime_valid = false;
            }
            prompt_close(ed);
            editor_set_message(ed, "Kept current buffer");
            return true;
        }
        if (ev->type == TAM_KEY_CHAR && (ev->codepoint == 'y' || ev->codepoint == 'Y')) {
            prompt_close(ed);
            editor_reopen(ed, NULL);
            return true;
        }
        return true;
    }

    if (p->kind == PROMPT_QUIT_CONFIRM) {
        if (ev->type == TAM_KEY_ESC) {
            prompt_close(ed);
            editor_set_message(ed, "Cancelled");
            return true;
        }
        if (ev->type == TAM_KEY_CHAR) {
            if (ev->codepoint == 'y' || ev->codepoint == 'Y') {
                prompt_close(ed);
                if (ed->path != NULL && ed->path[0] != '\0') {
                    char err[192] = "";
                    if (editor_save_configured(ed, err, sizeof(err))) {
                        ed->running = false;
                    }
                } else {
                    prompt_open_save_as(ed);
                }
                return true;
            }
            if (ev->codepoint == 'n' || ev->codepoint == 'N') {
                prompt_close(ed);
                ed->running = false;
                return true;
            }
        }
        return true;
    }

    if (ev->type == TAM_KEY_ESC) {
        if (p->kind == PROMPT_FIND || p->kind == PROMPT_FIND_LITERAL) {
            search_inc_cancel(ed);
        }
        prompt_close(ed);
        editor_set_message(ed, "Cancelled");
        return true;
    }

    if (ev->type == TAM_KEY_ENTER) {
        submit_prompt(ed);
        return true;
    }

    if (ev->type == TAM_KEY_TAB && (p->kind == PROMPT_COMMAND || p->kind == PROMPT_SAVE_AS)) {
        int dir = (ev->mods & TAM_MOD_SHIFT) ? -1 : 1;
        prompt_complete_step(ed, dir);
        return true;
    }

    if (ev->type == TAM_KEY_UP) {
        history_step(ed, -1);
        return true;
    }

    if (ev->type == TAM_KEY_DOWN) {
        history_step(ed, 1);
        return true;
    }

    if (ev->type == TAM_KEY_LEFT) {
        if (p->cx > 0) p->cx--;
        return true;
    }

    if (ev->type == TAM_KEY_RIGHT) {
        size_t runes = edit_line_runes(p->buf, p->len);
        if (p->cx < runes) p->cx++;
        return true;
    }

    if (ev->type == TAM_KEY_HOME) {
        p->cx = 0;
        return true;
    }

    if (ev->type == TAM_KEY_END) {
        p->cx = edit_line_runes(p->buf, p->len);
        return true;
    }

    if (ev->type == TAM_KEY_BACKSPACE) {
        prompt_comp_clear(p);
        if (p->cx > 0) {
            size_t b_from = edit_rune_to_byte(p->buf, p->len, p->cx - 1);
            size_t b_to   = edit_rune_to_byte(p->buf, p->len, p->cx);
            memmove(p->buf + b_from, p->buf + b_to, p->len - b_to + 1);
            p->len -= (b_to - b_from);
            p->cx--;
            if (p->kind == PROMPT_FIND || p->kind == PROMPT_FIND_LITERAL) {
                search_inc_update(ed, true);
            }
        }
        return true;
    }

    if (ev->type == TAM_KEY_DELETE) {
        prompt_comp_clear(p);
        size_t runes = edit_line_runes(p->buf, p->len);
        if (p->cx < runes) {
            size_t b_from = edit_rune_to_byte(p->buf, p->len, p->cx);
            size_t b_to   = edit_rune_to_byte(p->buf, p->len, p->cx + 1);
            memmove(p->buf + b_from, p->buf + b_to, p->len - b_to + 1);
            p->len -= (b_to - b_from);
            if (p->kind == PROMPT_FIND || p->kind == PROMPT_FIND_LITERAL) {
                search_inc_update(ed, true);
            }
        }
        return true;
    }

    if (ev->type == TAM_KEY_CHAR && (ev->mods == 0 || ev->mods == TAM_MOD_SHIFT)) {
        prompt_comp_clear(p);
        char utf8[4];
        size_t n = tam_utf8_encode(ev->codepoint, utf8);
        if (p->len + n < sizeof(p->buf)) {
            size_t b_at = edit_rune_to_byte(p->buf, p->len, p->cx);
            memmove(p->buf + b_at + n, p->buf + b_at, p->len - b_at + 1);
            memcpy(p->buf + b_at, utf8, n);
            p->len += n;
            p->cx++;
            if (p->kind == PROMPT_FIND || p->kind == PROMPT_FIND_LITERAL) {
                search_inc_update(ed, true);
            }
        }
        return true;
    }

    return true;
}
