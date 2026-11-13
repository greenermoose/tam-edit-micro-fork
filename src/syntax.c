#include "syntax.h"
#include "buffer.h"
#include "syntax_data.h"

#include <tam.h>
#include <tam_uni.h>

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Internal Uncompiled Rule AST (Shared by Embedded Specs and YAML Overrides) */
/* ========================================================================= */

typedef struct {
    uint16_t group_id;
    char    *pattern;
} raw_pat_t;

typedef struct raw_reg {
    uint16_t         group_id;
    uint16_t         limit_group_id;
    char            *start_pat;
    char            *end_pat;
    char            *skip_pat;
    raw_pat_t       *pats;
    size_t           pat_count;
    size_t           pat_cap;
    struct raw_reg **regs;
    size_t           reg_count;
    size_t           reg_cap;
    char           **includes;
    size_t           inc_count;
    size_t           inc_cap;
} raw_reg_t;

typedef struct {
    char         *filetype;
    char         *filename_pat;
    char         *header_pat;
    char         *signature_pat;
    raw_pat_t    *pats;
    size_t        pat_count;
    size_t        pat_cap;
    raw_reg_t   **regs;
    size_t        reg_count;
    size_t        reg_cap;
    char        **includes;
    size_t        inc_count;
    size_t        inc_cap;
    bool          detect_compiled;
    syntax_def_t  compiled;
} syn_entry_t;

typedef struct {
    bool         initialized;
    char       **groups;
    size_t       group_count;
    size_t       group_cap;
    syn_entry_t *defs;
    size_t       def_count;
    size_t       def_cap;
} syntax_registry_t;

static syntax_registry_t g_reg = {0};

/* ========================================================================= */
/* Group Registry & Style Cache                                              */
/* ========================================================================= */

uint16_t syntax_intern_group(const char *name)
{
    if (name == NULL || *name == '\0') return 0;
    if (!g_reg.initialized) syntax_registry_init();

    for (size_t i = 0; i < g_reg.group_count; i++) {
        if (strcmp(g_reg.groups[i], name) == 0) return (uint16_t)i;
    }

    if (g_reg.group_count >= SYNTAX_MAX_GROUPS) return 0;
    if (g_reg.group_count == g_reg.group_cap) {
        size_t ncap = g_reg.group_cap ? g_reg.group_cap * 2 : 128;
        if (ncap > SYNTAX_MAX_GROUPS) ncap = SYNTAX_MAX_GROUPS;
        char **ng = realloc(g_reg.groups, ncap * sizeof(*ng));
        if (ng == NULL) return 0;
        g_reg.groups = ng;
        g_reg.group_cap = ncap;
    }

    char *dup = strdup(name);
    if (dup == NULL) return 0;
    uint16_t id = (uint16_t)g_reg.group_count++;
    g_reg.groups[id] = dup;
    return id;
}

const char *syntax_group_name(uint16_t group_id)
{
    if (group_id < g_reg.group_count) return g_reg.groups[group_id];
    return "default";
}

size_t syntax_group_count(void)
{
    return g_reg.group_count;
}

void syntax_build_style_cache(const colorscheme_t *cs, style_t cache[SYNTAX_MAX_GROUPS])
{
    if (!g_reg.initialized) syntax_registry_init();
    style_t def = cs ? cs->default_style : (style_t){COLOR_DEFAULT, COLOR_DEFAULT, ATTR_NONE};
    for (size_t i = 0; i < SYNTAX_MAX_GROUPS; i++) {
        cache[i] = def;
    }
    if (cs == NULL) return;
    for (size_t i = 0; i < g_reg.group_count && i < SYNTAX_MAX_GROUPS; i++) {
        cache[i] = colorscheme_get(cs, g_reg.groups[i]);
    }
}

/* ========================================================================= */
/* Raw AST Cleanup & Compiled AST Cleanup                                    */
/* ========================================================================= */

static void free_raw_reg(raw_reg_t *r)
{
    if (r == NULL) return;
    free(r->start_pat);
    free(r->end_pat);
    free(r->skip_pat);
    for (size_t i = 0; i < r->pat_count; i++) free(r->pats[i].pattern);
    free(r->pats);
    for (size_t i = 0; i < r->reg_count; i++) free_raw_reg(r->regs[i]);
    free(r->regs);
    for (size_t i = 0; i < r->inc_count; i++) free(r->includes[i]);
    free(r->includes);
    free(r);
}

static void free_compiled_region(syntax_region_t *r)
{
    if (r == NULL) return;
    tam_regex_free(r->start);
    tam_regex_free(r->end);
    tam_regex_free(r->skip);
    for (size_t i = 0; i < r->pattern_count; i++) {
        tam_regex_free(r->patterns[i].regex);
    }
    free(r->patterns);
    for (size_t i = 0; i < r->region_count; i++) {
        free_compiled_region(r->regions[i]);
    }
    free(r->regions);
    free(r);
}

static void free_compiled_def(syntax_def_t *d)
{
    if (d == NULL) return;
    free(d->filetype);
    tam_regex_free(d->filename_re);
    tam_regex_free(d->header_re);
    tam_regex_free(d->signature_re);
    for (size_t i = 0; i < d->pattern_count; i++) {
        tam_regex_free(d->patterns[i].regex);
    }
    free(d->patterns);
    for (size_t i = 0; i < d->region_count; i++) {
        free_compiled_region(d->regions[i]);
    }
    free(d->regions);
    memset(d, 0, sizeof(*d));
}

static void free_syn_entry(syn_entry_t *e)
{
    if (e == NULL) return;
    free(e->filetype);
    free(e->filename_pat);
    free(e->header_pat);
    free(e->signature_pat);
    for (size_t i = 0; i < e->pat_count; i++) free(e->pats[i].pattern);
    free(e->pats);
    for (size_t i = 0; i < e->reg_count; i++) free_raw_reg(e->regs[i]);
    free(e->regs);
    for (size_t i = 0; i < e->inc_count; i++) free(e->includes[i]);
    free(e->includes);
    free_compiled_def(&e->compiled);
    memset(e, 0, sizeof(*e));
}

/* ========================================================================= */
/* Flat YAML Parser for Micro Syntax Overrides (~/.config/tam-edit/syntax)   */
/* ========================================================================= */

static void yaml_err(char *errbuf, size_t errlen, const char *fmt, ...)
{
    if (errbuf == NULL || errlen == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, errlen, fmt, ap);
    va_end(ap);
}

static size_t append_utf8(char *dst, size_t cap, size_t pos, uint32_t cp)
{
    char tmp[4];
    size_t n = tam_utf8_encode(cp, tmp);
    for (size_t i = 0; i < n && pos < cap; i++) {
        dst[pos++] = tmp[i];
    }
    return pos;
}

