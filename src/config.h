#ifndef TAM_EDIT_CONFIG_H
#define TAM_EDIT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <tam_key.h>

#define TABBAR_OFF  0
#define TABBAR_ON   1
#define TABBAR_AUTO 2

#define TRUECOLOR_OFF  0
#define TRUECOLOR_ON   1
#define TRUECOLOR_AUTO 2

typedef struct {
    char colorscheme[64];
    bool softwrap;
    bool wordwrap;
    int  tabsize;
    bool tabstospaces;
    bool autoindent;
    bool cursorline;
    bool ruler;
    bool statusline;
    int  tabbar;
    bool scrollsync;
    bool splitbottom;
    bool splitright;
    bool eofnewline;
    bool rmtrailingws;
    int  truecolor;
    char clipboard[32];
    bool syntax;
    int  detectlimit;
} config_t;

typedef struct custom_keybind {
    tam_key_type_t         type;
    uint32_t               codepoint;
    unsigned               mods;
    char                  *action;
    struct custom_keybind *next;
} custom_keybind_t;

void config_init_defaults(config_t *cfg);
void keybinds_free(custom_keybind_t *kb);

bool config_load(config_t *cfg, custom_keybind_t **kb_head);
bool config_load_file(const char *path, config_t *cfg, custom_keybind_t **kb_head);
bool config_set(config_t *cfg, const char *name, const char *value);
bool config_get(const config_t *cfg, const char *name, char *buf, size_t buflen);

/* Key parsing helper: parses "Alt-w", "Ctrl-s", "CtrlPageUp", "MouseLeftRelease", etc. */
bool keybind_parse_string(const char *str, tam_key_type_t *type_out,
                          uint32_t *cp_out, unsigned *mods_out);

/* --check-config implementation */
bool config_check(const char *path, FILE *out);

/* --import-micro-config implementation */
bool config_import_micro(const char *micro_dir, const char *out_ini_path, FILE *out);

#endif
