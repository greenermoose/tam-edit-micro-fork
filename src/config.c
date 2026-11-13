#include "config.h"
#include "encoding.h"

#include <tam.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void config_init_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->colorscheme, "default", sizeof(cfg->colorscheme) - 1);
    cfg->softwrap     = false;
    cfg->wordwrap     = true;
    cfg->tabsize      = 4;
    cfg->tabstospaces = false;
    cfg->autoindent   = true;
    cfg->cursorline   = true;
    cfg->ruler        = true;
    cfg->statusline   = true;
    cfg->tabbar       = TABBAR_AUTO;
    cfg->scrollsync   = false;
    cfg->splitbottom  = true;
    cfg->splitright   = true;
    cfg->eofnewline   = true;
    cfg->rmtrailingws = false;
    cfg->truecolor    = TRUECOLOR_AUTO;
    strncpy(cfg->clipboard, "terminal", sizeof(cfg->clipboard) - 1);
    cfg->syntax       = true;
    cfg->detectlimit  = 100;
    strncpy(cfg->encoding, "utf-8", sizeof(cfg->encoding) - 1);
    cfg->ignorecase   = true;
    cfg->incsearch    = true;
    cfg->hlsearch     = false;
    cfg->mkparents    = false;
    cfg->scrollmargin = 3;
    cfg->tabmovement  = false;
    cfg->smartpaste   = true;
    cfg->overwrite    = false;
    strncpy(cfg->reload, "prompt", sizeof(cfg->reload) - 1);
    strncpy(cfg->helpsplit, "hsplit", sizeof(cfg->helpsplit) - 1);
}

void keybinds_free(custom_keybind_t *kb)
{
    while (kb != NULL) {
        custom_keybind_t *next = kb->next;
        free(kb->action);
        free(kb);
        kb = next;
    }
}