static char *parse_yaml_scalar(const char *s, size_t len, bool *ok)
{
    *ok = true;
    while (len > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        len--;
    }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) {
        len--;
    }
    if (len == 0) return strdup("");

    if (s[0] == '"') {
        char *out = malloc(len * 4 + 1);
        if (out == NULL) {
            *ok = false;
            return NULL;
        }
        size_t o = 0;
        size_t i = 1;
        bool closed = false;
        while (i < len) {
            if (s[i] == '"') {
                closed = true;
                i++;
                break;
            }
            if (s[i] == '\\' && i + 1 < len) {
                i++;
                char e = s[i++];
                switch (e) {
                case '\\': out[o++] = '\\'; break;
                case '"':  out[o++] = '"';  break;
                case '/':  out[o++] = '/';  break;
                case 'n':  out[o++] = '\n'; break;
                case 'r':  out[o++] = '\r'; break;
                case 't':  out[o++] = '\t'; break;
                case 'b':  out[o++] = '\b'; break;
                case 'f':  out[o++] = '\f'; break;
                case 'a':  out[o++] = '\a'; break;
                case 'v':  out[o++] = '\v'; break;
                case '0':  out[o++] = '\0'; break;
                case 'x':
                case 'u':
                case 'U': {
                    int digits = (e == 'x') ? 2 : (e == 'u' ? 4 : 8);
                    uint32_t cp = 0;
                    bool valid = true;
                    for (int d = 0; d < digits; d++) {
                        if (i >= len || !isxdigit((unsigned char)s[i])) {
                            valid = false;
                            break;
                        }
                        char h = s[i++];
                        int v = isdigit((unsigned char)h) ? (h - '0')
                                                          : (tolower((unsigned char)h) - 'a' + 10);
                        cp = (cp << 4) | (uint32_t)v;
                    }
                    if (!valid) {
                        free(out);
                        *ok = false;
                        return NULL;
                    }
                    o = append_utf8(out, len * 4, o, cp);
                    break;
                }
                default:
                    out[o++] = e;
                    break;
                }
            } else {
                out[o++] = s[i++];
            }
        }
        if (!closed) {
            free(out);
            *ok = false;
            return NULL;
        }
        /* Allow trailing whitespace or # comment after closing quote */
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i < len && s[i] != '#') {
            free(out);
            *ok = false;
            return NULL;
        }
        out[o] = '\0';
        return out;
    }

    if (s[0] == '\'') {
        char *out = malloc(len + 1);
        if (out == NULL) {
            *ok = false;
            return NULL;
        }
        size_t o = 0;
        size_t i = 1;
        bool closed = false;
        while (i < len) {
            if (s[i] == '\'') {
                if (i + 1 < len && s[i + 1] == '\'') {
                    out[o++] = '\'';
                    i += 2;
                    continue;
                }
                closed = true;
                i++;
                break;
            }
            out[o++] = s[i++];
        }
        if (!closed) {
            free(out);
            *ok = false;
            return NULL;
        }
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i < len && s[i] != '#') {
            free(out);
            *ok = false;
            return NULL;
        }
        out[o] = '\0';
        return out;
    }

    /* Unquoted scalar: strip trailing " #..." comment */
    size_t end = len;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
            end = i;
            break;
        }
    }
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
    char *out = malloc(end + 1);
    if (out == NULL) {
        *ok = false;
        return NULL;
    }
    memcpy(out, s, end);
    out[end] = '\0';
    return out;
}

static bool add_raw_pat(raw_pat_t **arr, size_t *cnt, size_t *cap,
                        uint16_t gid, char *pat)
{
    if (*cnt == *cap) {
        size_t ncap = *cap ? *cap * 2 : 8;
        raw_pat_t *na = realloc(*arr, ncap * sizeof(*na));
        if (na == NULL) return false;
        *arr = na;
        *cap = ncap;
    }
    (*arr)[*cnt].group_id = gid;
    (*arr)[*cnt].pattern = pat;
    (*cnt)++;
    return true;
}

static bool add_raw_reg(raw_reg_t ***arr, size_t *cnt, size_t *cap, raw_reg_t *r)
{
    if (*cnt == *cap) {
        size_t ncap = *cap ? *cap * 2 : 4;
        raw_reg_t **na = realloc(*arr, ncap * sizeof(*na));
        if (na == NULL) return false;
        *arr = na;
        *cap = ncap;
    }
    (*arr)[(*cnt)++] = r;
    return true;
}

static bool add_raw_inc(char ***arr, size_t *cnt, size_t *cap, char *lang)
{
    if (*cnt == *cap) {
        size_t ncap = *cap ? *cap * 2 : 2;
        char **na = realloc(*arr, ncap * sizeof(*na));
        if (na == NULL) return false;
        *arr = na;
        *cap = ncap;
    }
    (*arr)[(*cnt)++] = lang;
    return true;
}

static bool split_yaml_kv(const char *s, size_t len,
                          char *key_out, size_t key_cap,
                          const char **val_ptr, size_t *val_len)
{
    /* Key may be quoted ("bold default") or bare */
    size_t i = 0;
    size_t klen = 0;
    if (i < len && (s[i] == '"' || s[i] == '\'')) {
        char q = s[i++];
        while (i < len && s[i] != q && klen + 1 < key_cap) {
            key_out[klen++] = s[i++];
        }
        if (i >= len || s[i] != q) return false;
        i++;
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= len || s[i] != ':') return false;
    } else {
        while (i < len && s[i] != ':') {
            if (klen + 1 < key_cap) key_out[klen++] = s[i];
            i++;
        }
        if (i >= len || s[i] != ':') return false;
        while (klen > 0 && (key_out[klen - 1] == ' ' || key_out[klen - 1] == '\t')) {
            klen--;
        }
    }
    key_out[klen] = '\0';
    i++; /* consume ':' */
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    *val_ptr = s + i;
    *val_len = len - i;
    return klen > 0;
}

typedef enum {
    SEC_TOP = 0,
    SEC_DETECT,
    SEC_RULES,
    SEC_REGION
} yaml_sec_t;

typedef struct {
    yaml_sec_t sec;
    int        indent;
    raw_reg_t *reg;
} yaml_frame_t;

