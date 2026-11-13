#include "colorscheme.h"
#include "colorschemes_data.h"

#include <tam.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *name;
    uint8_t     ansi;
} g_named_colors[] = {
    {"black", 0},
    {"red", 1},
    {"green", 2},
    {"yellow", 3},
    {"blue", 4},
    {"magenta", 5},
    {"cyan", 6},
    {"white", 7},
    {"brightblack", 8},
    {"darkgray", 8},
    {"grey", 8},
    {"gray", 8},
    {"brightred", 9},
    {"brightgreen", 10},
    {"brightyellow", 11},
    {"brightblue", 12},
    {"brightmagenta", 13},
    {"brightcyan", 14},
    {"brightwhite", 15},
    {NULL, 0}
};

static uint32_t parse_color_token(const char *tok)
{
    while (isspace((unsigned char)*tok)) tok++;
    if (*tok == '\0' || strcmp(tok, "default") == 0) {
        return COLOR_DEFAULT;
    }

    if (*tok == '#') {
        tok++;
        size_t len = strlen(tok);
        if (len == 6) {
            unsigned int r = 0, g = 0, b = 0;
            if (sscanf(tok, "%02x%02x%02x", &r, &g, &b) == 3) {
                return COLOR_RGB(r, g, b);
            }
        } else if (len == 3) {
            unsigned int r = 0, g = 0, b = 0;
            if (sscanf(tok, "%1x%1x%1x", &r, &g, &b) == 3) {
                return COLOR_RGB(r * 17, g * 17, b * 17);
            }
        }
        return COLOR_DEFAULT;
    }

    /* Check integer 0..255 */
    char *end = NULL;
    long val = strtol(tok, &end, 10);
    if (end != tok && *end == '\0' && val >= 0 && val <= 255) {
        return COLOR_ANSI((uint8_t)val);
    }

    /* Check named colors */
    for (size_t i = 0; g_named_colors[i].name != NULL; i++) {
        if (strcasecmp(tok, g_named_colors[i].name) == 0) {
            return COLOR_ANSI(g_named_colors[i].ansi);
        }
    }

    return COLOR_DEFAULT;
}