static bool parse_bool(const char *val, bool *out)
{
    if (strcasecmp(val, "true") == 0 ||
        strcasecmp(val, "on") == 0 ||
        strcasecmp(val, "yes") == 0 ||
        strcmp(val, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcasecmp(val, "false") == 0 ||
        strcasecmp(val, "off") == 0 ||
        strcasecmp(val, "no") == 0 ||
        strcmp(val, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

bool config_set(config_t *cfg, const char *name, const char *value)
{
    if (cfg == NULL || name == NULL || value == NULL) return false;

    if (strcmp(name, "colorscheme") == 0) {
        strncpy(cfg->colorscheme, value, sizeof(cfg->colorscheme) - 1);
        cfg->colorscheme[sizeof(cfg->colorscheme) - 1] = '\0';
        return true;
    }
    if (strcmp(name, "softwrap") == 0) {
        return parse_bool(value, &cfg->softwrap);
    }
    if (strcmp(name, "wordwrap") == 0) {
        return parse_bool(value, &cfg->wordwrap);
    }
    if (strcmp(name, "tabsize") == 0) {
        char *end = NULL;
        long v = strtol(value, &end, 10);
        if (end != value && *end == '\0' && v >= 1 && v <= 16) {
            cfg->tabsize = (int)v;
            return true;
        }
        return false;
    }
    if (strcmp(name, "tabstospaces") == 0) {
        return parse_bool(value, &cfg->tabstospaces);
    }
    if (strcmp(name, "autoindent") == 0) {
        return parse_bool(value, &cfg->autoindent);
    }
    if (strcmp(name, "cursorline") == 0) {
        return parse_bool(value, &cfg->cursorline);
    }
    if (strcmp(name, "ruler") == 0) {
        return parse_bool(value, &cfg->ruler);
    }
    if (strcmp(name, "statusline") == 0) {
        return parse_bool(value, &cfg->statusline);
    }
    if (strcmp(name, "tabbar") == 0) {
        if (strcasecmp(value, "auto") == 0) {
            cfg->tabbar = TABBAR_AUTO;
            return true;
        }
        bool b = false;
        if (parse_bool(value, &b)) {
            cfg->tabbar = b ? TABBAR_ON : TABBAR_OFF;
            return true;
        }
        return false;
    }
    if (strcmp(name, "scrollsync") == 0) {
        return parse_bool(value, &cfg->scrollsync);
    }
    if (strcmp(name, "splitbottom") == 0) {
        return parse_bool(value, &cfg->splitbottom);
    }
    if (strcmp(name, "splitright") == 0) {
        return parse_bool(value, &cfg->splitright);
    }
    if (strcmp(name, "eofnewline") == 0) {
        return parse_bool(value, &cfg->eofnewline);
    }
    if (strcmp(name, "rmtrailingws") == 0) {
        return parse_bool(value, &cfg->rmtrailingws);
    }
    if (strcmp(name, "truecolor") == 0) {
        if (strcasecmp(value, "auto") == 0) {
            cfg->truecolor = TRUECOLOR_AUTO;
            return true;
        }
        bool b = false;
        if (parse_bool(value, &b)) {
            cfg->truecolor = b ? TRUECOLOR_ON : TRUECOLOR_OFF;
            return true;
        }
        return false;
    }
    if (strcmp(name, "clipboard") == 0) {
        if (strcmp(value, "terminal") == 0 ||
            strcmp(value, "external") == 0 ||
            strcmp(value, "internal") == 0) {
            strncpy(cfg->clipboard, value, sizeof(cfg->clipboard) - 1);
            cfg->clipboard[sizeof(cfg->clipboard) - 1] = '\0';
            return true;
        }
        return false;
    }
    if (strcmp(name, "syntax") == 0) {
        return parse_bool(value, &cfg->syntax);
    }
    if (strcmp(name, "detectlimit") == 0) {
        char *end = NULL;
        long v = strtol(value, &end, 10);
        if (end != value && *end == '\0' && v >= 0 && v <= 100000) {
            cfg->detectlimit = (int)v;
            return true;
        }
        return false;
    }
    if (strcmp(name, "encoding") == 0) {
        if (!encoding_is_valid(value)) return false;
        encoding_canonical_name(value, cfg->encoding, sizeof(cfg->encoding));
        return true;
    }
    if (strcmp(name, "ignorecase") == 0) {
        return parse_bool(value, &cfg->ignorecase);
    }
    if (strcmp(name, "incsearch") == 0) {
        return parse_bool(value, &cfg->incsearch);
    }
    if (strcmp(name, "hlsearch") == 0) {
        return parse_bool(value, &cfg->hlsearch);
    }
    if (strcmp(name, "mkparents") == 0) {
        return parse_bool(value, &cfg->mkparents);
    }
    if (strcmp(name, "scrollmargin") == 0) {
        char *end = NULL;
        long v = strtol(value, &end, 10);
        if (end != value && *end == '\0' && v >= 0 && v <= 50) {
            cfg->scrollmargin = (int)v;
            return true;
        }
        return false;
    }
    if (strcmp(name, "tabmovement") == 0) {
        return parse_bool(value, &cfg->tabmovement);
    }
    if (strcmp(name, "smartpaste") == 0) {
        return parse_bool(value, &cfg->smartpaste);
    }
    if (strcmp(name, "overwrite") == 0) {
        return parse_bool(value, &cfg->overwrite);
    }
    if (strcmp(name, "reload") == 0) {
        if (strcasecmp(value, "prompt") == 0 ||
            strcasecmp(value, "auto") == 0 ||
            strcasecmp(value, "disabled") == 0) {
            for (size_t i = 0; i < sizeof(cfg->reload) - 1 && value[i]; i++) {
                cfg->reload[i] = (char)tolower((unsigned char)value[i]);
                cfg->reload[i + 1] = '\0';
            }
            return true;
        }
        return false;
    }
    if (strcmp(name, "helpsplit") == 0) {
        if (strcasecmp(value, "hsplit") == 0 ||
            strcasecmp(value, "vsplit") == 0) {
            snprintf(cfg->helpsplit, sizeof(cfg->helpsplit), "%s",
                     strcasecmp(value, "vsplit") == 0 ? "vsplit" : "hsplit");
            return true;
        }
        return false;
    }

    return false;
}

bool config_get(const config_t *cfg, const char *name, char *buf, size_t buflen)
{
    if (cfg == NULL || name == NULL || buf == NULL || buflen == 0) return false;

    if (strcmp(name, "colorscheme") == 0) {
        snprintf(buf, buflen, "%s", cfg->colorscheme);
        return true;
    }
    if (strcmp(name, "softwrap") == 0) {
        snprintf(buf, buflen, "%s", cfg->softwrap ? "true" : "false");
        return true;
    }
    if (strcmp(name, "wordwrap") == 0) {
        snprintf(buf, buflen, "%s", cfg->wordwrap ? "true" : "false");
        return true;
    }
    if (strcmp(name, "tabsize") == 0) {
        snprintf(buf, buflen, "%d", cfg->tabsize);
        return true;
    }
    if (strcmp(name, "tabstospaces") == 0) {
        snprintf(buf, buflen, "%s", cfg->tabstospaces ? "true" : "false");
        return true;
    }
    if (strcmp(name, "autoindent") == 0) {
        snprintf(buf, buflen, "%s", cfg->autoindent ? "true" : "false");
        return true;
    }
    if (strcmp(name, "cursorline") == 0) {
        snprintf(buf, buflen, "%s", cfg->cursorline ? "true" : "false");
        return true;
    }
    if (strcmp(name, "ruler") == 0) {
        snprintf(buf, buflen, "%s", cfg->ruler ? "true" : "false");
        return true;
    }
    if (strcmp(name, "statusline") == 0) {
        snprintf(buf, buflen, "%s", cfg->statusline ? "true" : "false");
        return true;
    }
    if (strcmp(name, "tabbar") == 0) {
        snprintf(buf, buflen, "%s", cfg->tabbar == TABBAR_AUTO ? "auto" :
                                    (cfg->tabbar == TABBAR_ON ? "true" : "false"));
        return true;
    }
    if (strcmp(name, "scrollsync") == 0) {
        snprintf(buf, buflen, "%s", cfg->scrollsync ? "true" : "false");
        return true;
    }
    if (strcmp(name, "splitbottom") == 0) {
        snprintf(buf, buflen, "%s", cfg->splitbottom ? "true" : "false");
        return true;
    }
    if (strcmp(name, "splitright") == 0) {
        snprintf(buf, buflen, "%s", cfg->splitright ? "true" : "false");
        return true;
    }
    if (strcmp(name, "eofnewline") == 0) {
        snprintf(buf, buflen, "%s", cfg->eofnewline ? "true" : "false");
        return true;
    }
    if (strcmp(name, "rmtrailingws") == 0) {
        snprintf(buf, buflen, "%s", cfg->rmtrailingws ? "true" : "false");
        return true;
    }
    if (strcmp(name, "truecolor") == 0) {
        snprintf(buf, buflen, "%s", cfg->truecolor == TRUECOLOR_AUTO ? "auto" :
                                    (cfg->truecolor == TRUECOLOR_ON ? "true" : "false"));
        return true;
    }
    if (strcmp(name, "clipboard") == 0) {
        snprintf(buf, buflen, "%s", cfg->clipboard);
        return true;
    }
    if (strcmp(name, "syntax") == 0) {
        snprintf(buf, buflen, "%s", cfg->syntax ? "true" : "false");
        return true;
    }
    if (strcmp(name, "detectlimit") == 0) {
        snprintf(buf, buflen, "%d", cfg->detectlimit);
        return true;
    }
    if (strcmp(name, "encoding") == 0) {
        snprintf(buf, buflen, "%s", cfg->encoding);
        return true;
    }
    if (strcmp(name, "ignorecase") == 0) {
        snprintf(buf, buflen, "%s", cfg->ignorecase ? "true" : "false");
        return true;
    }
    if (strcmp(name, "incsearch") == 0) {
        snprintf(buf, buflen, "%s", cfg->incsearch ? "true" : "false");
        return true;
    }
    if (strcmp(name, "hlsearch") == 0) {
        snprintf(buf, buflen, "%s", cfg->hlsearch ? "true" : "false");
        return true;
    }
    if (strcmp(name, "mkparents") == 0) {
        snprintf(buf, buflen, "%s", cfg->mkparents ? "true" : "false");
        return true;
    }
    if (strcmp(name, "scrollmargin") == 0) {
        snprintf(buf, buflen, "%d", cfg->scrollmargin);
        return true;
    }
    if (strcmp(name, "tabmovement") == 0) {
        snprintf(buf, buflen, "%s", cfg->tabmovement ? "true" : "false");
        return true;
    }
    if (strcmp(name, "smartpaste") == 0) {
        snprintf(buf, buflen, "%s", cfg->smartpaste ? "true" : "false");
        return true;
    }
    if (strcmp(name, "overwrite") == 0) {
        snprintf(buf, buflen, "%s", cfg->overwrite ? "true" : "false");
        return true;
    }
    if (strcmp(name, "reload") == 0) {
        snprintf(buf, buflen, "%s", cfg->reload);
        return true;
    }
    if (strcmp(name, "helpsplit") == 0) {
        snprintf(buf, buflen, "%s", cfg->helpsplit);
        return true;
    }
    return false;
}

bool config_get_default(const char *name, char *buf, size_t buflen)
{
    config_t def;
    config_init_defaults(&def);
    return config_get(&def, name, buf, buflen);
}

static const char *const k_option_names[] = {
    "autoindent", "clipboard", "colorscheme", "cursorline", "detectlimit",
    "encoding", "eofnewline", "helpsplit", "hlsearch", "ignorecase",
    "incsearch", "mkparents", "overwrite", "reload", "rmtrailingws",
    "ruler", "scrollmargin", "scrollsync", "smartpaste", "softwrap",
    "splitbottom", "splitright", "statusline", "syntax", "tabbar",
    "tabmovement", "tabsize", "tabstospaces", "truecolor", "wordwrap"
};

const char *const *config_option_names(size_t *out_count)
{
    if (out_count) *out_count = sizeof(k_option_names) / sizeof(k_option_names[0]);
    return k_option_names;
}

static char *trim_inplace(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

bool keybind_parse_string(const char *str, tam_key_type_t *type_out,
                          uint32_t *cp_out, unsigned *mods_out)
{
    if (str == NULL || *str == '\0') return false;

    char key_buf[128];
    snprintf(key_buf, sizeof(key_buf), "%s", str);
    const char *p = trim_inplace(key_buf);
    if (*p == '\0') return false;

    unsigned mods = 0;

    /* Parse modifier prefixes: "Ctrl-", "Alt-", "Shift-" or combined "CtrlShift" */
    while (*p) {
        if (strncasecmp(p, "ctrl-", 5) == 0) {
            mods |= TAM_MOD_CTRL;
            p += 5;
        } else if (strncasecmp(p, "alt-", 4) == 0) {
            mods |= TAM_MOD_ALT;
            p += 4;
        } else if (strncasecmp(p, "shift-", 6) == 0) {
            mods |= TAM_MOD_SHIFT;
            p += 6;
        } else if (strncasecmp(p, "ctrl", 4) == 0 && isupper((unsigned char)p[4])) {
            mods |= TAM_MOD_CTRL;
            p += 4;
        } else if (strncasecmp(p, "alt", 3) == 0 && isupper((unsigned char)p[3])) {
            mods |= TAM_MOD_ALT;
            p += 3;
        } else if (strncasecmp(p, "shift", 5) == 0 && isupper((unsigned char)p[5])) {
            mods |= TAM_MOD_SHIFT;
            p += 5;
        } else {
            break;
        }
    }

    tam_key_type_t type = TAM_KEY_CHAR;
    uint32_t cp = 0;

    if (strcasecmp(p, "up") == 0) {
        type = TAM_KEY_UP;
    } else if (strcasecmp(p, "down") == 0) {
        type = TAM_KEY_DOWN;
    } else if (strcasecmp(p, "left") == 0) {
        type = TAM_KEY_LEFT;
    } else if (strcasecmp(p, "right") == 0) {
        type = TAM_KEY_RIGHT;
    } else if (strcasecmp(p, "home") == 0) {
        type = TAM_KEY_HOME;
    } else if (strcasecmp(p, "end") == 0) {
        type = TAM_KEY_END;
    } else if (strcasecmp(p, "pageup") == 0 || strcasecmp(p, "pgup") == 0) {
        type = TAM_KEY_PAGE_UP;
    } else if (strcasecmp(p, "pagedown") == 0 || strcasecmp(p, "pgdn") == 0) {
        type = TAM_KEY_PAGE_DOWN;
    } else if (strcasecmp(p, "enter") == 0 || strcasecmp(p, "return") == 0) {
        type = TAM_KEY_ENTER;
    } else if (strcasecmp(p, "tab") == 0) {
        type = TAM_KEY_TAB;
    } else if (strcasecmp(p, "backtab") == 0) {
        type = TAM_KEY_TAB;
        mods |= TAM_MOD_SHIFT;
    } else if (strcasecmp(p, "backspace") == 0) {
        type = TAM_KEY_BACKSPACE;
    } else if (strcasecmp(p, "insert") == 0 || strcasecmp(p, "ins") == 0) {
        type = TAM_KEY_INSERT;
    } else if (strcasecmp(p, "delete") == 0 || strcasecmp(p, "del") == 0) {
        type = TAM_KEY_DELETE;
    } else if (strcasecmp(p, "escape") == 0 || strcasecmp(p, "esc") == 0) {
        type = TAM_KEY_ESC;
    } else if (strcasecmp(p, "space") == 0) {
        type = TAM_KEY_CHAR;
        cp = ' ';
    } else if (p[0] == 'f' || p[0] == 'F') {
        int fnum = atoi(p + 1);
        if (fnum >= 1 && fnum <= 12) {
            type = (tam_key_type_t)(TAM_KEY_F1 + (fnum - 1));
        } else {
            return false;
        }
    } else if (strlen(p) == 1) {
        type = TAM_KEY_CHAR;
        cp = (unsigned char)tolower((unsigned char)p[0]);
    } else {
        /* Other key names like MouseLeft, etc. can be accepted */
        return false;
    }

    *type_out = type;
    *cp_out   = cp;
    *mods_out = mods;
    return true;
}

void keybind_format_string(tam_key_type_t type, uint32_t cp, unsigned mods,
                           char *buf, size_t buflen)
{
    if (!buf || buflen == 0) return;
    char prefix[64] = "";
    if (mods & TAM_MOD_CTRL)  strncat(prefix, "Ctrl-",  sizeof(prefix) - strlen(prefix) - 1);
    if (mods & TAM_MOD_ALT)   strncat(prefix, "Alt-",   sizeof(prefix) - strlen(prefix) - 1);
    if (mods & TAM_MOD_SHIFT) strncat(prefix, "Shift-", sizeof(prefix) - strlen(prefix) - 1);

    const char *kname = NULL;
    char ch_name[16] = "";
    switch (type) {
    case TAM_KEY_UP:        kname = "Up"; break;
    case TAM_KEY_DOWN:      kname = "Down"; break;
    case TAM_KEY_LEFT:      kname = "Left"; break;
    case TAM_KEY_RIGHT:     kname = "Right"; break;
    case TAM_KEY_HOME:      kname = "Home"; break;
    case TAM_KEY_END:       kname = "End"; break;
    case TAM_KEY_PAGE_UP:   kname = "PageUp"; break;
    case TAM_KEY_PAGE_DOWN: kname = "PageDown"; break;
    case TAM_KEY_ENTER:     kname = "Enter"; break;
    case TAM_KEY_TAB:       kname = "Tab"; break;
    case TAM_KEY_BACKSPACE: kname = "Backspace"; break;
    case TAM_KEY_INSERT:    kname = "Insert"; break;
    case TAM_KEY_DELETE:    kname = "Delete"; break;
    case TAM_KEY_ESC:       kname = "Esc"; break;
    case TAM_KEY_CHAR:
        if (cp == ' ') {
            kname = "Space";
        } else if (cp >= 33 && cp < 127) {
            snprintf(ch_name, sizeof(ch_name), "%c", (char)cp);
            kname = ch_name;
        } else {
            snprintf(ch_name, sizeof(ch_name), "U+%X", cp);
            kname = ch_name;
        }
        break;
    default:
        if (type >= TAM_KEY_F1 && type <= TAM_KEY_F12) {
            snprintf(ch_name, sizeof(ch_name), "F%d", (int)(type - TAM_KEY_F1 + 1));
            kname = ch_name;
        } else {
            kname = "Unknown";
        }
        break;
    }
    snprintf(buf, buflen, "%s%s", prefix, kname);
}

typedef struct {
    config_t          *cfg;
    custom_keybind_t **kb_head;
    FILE              *check_out;
    bool               check_mode;
    bool               has_error;
} ini_load_state_t;

static int ini_entry_handler(void *user, const char *section,
                             const char *name, const char *value)
{
    ini_load_state_t *st = (ini_load_state_t *)user;

    char sec_buf[128];
    char name_buf[128];
    char val_buf[256];
    snprintf(sec_buf, sizeof(sec_buf), "%s", section ? section : "");
    snprintf(name_buf, sizeof(name_buf), "%s", name ? name : "");
    snprintf(val_buf, sizeof(val_buf), "%s", value ? value : "");
    char *sec = trim_inplace(sec_buf);
    char *nm  = trim_inplace(name_buf);
    char *vl  = trim_inplace(val_buf);

    if (sec[0] == '[' && sec[strlen(sec) - 1] == ']') {
        sec[strlen(sec) - 1] = '\0';
        sec++;
        sec = trim_inplace(sec);
    }

    if (*sec == '\0' ||
        strcasecmp(sec, "settings") == 0 ||
        strcasecmp(sec, "options") == 0) {
        bool ok = config_set(st->cfg, nm, vl);
        if (st->check_mode && st->check_out != NULL) {
            if (ok) {
                fprintf(st->check_out, "  [VALID] setting '%s' = '%s'\n", nm, vl);
            } else {
                fprintf(st->check_out, "  [ERROR] invalid setting or value: '%s' = '%s'\n", nm, vl);
                st->has_error = true;
            }
        }
        return 0;
    }

    if (strcasecmp(sec, "bindings") == 0) {
        tam_key_type_t type = TAM_KEY_NONE;
        uint32_t cp = 0;
        unsigned mods = 0;
        bool ok = keybind_parse_string(nm, &type, &cp, &mods);
        if (ok) {
            if (st->kb_head != NULL) {
                custom_keybind_t *kb = malloc(sizeof(*kb));
                if (kb != NULL) {
                    kb->type      = type;
                    kb->codepoint = cp;
                    kb->mods      = mods;
                    kb->action    = strdup(vl);
                    kb->next      = *st->kb_head;
                    *st->kb_head  = kb;
                }
            }
            if (st->check_mode && st->check_out != NULL) {
                fprintf(st->check_out, "  [VALID] binding '%s' -> '%s'\n", nm, vl);
            }
        } else {
            if (st->check_mode && st->check_out != NULL) {
                fprintf(st->check_out, "  [ERROR] invalid key specifier: '%s'\n", nm);
                st->has_error = true;
            }
        }
        return 0;
    }

    if (st->check_mode && st->check_out != NULL) {
        fprintf(st->check_out, "  [WARN] unknown section '[%s]'\n", sec);
    }
    return 0;
}

static char *get_config_dir(bool create)
{
    const char *override = getenv("TAM_EDIT_CONFIG_HOME");
    char base[512];
    if (override != NULL && *override != '\0') {
        snprintf(base, sizeof(base), "%s", override);
        if (create) tam_fs_mkdir_p(base, 0755);
        return strdup(base);
    }

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && *xdg != '\0') {
        snprintf(base, sizeof(base), "%s/tam-edit", xdg);
    } else {
        const char *home = getenv("HOME");
        if (home == NULL) home = "";
        snprintf(base, sizeof(base), "%s/.config/tam-edit", home);
    }

    struct stat st;
    if (stat(base, &st) == 0 && S_ISDIR(st.st_mode)) {
        return strdup(base);
    }

    /* Check tamlinux/tam-edit path from libtam */
    char *tam_dir = tam_xdg_path(TAM_XDG_CONFIG, "tam-edit", NULL, false);
    if (tam_dir != NULL) {
        char ini_check[512];
        snprintf(ini_check, sizeof(ini_check), "%s/settings.ini", tam_dir);
        if (access(ini_check, F_OK) == 0) {
            return tam_dir;
        }
        free(tam_dir);
    }

    if (create) {
        tam_fs_mkdir_p(base, 0755);
    }
    return strdup(base);
}

bool config_load_file(const char *path, config_t *cfg, custom_keybind_t **kb_head)
{
    if (path == NULL || cfg == NULL) return false;
    ini_load_state_t st;
    st.cfg        = cfg;
    st.kb_head    = kb_head;
    st.check_out  = NULL;
    st.check_mode = false;
    st.has_error  = false;

    int rc = tam_ini_parse_file(path, ini_entry_handler, &st);
    return rc >= 0;
}

bool config_load(config_t *cfg, custom_keybind_t **kb_head)
{
    char *cfg_dir = get_config_dir(false);
    if (cfg_dir == NULL) return false;

    char ini_path[512];
    snprintf(ini_path, sizeof(ini_path), "%s/settings.ini", cfg_dir);
    free(cfg_dir);

    return config_load_file(ini_path, cfg, kb_head);
}

bool config_check(const char *path, FILE *out)
{
    char ini_path[512] = {0};
    if (path != NULL && *path != '\0') {
        strncpy(ini_path, path, sizeof(ini_path) - 1);
    } else {
        char *cfg_dir = get_config_dir(false);
        if (cfg_dir == NULL) return false;
        snprintf(ini_path, sizeof(ini_path), "%s/settings.ini", cfg_dir);
        free(cfg_dir);
    }

    if (out == NULL) out = stdout;
    fprintf(out, "Checking configuration '%s'...\n", ini_path);

    FILE *f = fopen(ini_path, "r");
    if (f == NULL) {
        fprintf(out, "Configuration file not found (default settings will be used).\n");
        return true;
    }
    fclose(f);

    config_t cfg;
    config_init_defaults(&cfg);

    ini_load_state_t st;
    st.cfg        = &cfg;
    st.kb_head    = NULL;
    st.check_out  = out;
    st.check_mode = true;
    st.has_error  = false;

    int rc = tam_ini_parse_file(ini_path, ini_entry_handler, &st);
    if (rc < 0 || st.has_error) {
        fprintf(out, "Configuration check failed with errors.\n");
        return false;
    }

    fprintf(out, "Configuration check passed successfully.\n");
    return true;
}

/* -------------------------------------------------------------------------- */
/* Lightweight JSON5 Parser for Micro Config Import                           */
/* -------------------------------------------------------------------------- */

typedef struct {
    char key[128];
    char val[256];
} kv_pair_t;

static size_t parse_json5_pairs(const char *text, kv_pair_t *pairs, size_t max_pairs)
{
    size_t count = 0;
    const char *p = text;

    while (*p && count < max_pairs) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;

        /* Skip comments */
        if (*p == '/' && *(p + 1) == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (*p == '/' && *(p + 1) == '*') {
            p += 2;
            while (*p && !(*p == '*' && *(p + 1) == '/')) p++;
            if (*p) p += 2;
            continue;
        }

        if (*p == '{' || *p == '}' || *p == ',') {
            p++;
            continue;
        }

        if (!*p) break;

        /* Parse key: unquoted or quoted */
        char key_buf[128] = {0};
        size_t klen = 0;
        char quote = 0;
        if (*p == '"' || *p == '\'') {
            quote = *p++;
            while (*p && *p != quote && klen < sizeof(key_buf) - 1) {
                key_buf[klen++] = *p++;
            }
            if (*p == quote) p++;
        } else {
            while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-') &&
                   klen < sizeof(key_buf) - 1) {
                key_buf[klen++] = *p++;
            }
        }
        key_buf[klen] = '\0';

        /* Expect colon */
        while (*p && (isspace((unsigned char)*p) || *p == ':')) {
            if (*p == ':') {
                p++;
                break;
            }
            p++;
        }

        while (*p && isspace((unsigned char)*p)) p++;

        /* Parse value */
        char val_buf[256] = {0};
        size_t vlen = 0;
        if (*p == '"' || *p == '\'') {
            quote = *p++;
            while (*p && *p != quote && vlen < sizeof(val_buf) - 1) {
                if (*p == '\\' && *(p + 1)) p++;
                val_buf[vlen++] = *p++;
            }
            if (*p == quote) p++;
        } else {
            while (*p && *p != ',' && *p != '}' && *p != '\n' && *p != '\r' &&
                   vlen < sizeof(val_buf) - 1) {
                val_buf[vlen++] = *p++;
            }
            /* Trim trailing whitespace */
            while (vlen > 0 && isspace((unsigned char)val_buf[vlen - 1])) {
                vlen--;
            }
        }
        val_buf[vlen] = '\0';

        if (klen > 0 && vlen > 0) {
            snprintf(pairs[count].key, sizeof(pairs[count].key), "%s", key_buf);
            snprintf(pairs[count].val, sizeof(pairs[count].val), "%s", val_buf);
            count++;
        }
    }

    return count;
}

bool config_import_micro(const char *micro_dir, const char *out_ini_path, FILE *out)
{
    if (out == NULL) out = stdout;

    char in_dir[512] = {0};
    if (micro_dir != NULL && *micro_dir != '\0') {
        strncpy(in_dir, micro_dir, sizeof(in_dir) - 1);
    } else {
        const char *home = getenv("HOME");
        if (home == NULL) home = "";
        snprintf(in_dir, sizeof(in_dir), "%s/.config/micro", home);
    }

    char target_path[512] = {0};
    if (out_ini_path != NULL && *out_ini_path != '\0') {
        strncpy(target_path, out_ini_path, sizeof(target_path) - 1);
    } else {
        char *cfg_dir = get_config_dir(true);
        if (cfg_dir == NULL) {
            fprintf(out, "Failed to determine tam-edit config directory.\n");
            return false;
        }
        snprintf(target_path, sizeof(target_path), "%s/settings.ini", cfg_dir);
        free(cfg_dir);
    }

    fprintf(out, "Importing micro configuration from '%s'...\n", in_dir);

    tam_str_t ini_out = tam_str_new("# tam-edit configuration generated from micro\n\n[settings]\n");
    if (ini_out == NULL) return false;

    /* 1. Import settings.json */
    char settings_json[512];
    snprintf(settings_json, sizeof(settings_json), "%s/settings.json", in_dir);
    size_t s_len = 0;
    char *s_data = tam_fs_read_file(settings_json, &s_len);
    if (s_data != NULL) {
        kv_pair_t pairs[64];
        size_t n = parse_json5_pairs(s_data, pairs, 64);
        config_t test_cfg;
        config_init_defaults(&test_cfg);

        for (size_t i = 0; i < n; i++) {
            if (config_set(&test_cfg, pairs[i].key, pairs[i].val)) {
                ini_out = tam_str_cat_printf(ini_out, "%s = %s\n", pairs[i].key, pairs[i].val);
                fprintf(out, "  [SETTING] %s = %s\n", pairs[i].key, pairs[i].val);
            } else {
                fprintf(out, "  [IGNORED] unsupported micro setting: %s\n", pairs[i].key);
            }
        }
        free(s_data);
    } else {
        fprintf(out, "  Notice: %s not found.\n", settings_json);
    }

    /* 2. Import bindings.json */
    ini_out = tam_str_cat(ini_out, "\n[bindings]\n");
    char bindings_json[512];
    snprintf(bindings_json, sizeof(bindings_json), "%s/bindings.json", in_dir);
    size_t b_len = 0;
    char *b_data = tam_fs_read_file(bindings_json, &b_len);
    if (b_data != NULL) {
        kv_pair_t pairs[128];
        size_t n = parse_json5_pairs(b_data, pairs, 128);

        for (size_t i = 0; i < n; i++) {
            const char *action = pairs[i].val;
            /* Strip command: prefix if present */
            if (strncmp(action, "command:", 8) == 0) {
                action += 8;
            }
            ini_out = tam_str_cat_printf(ini_out, "%s = %s\n", pairs[i].key, action);
            fprintf(out, "  [BINDING] %s = %s\n", pairs[i].key, action);
        }
        free(b_data);
    } else {
        fprintf(out, "  Notice: %s not found.\n", bindings_json);
    }

    /* 3. If target exists, backup to target_path.bak */
    char backup_path[540];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", target_path);
    size_t existing_len = 0;
    char *existing = tam_fs_read_file(target_path, &existing_len);
    if (existing != NULL) {
        tam_fs_write_atomic(backup_path, existing, existing_len, 0644);
        fprintf(out, "Existing configuration backed up to '%s'.\n", backup_path);
        free(existing);
    }

    /* 4. Write new settings.ini */
    bool ok = tam_fs_write_atomic(target_path, ini_out, tam_str_len(ini_out), 0644);
    if (ok) {
        fprintf(out, "Successfully wrote '%s'.\n", target_path);
    } else {
        fprintf(out, "Failed to write '%s': %s\n", target_path, strerror(errno));
    }

    tam_str_free(ini_out);
    return ok;
}

static bool ini_update_entry(const char *section, const char *key, const char *value)
{
    char *cfg_dir = get_config_dir(true);
    if (!cfg_dir) return false;

    char ini_path[512];
    snprintf(ini_path, sizeof(ini_path), "%s/settings.ini", cfg_dir);
    free(cfg_dir);

    size_t len = 0;
    char *existing = tam_fs_read_file(ini_path, &len);
    tam_str_t out = tam_str_new("");
    if (!out) {
        free(existing);
        return false;
    }

    bool in_target_sec = false;
    bool seen_target_sec = false;
    bool wrote_entry = false;

    if (existing) {
        char *p = existing;
        while (*p) {
            char *nl = strchr(p, '\n');
            size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
            char *line = malloc(line_len + 1);
            if (!line) break;
            memcpy(line, p, line_len);
            line[line_len] = '\0';
            if (line_len > 0 && line[line_len - 1] == '\r') {
                line[--line_len] = '\0';
            }

            char copy[512];
            snprintf(copy, sizeof(copy), "%s", line);
            char *t = trim_inplace(copy);

            if (t[0] == '[') {
                if (in_target_sec && !wrote_entry && value != NULL) {
                    out = tam_str_cat_printf(out, "%s = %s\n", key, value);
                    wrote_entry = true;
                }
                char *rb = strchr(t, ']');
                if (rb) *rb = '\0';
                char *sec_name = trim_inplace(t + 1);
                in_target_sec = (strcasecmp(sec_name, section) == 0);
                if (in_target_sec) seen_target_sec = true;
                out = tam_str_cat_printf(out, "%s\n", line);
            } else if (in_target_sec && t[0] != '#' && t[0] != ';' && t[0] != '\0') {
                char *eq = strchr(t, '=');
                if (eq) {
                    *eq = '\0';
                    char *k = trim_inplace(t);
                    bool match = (strcasecmp(section, "bindings") == 0)
                                 ? (strcasecmp(k, key) == 0)
                                 : (strcmp(k, key) == 0);
                    if (match) {
                        if (value != NULL) {
                            if (!wrote_entry) {
                                out = tam_str_cat_printf(out, "%s = %s\n", key, value);
                                wrote_entry = true;
                            }
                        } else {
                            wrote_entry = true; /* omitted */
                        }
                        free(line);
                        p = nl ? (nl + 1) : (p + line_len);
                        continue;
                    }
                }
                out = tam_str_cat_printf(out, "%s\n", line);
            } else {
                out = tam_str_cat_printf(out, "%s\n", line);
            }

            free(line);
            p = nl ? (nl + 1) : (p + line_len);
        }
        free(existing);
    }

    if (value != NULL && !wrote_entry) {
        if (!seen_target_sec) {
            if (tam_str_len(out) > 0 && out[tam_str_len(out) - 1] != '\n') {
                out = tam_str_cat(out, "\n");
            }
            if (tam_str_len(out) > 0) {
                out = tam_str_cat(out, "\n");
            }
            out = tam_str_cat_printf(out, "[%s]\n", section);
        }
        out = tam_str_cat_printf(out, "%s = %s\n", key, value);
    }

    bool ok = tam_fs_write_atomic(ini_path, out, tam_str_len(out), 0644);
    tam_str_free(out);
    return ok;
}

bool config_save_setting(const char *name, const char *value)
{
    if (!name || !name[0] || !value) return false;
    return ini_update_entry("settings", name, value);
}

bool config_save_binding(const char *key_str, const char *action_str)
{
    if (!key_str || !key_str[0] || !action_str) return false;
    return ini_update_entry("bindings", key_str, action_str);
}

bool config_remove_binding(const char *key_str)
{
    if (!key_str || !key_str[0]) return false;
    return ini_update_entry("bindings", key_str, NULL);
}