static bool parse_syntax_yaml(const char *yaml_text, syn_entry_t *out,
                              char *errbuf, size_t errlen)
{
    memset(out, 0, sizeof(*out));

    yaml_frame_t stack[32];
    int sp = 0;
    stack[0].sec = SEC_TOP;
    stack[0].indent = -1;
    stack[0].reg = NULL;

    const char *p = yaml_text;
    size_t lineno = 0;
    while (*p != '\0') {
        lineno++;
        const char *eol = strchr(p, '\n');
        size_t raw_len = eol ? (size_t)(eol - p) : strlen(p);
        const char *next_p = eol ? eol + 1 : p + raw_len;

        if (raw_len > 0 && p[raw_len - 1] == '\r') raw_len--;

        size_t indent = 0;
        while (indent < raw_len && p[indent] == ' ') indent++;
        if (indent < raw_len && p[indent] == '\t') {
            yaml_err(errbuf, errlen, "line %zu: tab indentation not allowed in YAML", lineno);
            free_syn_entry(out);
            return false;
        }

        const char *s = p + indent;
        size_t len = raw_len - indent;
        if (len == 0 || s[0] == '#') {
            p = next_p;
            continue;
        }

        /* Pop frames whose indent >= current line's indent (except list items at same indent as SEC_RULES) */
        while (sp > 0) {
            yaml_frame_t *top = &stack[sp];
            if ((int)indent < top->indent) {
                sp--;
            } else if ((int)indent == top->indent && top->sec != SEC_RULES) {
                sp--;
            } else if ((int)indent == top->indent && top->sec == SEC_RULES &&
                       !(len >= 2 && s[0] == '-' && (s[1] == ' ' || s[1] == '\t'))) {
                sp--;
            } else {
                break;
            }
        }

        yaml_frame_t *cur = &stack[sp];
        bool is_list = (len >= 2 && s[0] == '-' && (s[1] == ' ' || s[1] == '\t'));

        if (cur->sec == SEC_TOP) {
            char key[64];
            const char *vp = NULL;
            size_t vlen = 0;
            if (!split_yaml_kv(s, len, key, sizeof(key), &vp, &vlen)) {
                yaml_err(errbuf, errlen, "line %zu: invalid top-level syntax", lineno);
                free_syn_entry(out);
                return false;
            }
            bool ok = true;
            char *val = parse_yaml_scalar(vp, vlen, &ok);
            if (!ok) {
                yaml_err(errbuf, errlen, "line %zu: invalid scalar", lineno);
                free_syn_entry(out);
                return false;
            }
            if (strcmp(key, "filetype") == 0) {
                free(out->filetype);
                out->filetype = val;
            } else if (strcmp(key, "detect") == 0) {
                free(val);
                if (sp + 1 < 32) {
                    stack[++sp] = (yaml_frame_t){SEC_DETECT, (int)indent + 1, NULL};
                }
            } else if (strcmp(key, "rules") == 0) {
                free(val);
                if (sp + 1 < 32) {
                    stack[++sp] = (yaml_frame_t){SEC_RULES, (int)indent + 1, NULL};
                }
            } else {
                free(val);
            }
        } else if (cur->sec == SEC_DETECT) {
            char key[64];
            const char *vp = NULL;
            size_t vlen = 0;
            if (!split_yaml_kv(s, len, key, sizeof(key), &vp, &vlen)) {
                yaml_err(errbuf, errlen, "line %zu: invalid detect entry", lineno);
                free_syn_entry(out);
                return false;
            }
            bool ok = true;
            char *val = parse_yaml_scalar(vp, vlen, &ok);
            if (!ok) {
                yaml_err(errbuf, errlen, "line %zu: invalid detect scalar", lineno);
                free_syn_entry(out);
                return false;
            }
            if (strcmp(key, "filename") == 0) {
                free(out->filename_pat);
                out->filename_pat = (*val) ? val : (free(val), NULL);
            } else if (strcmp(key, "header") == 0) {
                free(out->header_pat);
                out->header_pat = (*val) ? val : (free(val), NULL);
            } else if (strcmp(key, "signature") == 0) {
                free(out->signature_pat);
                out->signature_pat = (*val) ? val : (free(val), NULL);
            } else {
                free(val);
            }
        } else if (cur->sec == SEC_RULES) {
            if (!is_list) {
                yaml_err(errbuf, errlen, "line %zu: expected '-' list item in rules", lineno);
                free_syn_entry(out);
                return false;
            }
            cur->indent = (int)indent;
            const char *item = s + 2;
            size_t ilen = len - 2;
            while (ilen > 0 && (*item == ' ' || *item == '\t')) {
                item++;
                ilen--;
            }

            char key[128];
            const char *vp = NULL;
            size_t vlen = 0;
            if (!split_yaml_kv(item, ilen, key, sizeof(key), &vp, &vlen)) {
                yaml_err(errbuf, errlen, "line %zu: invalid rule item", lineno);
                free_syn_entry(out);
                return false;
            }
            bool ok = true;
            char *val = parse_yaml_scalar(vp, vlen, &ok);
            if (!ok) {
                yaml_err(errbuf, errlen, "line %zu: invalid rule scalar", lineno);
                free_syn_entry(out);
                return false;
            }
            /* Note: empty value after ':' means a region map block */
            bool is_region = (*val == '\0' && (vlen == 0 || vp[0] == '#'));
            if (is_region) {
                free(val);
                raw_reg_t *r = calloc(1, sizeof(*r));
                if (r == NULL) {
                    free_syn_entry(out);
                    return false;
                }
                r->group_id = syntax_intern_group(key);
                r->limit_group_id = r->group_id;
                if (cur->reg != NULL) {
                    add_raw_reg(&cur->reg->regs, &cur->reg->reg_count, &cur->reg->reg_cap, r);
                } else {
                    add_raw_reg(&out->regs, &out->reg_count, &out->reg_cap, r);
                }
                if (sp + 1 < 32) {
                    stack[++sp] = (yaml_frame_t){SEC_REGION, (int)indent + 1, r};
                }
            } else if (strcmp(key, "include") == 0) {
                if (cur->reg != NULL) {
                    add_raw_inc(&cur->reg->includes, &cur->reg->inc_count, &cur->reg->inc_cap, val);
                } else {
                    add_raw_inc(&out->includes, &out->inc_count, &out->inc_cap, val);
                }
            } else {
                uint16_t gid = syntax_intern_group(key);
                if (cur->reg != NULL) {
                    add_raw_pat(&cur->reg->pats, &cur->reg->pat_count, &cur->reg->pat_cap, gid, val);
                } else {
                    add_raw_pat(&out->pats, &out->pat_count, &out->pat_cap, gid, val);
                }
            }
        } else if (cur->sec == SEC_REGION) {
            raw_reg_t *r = cur->reg;
            char key[64];
            const char *vp = NULL;
            size_t vlen = 0;
            if (!split_yaml_kv(s, len, key, sizeof(key), &vp, &vlen)) {
                yaml_err(errbuf, errlen, "line %zu: invalid region property", lineno);
                free_syn_entry(out);
                return false;
            }
            bool ok = true;
            char *val = parse_yaml_scalar(vp, vlen, &ok);
            if (!ok) {
                yaml_err(errbuf, errlen, "line %zu: invalid region scalar", lineno);
                free_syn_entry(out);
                return false;
            }
            if (strcmp(key, "start") == 0) {
                free(r->start_pat);
                r->start_pat = val;
            } else if (strcmp(key, "end") == 0) {
                free(r->end_pat);
                r->end_pat = val;
            } else if (strcmp(key, "skip") == 0) {
                free(r->skip_pat);
                r->skip_pat = (*val) ? val : (free(val), NULL);
            } else if (strcmp(key, "limit-group") == 0) {
                if (*val) r->limit_group_id = syntax_intern_group(val);
                free(val);
            } else if (strcmp(key, "rules") == 0) {
                free(val);
                if (sp + 1 < 32) {
                    stack[++sp] = (yaml_frame_t){SEC_RULES, (int)indent + 1, r};
                }
            } else {
                free(val);
            }
        }

        p = next_p;
    }

    if (out->filetype == NULL || out->filetype[0] == '\0') {
        yaml_err(errbuf, errlen, "missing filetype");
        free_syn_entry(out);
        return false;
    }
    return true;
}

/* ========================================================================= */
/* Registry Population & Overrides                                           */
/* ========================================================================= */

static raw_reg_t *build_raw_reg_from_spec(uint32_t reg_idx)
{
    const syntax_reg_spec_t *sp = &g_embedded_syntax_regs[reg_idx];
    raw_reg_t *r = calloc(1, sizeof(*r));
    if (r == NULL) return NULL;

    r->group_id = sp->group_id;
    r->limit_group_id = sp->limit_group_id;
    r->start_pat = strdup(sp->start_pat);
    r->end_pat   = strdup(sp->end_pat);
    r->skip_pat  = sp->skip_pat ? strdup(sp->skip_pat) : NULL;

    for (uint32_t i = 0; i < sp->pat_count; i++) {
        const syntax_pat_spec_t *ps = &g_embedded_syntax_pats[sp->pat_start + i];
        add_raw_pat(&r->pats, &r->pat_count, &r->pat_cap, ps->group_id, strdup(ps->pattern));
    }
    for (uint32_t i = 0; i < sp->subreg_count; i++) {
        uint32_t child_idx = g_embedded_syntax_child_regs[sp->subreg_start + i];
        raw_reg_t *cr = build_raw_reg_from_spec(child_idx);
        if (cr != NULL) {
            add_raw_reg(&r->regs, &r->reg_count, &r->reg_cap, cr);
        }
    }
    for (uint32_t i = 0; i < sp->inc_count; i++) {
        const char *inc = g_embedded_syntax_includes[sp->inc_start + i];
        add_raw_inc(&r->includes, &r->inc_count, &r->inc_cap, strdup(inc));
    }
    return r;
}