style_t style_parse(const char *str)
{
    style_t st;
    st.fg = COLOR_DEFAULT;
    st.bg = COLOR_DEFAULT;
    st.attr = ATTR_NONE;

    if (str == NULL || *str == '\0') return st;

    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Parse space-separated attributes and colors */
    char *saveptr = NULL;
    char *tok = strtok_r(buf, " \t", &saveptr);
    char *color_part = NULL;

    while (tok != NULL) {
        if (strcasecmp(tok, "bold") == 0) {
            st.attr |= ATTR_BOLD;
        } else if (strcasecmp(tok, "reverse") == 0) {
            st.attr |= ATTR_REVERSE;
        } else if (strcasecmp(tok, "underline") == 0) {
            st.attr |= ATTR_UNDERLINE;
        } else if (strcasecmp(tok, "italic") == 0) {
            st.attr |= ATTR_ITALIC;
        } else {
            color_part = tok;
        }
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    if (color_part != NULL) {
        char *comma = strchr(color_part, ',');
        if (comma != NULL) {
            *comma = '\0';
            char *fg_str = color_part;
            char *bg_str = comma + 1;
            st.fg = parse_color_token(fg_str);
            st.bg = parse_color_token(bg_str);
        } else {
            st.fg = parse_color_token(color_part);
        }
    }

    return st;
}

static void colorscheme_add_link(colorscheme_t *cs, const char *group, style_t st)
{
    cs->cache_valid = false;
    /* Check if group already exists */
    for (color_link_t *l = cs->links; l != NULL; l = l->next) {
        if (strcmp(l->group, group) == 0) {
            l->style = st;
            if (strcmp(group, "default") == 0) cs->default_style = st;
            return;
        }
    }

    color_link_t *l = malloc(sizeof(*l));
    if (l == NULL) return;
    l->group = strdup(group);
    l->style = st;
    l->next = cs->links;
    cs->links = l;

    if (strcmp(group, "default") == 0) {
        cs->default_style = st;
    }
}

static const char *g_embedded_monokai =
    "color-link default \"#F8F8F2,#282828\"\n"
    "color-link comment \"#75715E,#282828\"\n"
    "color-link identifier \"#66D9EF,#282828\"\n"
    "color-link constant \"#AE81FF,#282828\"\n"
    "color-link constant.string \"#E6DB74,#282828\"\n"
    "color-link statement \"#F92672,#282828\"\n"
    "color-link symbol.operator \"#F92672,#282828\"\n"
    "color-link preproc \"#CB4B16,#282828\"\n"
    "color-link type \"#66D9EF,#282828\"\n"
    "color-link special \"#A6E22E,#282828\"\n"
    "color-link underlined \"#D33682,#282828\"\n"
    "color-link error \"bold #CB4B16,#282828\"\n"
    "color-link todo \"bold #D33682,#282828\"\n"
    "color-link hlsearch \"#282828,#E6DB74\"\n"
    "color-link statusline \"#282828,#F8F8F2\"\n"
    "color-link tabbar \"#282828,#F8F8F2\"\n"
    "color-link indent-char \"#505050,#282828\"\n"
    "color-link line-number \"#AAAAAA,#323232\"\n"
    "color-link current-line-number \"#AAAAAA,#282828\"\n"
    "color-link cursor-line \"#323232\"\n"
    "color-link color-column \"#323232\"\n"
    "color-link match-brace \"#282828,#AE81FF\"\n"
    "color-link tab-error \"#D75F5F\"\n"
    "color-link trailingws \"#D75F5F\"\n"
    "color-link divider \"#505050,#282828\"\n";

void colorscheme_init(colorscheme_t *cs)
{
    memset(cs, 0, sizeof(*cs));
    strncpy(cs->name, "default", sizeof(cs->name) - 1);
    cs->default_style.fg = COLOR_DEFAULT;
    cs->default_style.bg = COLOR_DEFAULT;
    cs->default_style.attr = ATTR_NONE;

    /* Populate embedded monokai */
    char *copy = strdup(g_embedded_monokai);
    if (copy != NULL) {
        char *line = strtok(copy, "\n");
        while (line != NULL) {
            char group[64] = {0};
            char val[128] = {0};
            if (sscanf(line, "color-link %63s \"%127[^\"]\"", group, val) == 2) {
                colorscheme_add_link(cs, group, style_parse(val));
            }
            line = strtok(NULL, "\n");
        }
        free(copy);
    }
}

void colorscheme_free(colorscheme_t *cs)
{
    color_link_t *cur = cs->links;
    while (cur != NULL) {
        color_link_t *next = cur->next;
        free(cur->group);
        free(cur);
        cur = next;
    }
    cs->links = NULL;
    cs->cache_valid = false;
}

static const char *find_embedded_colorscheme(const char *name)
{
    if (name == NULL) return NULL;
    for (size_t i = 0; i < g_embedded_colorschemes_count; i++) {
        if (strcmp(g_embedded_colorschemes[i].name, name) == 0) {
            return g_embedded_colorschemes[i].data;
        }
    }
    return NULL;
}

static bool parse_scheme_buffer(colorscheme_t *cs, const char *data,
                                const char *parent_dir,
                                const char **seen, size_t seen_count)
{
    if (data == NULL) return false;
    char *copy = strdup(data);
    if (copy == NULL) return false;

    char *saveptr = NULL;
    char *line = strtok_r(copy, "\r\n", &saveptr);
    while (line != NULL) {
        while (isspace((unsigned char)*line)) line++;
        if (*line != '#' && *line != '\0') {
            char inc_name[128] = {0};
            char group[64] = {0};
            char val[128] = {0};

            if (sscanf(line, "include \"%127[^\"]\"", inc_name) == 1) {
                /* Check circular include */
                bool circular = false;
                for (size_t s = 0; s < seen_count; s++) {
                    if (strcmp(seen[s], inc_name) == 0) {
                        circular = true;
                        break;
                    }
                }
                if (!circular && seen_count < 16) {
                    const char *new_seen[16];
                    for (size_t s = 0; s < seen_count; s++) new_seen[s] = seen[s];
                    new_seen[seen_count] = inc_name;

                    /* Search file in parent dir, runtime, or embedded table */
                    char try_path[512];
                    char *inc_data = NULL;
                    size_t inc_len = 0;
                    if (parent_dir != NULL) {
                        snprintf(try_path, sizeof(try_path), "%s/%s.micro", parent_dir, inc_name);
                        inc_data = tam_fs_read_file(try_path, &inc_len);
                    }
                    if (inc_data == NULL) {
                        snprintf(try_path, sizeof(try_path), "runtime/colorschemes/%s.micro", inc_name);
                        inc_data = tam_fs_read_file(try_path, &inc_len);
                    }
                    if (inc_data != NULL) {
                        parse_scheme_buffer(cs, inc_data, parent_dir, new_seen, seen_count + 1);
                        free(inc_data);
                    } else {
                        const char *emb = find_embedded_colorscheme(inc_name);
                        if (emb != NULL) {
                            parse_scheme_buffer(cs, emb, parent_dir, new_seen, seen_count + 1);
                        }
                    }
                }
            } else if (sscanf(line, "color-link %63s \"%127[^\"]\"", group, val) == 2) {
                colorscheme_add_link(cs, group, style_parse(val));
            }
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }

    free(copy);
    return true;
}

bool colorscheme_load_file(colorscheme_t *cs, const char *path)
{
    if (path == NULL) return false;
    size_t len = 0;
    char *data = tam_fs_read_file(path, &len);
    if (data == NULL) return false;

    char dir[512] = {0};
    const char *slash = strrchr(path, '/');
    if (slash != NULL) {
        size_t dlen = slash - path;
        if (dlen < sizeof(dir)) {
            memcpy(dir, path, dlen);
            dir[dlen] = '\0';
        }
    }

    const char *seen[16];
    seen[0] = path;
    bool ok = parse_scheme_buffer(cs, data, dir[0] ? dir : NULL, seen, 1);
    free(data);
    return ok;
}

static bool colorscheme_try_load_into(colorscheme_t *cs, const char *name)
{
    const char *home = getenv("HOME");

    /* 1. Try ~/.config/tam-edit/colorschemes */
    if (home != NULL && *home != '\0') {
        char full[512];
        snprintf(full, sizeof(full), "%s/.config/tam-edit/colorschemes/%s.micro", home, name);
        if (colorscheme_load_file(cs, full)) return true;
    }

    /* 2. Try tamlinux XDG path */
    char *user_path = tam_xdg_path(TAM_XDG_CONFIG, "tam-edit", NULL, false);
    if (user_path != NULL) {
        char full[512];
        snprintf(full, sizeof(full), "%s/colorschemes/%s.micro", user_path, name);
        free(user_path);
        if (colorscheme_load_file(cs, full)) return true;
    }

    /* 3. Try micro's config directory */
    if (home != NULL && *home != '\0') {
        char full[512];
        snprintf(full, sizeof(full), "%s/.config/micro/colorschemes/%s.micro", home, name);
        if (colorscheme_load_file(cs, full)) return true;
    }

    /* 4. Try embedded 25 colorschemes */
    const char *emb = find_embedded_colorscheme(name);
    if (emb != NULL) {
        const char *seen[16];
        seen[0] = name;
        if (parse_scheme_buffer(cs, emb, NULL, seen, 1)) return true;
    }

    /* 5. Try runtime/colorschemes on disk */
    char rt_path[512];
    snprintf(rt_path, sizeof(rt_path), "runtime/colorschemes/%s.micro", name);
    if (colorscheme_load_file(cs, rt_path)) return true;

    return false;
}

bool colorscheme_load(colorscheme_t *cs, const char *name)
{
    if (name == NULL || *name == '\0') name = "default";

    colorscheme_t fresh;
    colorscheme_init(&fresh);
    strncpy(fresh.name, name, sizeof(fresh.name) - 1);

    if (colorscheme_try_load_into(&fresh, name)) {
        colorscheme_free(cs);
        *cs = fresh;
        return true;
    }

    colorscheme_free(&fresh);
    if (strcmp(name, "default") == 0 || strcmp(name, "monokai") == 0) {
        strncpy(cs->name, name, sizeof(cs->name) - 1);
        return true;
    }
    return false;
}

style_t colorscheme_get(const colorscheme_t *cs, const char *group)
{
    if (group == NULL || *group == '\0') return cs->default_style;

    /* 1. Exact match */
    for (color_link_t *l = cs->links; l != NULL; l = l->next) {
        if (strcmp(l->group, group) == 0) return l->style;
    }

    /* 2. Hierarchical fallback (e.g. constant.string -> constant) */
    char buf[128];
    strncpy(buf, group, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *dot = strrchr(buf, '.');
    while (dot != NULL) {
        *dot = '\0';
        for (color_link_t *l = cs->links; l != NULL; l = l->next) {
            if (strcmp(l->group, buf) == 0) return l->style;
        }
        dot = strrchr(buf, '.');
    }

    return cs->default_style;
}