static bool registry_upsert_entry(syn_entry_t *src, bool is_override)
{
    for (size_t i = 0; i < g_reg.def_count; i++) {
        if (strcmp(g_reg.defs[i].filetype, src->filetype) == 0) {
            free_syn_entry(&g_reg.defs[i]);
            g_reg.defs[i] = *src;
            g_reg.defs[i].compiled.is_override = is_override;
            memset(src, 0, sizeof(*src));
            return true;
        }
    }

    if (g_reg.def_count == g_reg.def_cap) {
        size_t ncap = g_reg.def_cap ? g_reg.def_cap * 2 : 164;
        syn_entry_t *nd = realloc(g_reg.defs, ncap * sizeof(*nd));
        if (nd == NULL) return false;
        g_reg.defs = nd;
        g_reg.def_cap = ncap;
    }

    g_reg.defs[g_reg.def_count] = *src;
    g_reg.defs[g_reg.def_count].compiled.is_override = is_override;
    g_reg.def_count++;
    memset(src, 0, sizeof(*src));
    return true;
}

bool syntax_registry_load_yaml_string(const char *yaml_text, const char *name,
                                      char *errbuf, size_t errlen)
{
    (void)name;
    if (yaml_text == NULL) return false;
    if (!g_reg.initialized) syntax_registry_init();

    syn_entry_t entry;
    if (!parse_syntax_yaml(yaml_text, &entry, errbuf, errlen)) {
        return false;
    }
    return registry_upsert_entry(&entry, true);
}

bool syntax_registry_load_yaml_file(const char *path, char *errbuf, size_t errlen)
{
    if (path == NULL) return false;
    size_t len = 0;
    char *data = tam_fs_read_file(path, &len);
    if (data == NULL) {
        yaml_err(errbuf, errlen, "cannot read %s", path);
        return false;
    }
    bool ok = syntax_registry_load_yaml_string(data, path, errbuf, errlen);
    free(data);
    return ok;
}

static void load_overrides_from_dir(const char *dir_path)
{
    DIR *d = opendir(dir_path);
    if (d == NULL) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen > 5 && strcmp(ent->d_name + nlen - 5, ".yaml") == 0) {
            char *full = tam_path_join(dir_path, ent->d_name);
            if (full != NULL) {
                syntax_registry_load_yaml_file(full, NULL, 0);
                free(full);
            }
        }
    }
    closedir(d);
}

void syntax_registry_init(void)
{
    if (g_reg.initialized) return;
    g_reg.initialized = true;

    for (size_t i = 0; i < g_embedded_syntax_groups_count; i++) {
        syntax_intern_group(g_embedded_syntax_groups[i]);
    }

    for (size_t i = 0; i < g_embedded_syntax_defs_count; i++) {
        const syntax_def_spec_t *sp = &g_embedded_syntax_defs[i];
        syn_entry_t e;
        memset(&e, 0, sizeof(e));
        e.filetype      = strdup(sp->filetype);
        e.filename_pat  = sp->filename_pat ? strdup(sp->filename_pat) : NULL;
        e.header_pat    = sp->header_pat ? strdup(sp->header_pat) : NULL;
        e.signature_pat = sp->signature_pat ? strdup(sp->signature_pat) : NULL;

        for (uint32_t j = 0; j < sp->pat_count; j++) {
            const syntax_pat_spec_t *ps = &g_embedded_syntax_pats[sp->pat_start + j];
            add_raw_pat(&e.pats, &e.pat_count, &e.pat_cap, ps->group_id, strdup(ps->pattern));
        }
        for (uint32_t j = 0; j < sp->reg_count; j++) {
            uint32_t reg_idx = g_embedded_syntax_child_regs[sp->reg_start + j];
            raw_reg_t *rr = build_raw_reg_from_spec(reg_idx);
            if (rr != NULL) add_raw_reg(&e.regs, &e.reg_count, &e.reg_cap, rr);
        }
        for (uint32_t j = 0; j < sp->inc_count; j++) {
            const char *inc = g_embedded_syntax_includes[sp->inc_start + j];
            add_raw_inc(&e.includes, &e.inc_count, &e.inc_cap, strdup(inc));
        }
        registry_upsert_entry(&e, false);
    }

    /* Scan override directories */
    const char *env_cfg = getenv("TAM_EDIT_CONFIG_HOME");
    if (env_cfg != NULL && *env_cfg != '\0') {
        char *sdir = tam_path_join(env_cfg, "syntax");
        if (sdir != NULL) {
            load_overrides_from_dir(sdir);
            free(sdir);
        }
    } else {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg != NULL && *xdg != '\0') {
            char *base = tam_path_join(xdg, "tam-edit");
            if (base != NULL) {
                char *sdir = tam_path_join(base, "syntax");
                if (sdir != NULL) {
                    load_overrides_from_dir(sdir);
                    free(sdir);
                }
                free(base);
            }
        }
        const char *home = getenv("HOME");
        if (home != NULL && *home != '\0') {
            char sdir[512];
            snprintf(sdir, sizeof(sdir), "%s/.config/tam-edit/syntax", home);
            load_overrides_from_dir(sdir);
        }
    }
}

void syntax_registry_free(void)
{
    if (!g_reg.initialized) return;
    for (size_t i = 0; i < g_reg.group_count; i++) free(g_reg.groups[i]);
    free(g_reg.groups);
    for (size_t i = 0; i < g_reg.def_count; i++) free_syn_entry(&g_reg.defs[i]);
    free(g_reg.defs);
    memset(&g_reg, 0, sizeof(g_reg));
}

/* ========================================================================= */
/* Lazy Compilation & Transitive Include Resolution                          */
/* ========================================================================= */

static syn_entry_t *find_entry(const char *filetype)
{
    if (filetype == NULL || *filetype == '\0') return NULL;
    for (size_t i = 0; i < g_reg.def_count; i++) {
        if (strcmp(g_reg.defs[i].filetype, filetype) == 0) {
            return &g_reg.defs[i];
        }
    }
    return NULL;
}

static void ensure_detect_compiled(syn_entry_t *e)
{
    if (e->detect_compiled) return;
    e->detect_compiled = true;
    if (e->filename_pat)  e->compiled.filename_re  = tam_regex_compile(e->filename_pat, 0, NULL, 0);
    if (e->header_pat)    e->compiled.header_re    = tam_regex_compile(e->header_pat, 0, NULL, 0);
    if (e->signature_pat) e->compiled.signature_re = tam_regex_compile(e->signature_pat, 0, NULL, 0);
}

static bool append_compiled_pattern(syntax_pattern_t **arr, size_t *cnt,
                                    uint16_t gid, const char *pat)
{
    tam_regex_t *re = tam_regex_compile(pat, 0, NULL, 0);
    if (re == NULL) return false;
    syntax_pattern_t *na = realloc(*arr, (*cnt + 1) * sizeof(*na));
    if (na == NULL) {
        tam_regex_free(re);
        return false;
    }
    *arr = na;
    (*arr)[*cnt].group_id = gid;
    (*arr)[*cnt].regex = re;
    (*cnt)++;
    return true;
}

static bool append_compiled_region(syntax_region_t ***arr, size_t *cnt, syntax_region_t *r)
{
    syntax_region_t **na = realloc(*arr, (*cnt + 1) * sizeof(*na));
    if (na == NULL) return false;
    *arr = na;
    (*arr)[(*cnt)++] = r;
    return true;
}

static void compile_rule_list(const raw_pat_t *pats, size_t pat_count,
                              raw_reg_t *const *regs, size_t reg_count,
                              char *const *incs, size_t inc_count,
                              syntax_region_t *parent,
                              syntax_pattern_t **dst_pats, size_t *dst_pat_cnt,
                              syntax_region_t ***dst_regs, size_t *dst_reg_cnt,
                              const char **visiting, size_t visit_depth);

static syntax_region_t *compile_region(const raw_reg_t *rr, syntax_region_t *parent,
                                       const char **visiting, size_t visit_depth)
{
    if (rr->start_pat == NULL || rr->end_pat == NULL) return NULL;
    tam_regex_t *s_re = tam_regex_compile(rr->start_pat, 0, NULL, 0);
    tam_regex_t *e_re = tam_regex_compile(rr->end_pat, 0, NULL, 0);
    if (s_re == NULL || e_re == NULL) {
        tam_regex_free(s_re);
        tam_regex_free(e_re);
        return NULL;
    }
    tam_regex_t *k_re = rr->skip_pat ? tam_regex_compile(rr->skip_pat, 0, NULL, 0) : NULL;

    syntax_region_t *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        tam_regex_free(s_re);
        tam_regex_free(e_re);
        tam_regex_free(k_re);
        return NULL;
    }
    r->group_id = rr->group_id;
    r->limit_group_id = rr->limit_group_id;
    r->parent = parent;
    r->start = s_re;
    r->end = e_re;
    r->skip = k_re;

    compile_rule_list(rr->pats, rr->pat_count, rr->regs, rr->reg_count,
                      rr->includes, rr->inc_count, r,
                      &r->patterns, &r->pattern_count,
                      &r->regions, &r->region_count,
                      visiting, visit_depth);
    return r;
}

static void compile_rule_list(const raw_pat_t *pats, size_t pat_count,
                              raw_reg_t *const *regs, size_t reg_count,
                              char *const *incs, size_t inc_count,
                              syntax_region_t *parent,
                              syntax_pattern_t **dst_pats, size_t *dst_pat_cnt,
                              syntax_region_t ***dst_regs, size_t *dst_reg_cnt,
                              const char **visiting, size_t visit_depth)
{
    for (size_t i = 0; i < pat_count; i++) {
        append_compiled_pattern(dst_pats, dst_pat_cnt, pats[i].group_id, pats[i].pattern);
    }
    for (size_t i = 0; i < reg_count; i++) {
        syntax_region_t *cr = compile_region(regs[i], parent, visiting, visit_depth);
        if (cr != NULL) append_compiled_region(dst_regs, dst_reg_cnt, cr);
    }
    for (size_t i = 0; i < inc_count; i++) {
        const char *lang = incs[i];
        bool seen = false;
        for (size_t v = 0; v < visit_depth; v++) {
            if (strcmp(visiting[v], lang) == 0) {
                seen = true;
                break;
            }
        }
        if (seen || visit_depth >= 16) continue;

        syn_entry_t *inc_e = find_entry(lang);
        if (inc_e == NULL) continue;
        visiting[visit_depth] = lang;
        compile_rule_list(inc_e->pats, inc_e->pat_count,
                          inc_e->regs, inc_e->reg_count,
                          inc_e->includes, inc_e->inc_count,
                          parent,
                          dst_pats, dst_pat_cnt,
                          dst_regs, dst_reg_cnt,
                          visiting, visit_depth + 1);
    }
}

static syntax_def_t *compile_entry(syn_entry_t *e)
{
    if (e == NULL) return NULL;
    ensure_detect_compiled(e);
    if (e->compiled.compiled) return &e->compiled;

    e->compiled.filetype = strdup(e->filetype);
    e->compiled.compiled = true;

    const char *visiting[16];
    visiting[0] = e->filetype;
    compile_rule_list(e->pats, e->pat_count, e->regs, e->reg_count,
                      e->includes, e->inc_count, NULL,
                      &e->compiled.patterns, &e->compiled.pattern_count,
                      &e->compiled.regions, &e->compiled.region_count,
                      visiting, 1);
    return &e->compiled;
}

syntax_def_t *syntax_find_by_filetype(const char *filetype)
{
    if (!g_reg.initialized) syntax_registry_init();
    syn_entry_t *e = find_entry(filetype);
    if (e == NULL) return NULL;
    return compile_entry(e);
}

/* ========================================================================= */
/* Filetype Detection (Filename -> Header -> Signature)                      */
/* ========================================================================= */

syntax_def_t *syntax_detect(const char *path, const char *requested_ft,
                            const char *const *lines, const size_t *lens,
                            size_t line_count, int detectlimit)
{
    if (!g_reg.initialized) syntax_registry_init();

    if (requested_ft != NULL && *requested_ft != '\0' &&
        strcmp(requested_ft, "unknown") != 0) {
        syntax_def_t *d = syntax_find_by_filetype(requested_ft);
        if (d != NULL) return d;
    }

    syn_entry_t *fname_matches[32];
    size_t fname_count = 0;
    syn_entry_t *hdr_matches[32];
    size_t hdr_count = 0;

    size_t path_len = path ? strlen(path) : 0;
    const char *line0 = (lines && line_count > 0 && lines[0]) ? lines[0] : "";
    size_t line0_len = (lens && line_count > 0) ? lens[0] : 0;

    /* Check overrides first, then embedded definitions */
    for (int pass = 0; pass < 2; pass++) {
        bool want_override = (pass == 0);
        for (size_t i = 0; i < g_reg.def_count; i++) {
            syn_entry_t *e = &g_reg.defs[i];
            if (e->compiled.is_override != want_override) continue;
            if (strcmp(e->filetype, "unknown") == 0) continue;

            ensure_detect_compiled(e);
            if (path != NULL && *path != '\0' && e->compiled.filename_re != NULL) {
                if (tam_regex_match(e->compiled.filename_re, path, path_len)) {
                    if (fname_count < 32) fname_matches[fname_count++] = e;
                    continue;
                }
            }
            if (fname_count == 0 && e->compiled.header_re != NULL && line0_len > 0) {
                if (tam_regex_match(e->compiled.header_re, line0, line0_len)) {
                    if (hdr_count < 32) hdr_matches[hdr_count++] = e;
                }
            }
        }
    }

    syn_entry_t **cands = fname_count > 0 ? fname_matches : hdr_matches;
    size_t cand_count = fname_count > 0 ? fname_count : hdr_count;

    syn_entry_t *chosen = NULL;
    if (cand_count == 1) {
        chosen = cands[0];
    } else if (cand_count > 1) {
        size_t limit = line_count;
        if (detectlimit > 0 && line_count > (size_t)detectlimit) {
            limit = (size_t)detectlimit;
        }
        for (size_t c = 0; c < cand_count && chosen == NULL; c++) {
            ensure_detect_compiled(cands[c]);
            if (cands[c]->compiled.signature_re == NULL) continue;
            for (size_t l = 0; l < limit; l++) {
                const char *ln = lines[l] ? lines[l] : "";
                if (tam_regex_match(cands[c]->compiled.signature_re, ln, lens[l])) {
                    chosen = cands[c];
                    break;
                }
            }
        }
        if (chosen == NULL) chosen = cands[0];
    }

    if (chosen == NULL) {
        chosen = find_entry("unknown");
    }
    return compile_entry(chosen);
}

/* ========================================================================= */
/* Line-State Highlighting Machine (Port of pkg/highlight/highlighter.go)    */
/* ========================================================================= */

static size_t count_runes(const char *s, size_t byte_len)
{
    size_t r = 0;
    size_t i = 0;
    while (i < byte_len) {
        uint32_t cp = 0;
        size_t u = tam_utf8_decode(s + i, byte_len - i, &cp);
        (void)cp;
        if (u == 0) break;
        i += u;
        r++;
    }
    return r;
}

static size_t byte_to_rune_pos(const char *s, size_t byte_len, size_t target_byte)
{
    if (target_byte >= byte_len) return count_runes(s, byte_len);
    return count_runes(s, target_byte);
}

static size_t rune_to_byte_pos(const char *s, size_t byte_len, size_t target_rune)
{
    size_t i = 0;
    while (i < byte_len && target_rune > 0) {
        uint32_t cp = 0;
        size_t u = tam_utf8_decode(s + i, byte_len - i, &cp);
        (void)cp;
        if (u == 0) break;
        i += u;
        target_rune--;
    }
    return i;
}

static bool find_index_skip(const tam_regex_t *re, const tam_regex_t *skip,
                            const char *line, size_t len,
                            size_t *out_start_rune, size_t *out_end_rune)
{
    if (re == NULL) return false;
    if (skip == NULL) {
        tam_regex_match_t m;
        if (!tam_regex_find(re, line, len, 0, &m)) return false;
        *out_start_rune = byte_to_rune_pos(line, len, m.start);
        *out_end_rune   = byte_to_rune_pos(line, len, m.end);
        return true;
    }

    /* Replace each rune matched by `skip` with a 1-byte '\x01' placeholder */
    size_t skip_cap = 64;
    tam_regex_match_t stack_skips[64];
    tam_regex_match_t *skips = stack_skips;
    size_t scount = 0;
    size_t spos = 0;
    size_t last_ne_end = (size_t)-1;
    while (spos <= len) {
        tam_regex_match_t sm;
        if (!tam_regex_find(skip, line, len, spos, &sm)) break;
        if (sm.start == sm.end && sm.start == last_ne_end) {
            if (spos >= len) break;
            uint32_t d = 0;
            spos += tam_utf8_decode(line + spos, len - spos, &d);
            continue;
        }
        if (scount == skip_cap) {
            size_t ncap = skip_cap * 2;
            tam_regex_match_t *ns = (skips == stack_skips)
                                        ? malloc(ncap * sizeof(*ns))
                                        : realloc(skips, ncap * sizeof(*ns));
            if (ns == NULL) break;
            if (skips == stack_skips) memcpy(ns, stack_skips, scount * sizeof(*ns));
            skips = ns;
            skip_cap = ncap;
        }
        skips[scount++] = sm;
        if (sm.end > sm.start) {
            spos = sm.end;
            last_ne_end = sm.end;
        } else {
            if (sm.end >= len) break;
            uint32_t d = 0;
            spos = sm.end + tam_utf8_decode(line + sm.end, len - sm.end, &d);
        }
    }

    if (scount == 0) {
        if (skips != stack_skips) free(skips);
        tam_regex_match_t m;
        if (!tam_regex_find(re, line, len, 0, &m)) return false;
        *out_start_rune = byte_to_rune_pos(line, len, m.start);
        *out_end_rune   = byte_to_rune_pos(line, len, m.end);
        return true;
    }

    char stack_buf[512];
    char *masked = (len <= sizeof(stack_buf)) ? stack_buf : malloc(len + 1);
    if (masked == NULL) {
        if (skips != stack_skips) free(skips);
        return false;
    }

    size_t out_i = 0;
    size_t src_i = 0;
    for (size_t k = 0; k < scount; k++) {
        if (skips[k].start > src_i) {
            size_t copy_n = skips[k].start - src_i;
            memcpy(masked + out_i, line + src_i, copy_n);
            out_i += copy_n;
        }
        size_t skip_runes = count_runes(line + skips[k].start,
                                        skips[k].end - skips[k].start);
        for (size_t r = 0; r < skip_runes; r++) {
            masked[out_i++] = '\x01';
        }
        src_i = skips[k].end;
    }
    if (src_i < len) {
        size_t copy_n = len - src_i;
        memcpy(masked + out_i, line + src_i, copy_n);
        out_i += copy_n;
    }

    tam_regex_match_t m;
    bool found = tam_regex_find(re, masked, out_i, 0, &m);
    if (found) {
        *out_start_rune = byte_to_rune_pos(masked, out_i, m.start);
        *out_end_rune   = byte_to_rune_pos(masked, out_i, m.end);
    }
    if (masked != stack_buf) free(masked);
    if (skips != stack_skips) free(skips);
    return found;
}

typedef struct {
    bool     *has_mark;
    uint16_t *mark_group;
    size_t    total_runes;
    bool      states_only;
    const syntax_region_t *last_region;
} hl_ctx_t;

static void set_hl_mark(hl_ctx_t *ctx, size_t rune_idx, uint16_t group_id)
{
    if (ctx->states_only || rune_idx >= ctx->total_runes) return;
    ctx->has_mark[rune_idx] = true;
    ctx->mark_group[rune_idx] = group_id;
}

static void highlight_empty_region(const syntax_def_t *def, hl_ctx_t *ctx,
                                   size_t start_rune, bool can_match_end,
                                   const char *line, size_t byte_len);

static void highlight_region(const syntax_def_t *def, hl_ctx_t *ctx,
                             size_t start_rune, bool can_match_end,
                             const char *line, size_t byte_len,
                             const syntax_region_t *cur_reg)
{
    size_t line_runes = count_runes(line, byte_len);
    if (start_rune == 0 && !ctx->states_only && ctx->total_runes > 0) {
        if (!ctx->has_mark[0]) {
            ctx->has_mark[0] = true;
            ctx->mark_group[0] = cur_reg->group_id;
        }
    }

    const syntax_region_t *first_reg = NULL;
    size_t first_start = line_runes;
    size_t first_end = 0;

    bool has_end = false;
    size_t end_start = 0, end_end = 0;
    bool search_nesting = true;

    if (find_index_skip(cur_reg->end, cur_reg->skip, line, byte_len, &end_start, &end_end)) {
        has_end = true;
        if (start_rune == end_start) {
            search_nesting = false;
        } else {
            first_start = end_start;
            first_end   = end_end;
        }
    }

    if (search_nesting) {
        for (size_t i = 0; i < cur_reg->region_count; i++) {
            const syntax_region_t *r = cur_reg->regions[i];
            size_t rs = 0, re = 0;
            if (find_index_skip(r->start, r->skip, line, byte_len, &rs, &re)) {
                if (rs < first_start) {
                    first_start = rs;
                    first_end   = re;
                    first_reg   = r;
                }
            }
        }
    }

    if (first_reg != NULL && first_start != line_runes) {
        set_hl_mark(ctx, start_rune + first_start, first_reg->limit_group_id);
        size_t b_end = rune_to_byte_pos(line, byte_len, first_end);
        highlight_empty_region(def, ctx, start_rune + first_end, can_match_end,
                               line + b_end, byte_len - b_end);
        highlight_region(def, ctx, start_rune + first_end, can_match_end,
                         line + b_end, byte_len - b_end, first_reg);
        return;
    }

    if (!ctx->states_only && line_runes > 0) {
        uint16_t stack_fg[256];
        uint16_t *full_hl = (line_runes <= 256) ? stack_fg : malloc(line_runes * sizeof(uint16_t));
        if (full_hl != NULL) {
            for (size_t i = 0; i < line_runes; i++) full_hl[i] = cur_reg->group_id;

            if (search_nesting) {
                for (size_t p = 0; p < cur_reg->pattern_count; p++) {
                    const syntax_pattern_t *pat = &cur_reg->patterns[p];
                    if (cur_reg->group_id == cur_reg->limit_group_id ||
                        pat->group_id == cur_reg->limit_group_id) {
                        /* Exact full-line find loop */
                        size_t cur_pos = 0;
                        size_t last_ne_end = (size_t)-1;
                        while (cur_pos <= byte_len) {
                            tam_regex_match_t m;
                            if (!tam_regex_find(pat->regex, line, byte_len, cur_pos, &m)) break;
                            if (m.start == m.end && m.start == last_ne_end) {
                                if (cur_pos >= byte_len) break;
                                uint32_t d = 0;
                                cur_pos += tam_utf8_decode(line + cur_pos, byte_len - cur_pos, &d);
                                continue;
                            }
                            size_t mr_s = byte_to_rune_pos(line, byte_len, m.start);
                            size_t mr_e = byte_to_rune_pos(line, byte_len, m.end);
                            if (!has_end || mr_s < end_start) {
                                for (size_t k = mr_s; k < mr_e && k < line_runes; k++) {
                                    full_hl[k] = pat->group_id;
                                }
                            }
                            if (m.end > m.start) {
                                cur_pos = m.end;
                                last_ne_end = m.end;
                            } else {
                                if (m.end >= byte_len) break;
                                uint32_t d = 0;
                                cur_pos = m.end + tam_utf8_decode(line + m.end, byte_len - m.end, &d);
                            }
                        }
                    }
                }
            }

            for (size_t i = 0; i < line_runes; i++) {
                if (i == 0 || full_hl[i] != full_hl[i - 1]) {
                    set_hl_mark(ctx, start_rune + i, full_hl[i]);
                }
            }
            if (full_hl != stack_fg) free(full_hl);
        }
    }

    if (has_end) {
        set_hl_mark(ctx, start_rune + end_start, cur_reg->limit_group_id);
        size_t b_end = rune_to_byte_pos(line, byte_len, end_end);
        if (cur_reg->parent == NULL) {
            set_hl_mark(ctx, start_rune + end_end, 0);
            highlight_empty_region(def, ctx, start_rune + end_end, can_match_end,
                                   line + b_end, byte_len - b_end);
            return;
        }
        set_hl_mark(ctx, start_rune + end_end, cur_reg->parent->group_id);
        highlight_region(def, ctx, start_rune + end_end, can_match_end,
                         line + b_end, byte_len - b_end, cur_reg->parent);
        return;
    }

    if (can_match_end) ctx->last_region = cur_reg;
}

static void highlight_empty_region(const syntax_def_t *def, hl_ctx_t *ctx,
                                   size_t start_rune, bool can_match_end,
                                   const char *line, size_t byte_len)
{
    size_t line_runes = count_runes(line, byte_len);
    if (line_runes == 0) {
        if (can_match_end) ctx->last_region = NULL;
        return;
    }

    const syntax_region_t *first_reg = NULL;
    size_t first_start = line_runes;
    size_t first_end = 0;

    for (size_t i = 0; i < def->region_count; i++) {
        const syntax_region_t *r = def->regions[i];
        size_t rs = 0, re = 0;
        if (find_index_skip(r->start, r->skip, line, byte_len, &rs, &re)) {
            if (rs < first_start) {
                first_start = rs;
                first_end   = re;
                first_reg   = r;
            }
        }
    }

    if (first_reg != NULL && first_start != line_runes) {
        set_hl_mark(ctx, start_rune + first_start, first_reg->limit_group_id);
        size_t b_first_start = rune_to_byte_pos(line, byte_len, first_start);
        size_t b_first_end   = rune_to_byte_pos(line, byte_len, first_end);
        highlight_empty_region(def, ctx, start_rune, false, line, b_first_start);
        highlight_region(def, ctx, start_rune + first_end, can_match_end,
                         line + b_first_end, byte_len - b_first_end, first_reg);
        return;
    }

    if (ctx->states_only) {
        if (can_match_end) ctx->last_region = NULL;
        return;
    }

    uint16_t stack_fg[256];
    uint16_t *full_hl = (line_runes <= 256) ? stack_fg : calloc(line_runes, sizeof(uint16_t));
    if (full_hl != NULL) {
        if (full_hl == stack_fg) memset(full_hl, 0, line_runes * sizeof(uint16_t));

        for (size_t p = 0; p < def->pattern_count; p++) {
            const syntax_pattern_t *pat = &def->patterns[p];
            size_t cur_pos = 0;
            size_t last_ne_end = (size_t)-1;
            while (cur_pos <= byte_len) {
                tam_regex_match_t m;
                if (!tam_regex_find(pat->regex, line, byte_len, cur_pos, &m)) break;
                if (m.start == m.end && m.start == last_ne_end) {
                    if (cur_pos >= byte_len) break;
                    uint32_t d = 0;
                    cur_pos += tam_utf8_decode(line + cur_pos, byte_len - cur_pos, &d);
                    continue;
                }
                size_t mr_s = byte_to_rune_pos(line, byte_len, m.start);
                size_t mr_e = byte_to_rune_pos(line, byte_len, m.end);
                for (size_t k = mr_s; k < mr_e && k < line_runes; k++) {
                    full_hl[k] = pat->group_id;
                }
                if (m.end > m.start) {
                    cur_pos = m.end;
                    last_ne_end = m.end;
                } else {
                    if (m.end >= byte_len) break;
                    uint32_t d = 0;
                    cur_pos = m.end + tam_utf8_decode(line + m.end, byte_len - m.end, &d);
                }
            }
        }

        for (size_t i = 0; i < line_runes; i++) {
            if (i == 0 || full_hl[i] != full_hl[i - 1]) {
                set_hl_mark(ctx, start_rune + i, full_hl[i]);
            }
        }
        if (full_hl != stack_fg) free(full_hl);
    }

    if (can_match_end) ctx->last_region = NULL;
}

const syntax_region_t *syntax_highlight_line(const syntax_def_t *def,
                                             const syntax_region_t *prev_state,
                                             const char *line, size_t byte_len,
                                             bool states_only,
                                             uint16_t *out_groups, size_t max_runes,
                                             size_t *out_rune_len)
{
    if (line == NULL) {
        line = "";
        byte_len = 0;
    }
    size_t total_runes = count_runes(line, byte_len);
    if (out_rune_len != NULL) *out_rune_len = total_runes;
    if (def == NULL) {
        if (!states_only && out_groups != NULL) {
            size_t n = total_runes < max_runes ? total_runes : max_runes;
            memset(out_groups, 0, n * sizeof(uint16_t));
        }
        return NULL;
    }

    bool stack_mark[256];
    uint16_t stack_grp[256];
    hl_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.total_runes = total_runes;
    ctx.states_only = states_only;

    if (!states_only && total_runes > 0) {
        if (total_runes <= 256) {
            ctx.has_mark = stack_mark;
            ctx.mark_group = stack_grp;
            memset(ctx.has_mark, 0, total_runes * sizeof(bool));
        } else {
            ctx.has_mark = calloc(total_runes, sizeof(bool));
            ctx.mark_group = calloc(total_runes, sizeof(uint16_t));
            if (ctx.has_mark == NULL || ctx.mark_group == NULL) {
                free(ctx.has_mark);
                free(ctx.mark_group);
                return prev_state;
            }
        }
    }

    if (prev_state == NULL) {
        highlight_empty_region(def, &ctx, 0, true, line, byte_len);
    } else {
        highlight_region(def, &ctx, 0, true, line, byte_len, prev_state);
    }

    if (!states_only && out_groups != NULL && total_runes > 0) {
        size_t n = total_runes < max_runes ? total_runes : max_runes;
        uint16_t cur = 0;
        for (size_t i = 0; i < n; i++) {
            if (ctx.has_mark[i]) cur = ctx.mark_group[i];
            out_groups[i] = cur;
        }
    }

    if (!states_only && total_runes > 256) {
        free(ctx.has_mark);
        free(ctx.mark_group);
    }
    return ctx.last_region;
}

/* ========================================================================= */
/* Buffer Integration & Direct-Mapped Viewport Cache                         */
/* ========================================================================= */

#define SYNTAX_CACHE_SLOTS 128

typedef struct {
    uint64_t               gen;
    size_t                 line_idx;
    const syntax_region_t *prev_state;
    uint16_t              *groups;
    size_t                 rune_len;
    size_t                 cap;
} syntax_cache_slot_t;

struct syntax_line_cache {
    uint64_t            gen;
    syntax_cache_slot_t slots[SYNTAX_CACHE_SLOTS];
};

static bool ensure_state_cap(buffer_t *buf, size_t need)
{
    if (need <= buf->state_cap) return true;
    size_t ncap = buf->state_cap ? buf->state_cap : 64;
    while (ncap < need) ncap *= 2;
    const syntax_region_t **ns = realloc((void *)buf->line_states, ncap * sizeof(*ns));
    if (ns == NULL) return false;
    memset((void *)(ns + buf->state_cap), 0, (ncap - buf->state_cap) * sizeof(*ns));
    buf->line_states = ns;
    buf->state_cap = ncap;
    return true;
}

void buffer_syntax_init(buffer_t *buf, const char *requested_ft, int detectlimit)
{
    if (buf == NULL) return;
    syntax_registry_init();

    buf->syntax_def = syntax_detect(buf->path, requested_ft,
                                    (const char *const *)buf->doc.lines,
                                    buf->doc.lens, buf->doc.count, detectlimit);
    if (buf->syntax_def != NULL && buf->syntax_def->filetype != NULL) {
        snprintf(buf->filetype, sizeof(buf->filetype), "%s", buf->syntax_def->filetype);
    } else {
        snprintf(buf->filetype, sizeof(buf->filetype), "unknown");
    }

    if (buf->syntax_cache == NULL) {
        buf->syntax_cache = calloc(1, sizeof(struct syntax_line_cache));
    }
    if (buf->syntax_cache != NULL) {
        buf->syntax_cache->gen++;
    }

    buf->state_count = buf->doc.count;
    ensure_state_cap(buf, buf->doc.count);
    if (buf->line_states != NULL && buf->doc.count > 0) {
        memset((void *)buf->line_states, 0, buf->doc.count * sizeof(*buf->line_states));
    }
    buf->syntax_valid_through = 0;
}

void buffer_syntax_free(buffer_t *buf)
{
    if (buf == NULL) return;
    free((void *)buf->line_states);
    buf->line_states = NULL;
    buf->state_count = 0;
    buf->state_cap = 0;
    if (buf->syntax_cache != NULL) {
        for (size_t i = 0; i < SYNTAX_CACHE_SLOTS; i++) {
            free(buf->syntax_cache->slots[i].groups);
        }
        free(buf->syntax_cache);
        buf->syntax_cache = NULL;
    }
}

void buffer_syntax_update_to(buffer_t *buf, size_t target_line)
{
    if (buf == NULL || !buf->syntax_enabled || buf->syntax_def == NULL) return;
    if (buf->doc.count == 0) return;

    if (target_line >= buf->doc.count) target_line = buf->doc.count - 1;
    if (buf->syntax_valid_through > target_line && buf->state_count == buf->doc.count) {
        return;
    }

    if (!ensure_state_cap(buf, buf->doc.count)) return;
    buf->state_count = buf->doc.count;

    size_t start = buf->syntax_valid_through;
    const syntax_region_t *state = (start > 0 && start - 1 < buf->state_count)
                                       ? buf->line_states[start - 1]
                                       : NULL;

    for (size_t i = start; i <= target_line; i++) {
        state = syntax_highlight_line(buf->syntax_def, state,
                                      buf->doc.lines[i], buf->doc.lens[i],
                                      true, NULL, 0, NULL);
        buf->line_states[i] = state;
    }
    buf->syntax_valid_through = target_line + 1;
}

void buffer_syntax_on_edit(buffer_t *buf, size_t start_line,
                           size_t old_line_count, size_t new_line_count)
{
    if (buf == NULL) return;
    if (buf->syntax_cache != NULL) buf->syntax_cache->gen++;

    if (!buf->syntax_enabled || buf->syntax_def == NULL) {
        buf->syntax_valid_through = 0;
        return;
    }

    if (!ensure_state_cap(buf, buf->doc.count)) return;

    if (buf->state_count > 0 && old_line_count != new_line_count) {
        if (new_line_count > old_line_count) {
            size_t delta = new_line_count - old_line_count;
            size_t tail_from = start_line + 1;
            if (tail_from <= buf->state_count) {
                size_t move_n = buf->state_count - tail_from;
                if (tail_from + delta + move_n <= buf->state_cap) {
                    memmove((void *)(buf->line_states + tail_from + delta),
                            (void *)(buf->line_states + tail_from),
                            move_n * sizeof(*buf->line_states));
                    memset((void *)(buf->line_states + tail_from), 0,
                           delta * sizeof(*buf->line_states));
                }
            }
        } else {
            size_t delta = old_line_count - new_line_count;
            size_t tail_from = start_line + 1 + delta;
            if (tail_from <= buf->state_count) {
                size_t move_n = buf->state_count - tail_from;
                memmove((void *)(buf->line_states + start_line + 1),
                        (void *)(buf->line_states + tail_from),
                        move_n * sizeof(*buf->line_states));
            }
        }
    }
    buf->state_count = buf->doc.count;

    if (start_line >= buf->syntax_valid_through) {
        return;
    }

    /* Re-scan states downward from start_line until convergence or valid_through */
    const syntax_region_t *state = (start_line > 0 && start_line - 1 < buf->state_count)
                                       ? buf->line_states[start_line - 1]
                                       : NULL;
    size_t limit = buf->syntax_valid_through;
    if (new_line_count > old_line_count) {
        limit += (new_line_count - old_line_count);
    } else if (old_line_count > new_line_count) {
        size_t d = old_line_count - new_line_count;
        limit = (limit > d) ? (limit - d) : 0;
    }
    if (limit > buf->doc.count) limit = buf->doc.count;

    size_t min_dirty_end = start_line + (new_line_count > 0 ? new_line_count : 1);
    if (min_dirty_end > limit) min_dirty_end = limit;

    for (size_t i = start_line; i < limit; i++) {
        state = syntax_highlight_line(buf->syntax_def, state,
                                      buf->doc.lines[i], buf->doc.lens[i],
                                      true, NULL, 0, NULL);
        const syntax_region_t *old_s = buf->line_states[i];
        buf->line_states[i] = state;
        if (i + 1 >= min_dirty_end && state == old_s) {
            break;
        }
    }
    buf->syntax_valid_through = limit;
    if (buf->syntax_valid_through < min_dirty_end) {
        buf->syntax_valid_through = min_dirty_end;
    }
    if (buf->syntax_valid_through > buf->doc.count) {
        buf->syntax_valid_through = buf->doc.count;
    }
}

const uint16_t *buffer_syntax_get_line_groups(struct buffer *buf, size_t line_idx,
                                              size_t *out_runes)
{
    if (out_runes != NULL) *out_runes = 0;
    if (buf == NULL || !buf->syntax_enabled || buf->syntax_def == NULL ||
        line_idx >= buf->doc.count || buf->syntax_cache == NULL) {
        return NULL;
    }

    buffer_syntax_update_to(buf, line_idx);

    const syntax_region_t *prev = (line_idx > 0 && line_idx - 1 < buf->state_count)
                                      ? buf->line_states[line_idx - 1]
                                      : NULL;
    syntax_cache_slot_t *slot = &buf->syntax_cache->slots[line_idx % SYNTAX_CACHE_SLOTS];

    if (slot->gen == buf->syntax_cache->gen &&
        slot->line_idx == line_idx &&
        slot->prev_state == prev) {
        if (out_runes != NULL) *out_runes = slot->rune_len;
        return slot->groups;
    }

    size_t need_runes = count_runes(buf->doc.lines[line_idx], buf->doc.lens[line_idx]);
    if (need_runes == 0) {
        if (out_runes != NULL) *out_runes = 0;
        return NULL;
    }

    if (need_runes > slot->cap) {
        uint16_t *ng = realloc(slot->groups, need_runes * sizeof(*ng));
        if (ng == NULL) return NULL;
        slot->groups = ng;
        slot->cap = need_runes;
    }

    size_t got_runes = 0;
    syntax_highlight_line(buf->syntax_def, prev,
                          buf->doc.lines[line_idx], buf->doc.lens[line_idx],
                          false, slot->groups, slot->cap, &got_runes);
    slot->gen = buf->syntax_cache->gen;
    slot->line_idx = line_idx;
    slot->prev_state = prev;
    slot->rune_len = got_runes;

    if (out_runes != NULL) *out_runes = got_runes;
    return slot->groups;
}
