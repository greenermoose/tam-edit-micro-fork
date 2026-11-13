#include "regex.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* UTF-8 & Character Helpers                                                 */
/* ========================================================================= */

static size_t utf8_next(const char *s, size_t len, uint32_t *cp_out)
{
    if (len == 0) {
        *cp_out = 0;
        return 0;
    }
    const unsigned char *u = (const unsigned char *)s;
    unsigned char b0 = u[0];
    if (b0 < 0x80) {
        *cp_out = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0 && len >= 2 && (u[1] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(u[1] & 0x3F);
        if (cp >= 0x80) {
            *cp_out = cp;
            return 2;
        }
    } else if ((b0 & 0xF0) == 0xE0 && len >= 3 &&
               (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) |
                      ((uint32_t)(u[1] & 0x3F) << 6) |
                      (uint32_t)(u[2] & 0x3F);
        if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) {
            *cp_out = cp;
            return 3;
        }
    } else if ((b0 & 0xF8) == 0xF0 && len >= 4 &&
               (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 &&
               (u[3] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) |
                      ((uint32_t)(u[1] & 0x3F) << 12) |
                      ((uint32_t)(u[2] & 0x3F) << 6) |
                      (uint32_t)(u[3] & 0x3F);
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            *cp_out = cp;
            return 4;
        }
    }
    *cp_out = 0xFFFDu;
    return 1;
}

static uint32_t utf8_prev(const char *text, size_t pos)
{
    if (pos == 0) return 0;
    size_t back = 1;
    while (back < 4 && back < pos &&
           (((const unsigned char *)text)[pos - back] & 0xC0) == 0x80) {
        back++;
    }
    uint32_t cp = 0xFFFDu;
    size_t used = utf8_next(text + (pos - back), back, &cp);
    if (used != back) return 0xFFFDu;
    return cp;
}

static bool is_word_char(uint32_t cp)
{
    return (cp >= '0' && cp <= '9') ||
           (cp >= 'A' && cp <= 'Z') ||
           (cp >= 'a' && cp <= 'z') ||
           cp == '_';
}

static uint32_t fold_ascii_latin(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) return cp + 32;
    return cp;
}

typedef struct {
    uint32_t lo;
    uint32_t hi;
} cp_range_t;

static const cp_range_t g_unicode_sm_ranges[] = {
    {0x002B,0x002B},{0x003C,0x003C},{0x003D,0x003E},{0x007C,0x007C},
    {0x007E,0x007E},{0x00AC,0x00AC},{0x00B1,0x00B1},{0x00D7,0x00D7},
    {0x00F7,0x00F7},{0x03F6,0x03F6},{0x0606,0x0606},{0x0607,0x0608},
    {0x2044,0x2044},{0x2052,0x2052},{0x207A,0x207C},{0x208A,0x208C},
    {0x2118,0x2118},{0x2140,0x2140},{0x2141,0x2144},{0x214B,0x214B},
    {0x2190,0x2190},{0x2191,0x2194},{0x219A,0x219B},{0x21A0,0x21A0},
    {0x21A3,0x21A3},{0x21A6,0x21A6},{0x21AE,0x21AE},{0x21CE,0x21CE},
    {0x21CF,0x21CF},{0x21D2,0x21D2},{0x21D4,0x21D4},{0x21F4,0x21F4},
    {0x21F5,0x22FF},{0x2320,0x2321},{0x237C,0x237C},{0x239B,0x239B},
    {0x239C,0x23B3},{0x23DC,0x23E1},{0x25B7,0x25B7},{0x25C1,0x25C1},
    {0x25F8,0x25FF},{0x266F,0x266F},{0x27C0,0x27C0},{0x27C1,0x27C4},
    {0x27C7,0x27E5},{0x27F0,0x27FF},{0x2900,0x2982},{0x2999,0x29D7},
    {0x29DC,0x29FB},{0x29FE,0x2AFF},{0x2B30,0x2B44},{0x2B47,0x2B4C},
    {0xFB29,0xFB29},{0xFE62,0xFE62},{0xFE64,0xFE66},{0xFF0B,0xFF0B},
    {0xFF1C,0xFF1C},{0xFF1D,0xFF1E},{0xFF5C,0xFF5C},{0xFF5E,0xFF5E},
    {0xFFE2,0xFFE2},{0xFFE9,0xFFE9},{0xFFEA,0xFFEC},{0x10D8E,0x10D8F},
    {0x1CEF0,0x1CEF0},{0x1D6C1,0x1D6C1},{0x1D6DB,0x1D6DB},{0x1D6FB,0x1D6FB},
    {0x1D715,0x1D715},{0x1D735,0x1D735},{0x1D74F,0x1D74F},{0x1D76F,0x1D76F},
    {0x1D789,0x1D789},{0x1D7A9,0x1D7A9},{0x1D7C3,0x1D7C3},{0x1EEF0,0x1EEF0},
    {0x1EEF1,0x1EEF1},{0x1F8D0,0x1F8D0},{0x1F8D1,0x1F8D8}
};

/* ========================================================================= */
/* Character Class Data                                                      */
/* ========================================================================= */

typedef struct {
    cp_range_t *ranges;
    size_t      count;
    size_t      cap;
    bool        negated;
    bool        icase;
} char_class_t;

static bool cc_add_range(char_class_t *cc, uint32_t lo, uint32_t hi)
{
    if (lo > hi) {
        uint32_t tmp = lo;
        lo = hi;
        hi = tmp;
    }
    if (cc->count == cc->cap) {
        size_t ncap = cc->cap ? cc->cap * 2 : 8;
        cp_range_t *nr = realloc(cc->ranges, ncap * sizeof(*nr));
        if (nr == NULL) return false;
        cc->ranges = nr;
        cc->cap = ncap;
    }
    cc->ranges[cc->count].lo = lo;
    cc->ranges[cc->count].hi = hi;
    cc->count++;
    return true;
}

static bool cc_add_ranges(char_class_t *cc, const cp_range_t *r, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (!cc_add_range(cc, r[i].lo, r[i].hi)) return false;
    }
    return true;
}

static bool cc_add_shorthand(char_class_t *cc, char ch)
{
    switch (ch) {
    case 'd':
        return cc_add_range(cc, '0', '9');
    case 'D':
        return cc_add_range(cc, 0, '0' - 1) && cc_add_range(cc, '9' + 1, 0x10FFFFu);
    case 'w':
        return cc_add_range(cc, '0', '9') &&
               cc_add_range(cc, 'A', 'Z') &&
               cc_add_range(cc, '_', '_') &&
               cc_add_range(cc, 'a', 'z');
    case 'W':
        return cc_add_range(cc, 0, '0' - 1) &&
               cc_add_range(cc, '9' + 1, 'A' - 1) &&
               cc_add_range(cc, 'Z' + 1, '_' - 1) &&
               cc_add_range(cc, '_' + 1, 'a' - 1) &&
               cc_add_range(cc, 'z' + 1, 0x10FFFFu);
    case 's':
        return cc_add_range(cc, '\t', '\r') && cc_add_range(cc, ' ', ' ');
    case 'S':
        return cc_add_range(cc, 0, '\t' - 1) &&
               cc_add_range(cc, '\r' + 1, ' ' - 1) &&
               cc_add_range(cc, ' ' + 1, 0x10FFFFu);
    default:
        return false;
    }
}

static bool cc_contains_raw(const char_class_t *cc, uint32_t cp)
{
    for (size_t i = 0; i < cc->count; i++) {
        if (cp >= cc->ranges[i].lo && cp <= cc->ranges[i].hi) return true;
    }
    return false;
}

static bool cc_match(const char_class_t *cc, uint32_t cp)
{
    bool hit = cc_contains_raw(cc, cp);
    if (!hit && cc->icase) {
        if (cp >= 'a' && cp <= 'z') {
            hit = cc_contains_raw(cc, cp - ('a' - 'A'));
        } else if (cp >= 'A' && cp <= 'Z') {
            hit = cc_contains_raw(cc, cp + ('a' - 'A'));
        } else if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) {
            hit = cc_contains_raw(cc, cp + 32);
        } else if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7) {
            hit = cc_contains_raw(cc, cp - 32);
        }
    }
    return cc->negated ? !hit : hit;
}

/* ========================================================================= */
/* AST & NFA Bytecode Definitions                                            */
/* ========================================================================= */

typedef enum {
    AST_EMPTY = 0,
    AST_CHAR,
    AST_ANY,
    AST_CLASS,
    AST_BOL,
    AST_EOL,
    AST_WB,
    AST_NWB,
    AST_CONCAT,
    AST_ALT,
    AST_REPEAT
} ast_kind_t;

typedef struct {
    ast_kind_t kind;
    int        flags;
    uint32_t   cp;
    uint32_t   class_idx;
    int32_t    left;
    int32_t    right;
    int        min_rep;
    int        max_rep; /* -1 for unbounded */
    bool       lazy;
} ast_node_t;

typedef enum {
    OP_CHAR = 0,
    OP_ANY,
    OP_CLASS,
    OP_SPLIT,
    OP_JMP,
    OP_BOL,
    OP_EOL,
    OP_WB,
    OP_NWB,
    OP_MATCH
} opcode_t;

typedef struct {
    opcode_t op;
    uint8_t  flags;
    uint32_t arg;  /* codepoint or class_idx or jmp target */
    uint32_t arg2; /* second target for OP_SPLIT */
} insn_t;

struct tam_regex {
    insn_t       *prog;
    size_t        prog_len;
    size_t        prog_cap;
    char_class_t *classes;
    size_t        class_count;
    size_t        class_cap;
};

/* ========================================================================= */
/* Recursive-Descent Parser                                                  */
/* ========================================================================= */

#define MAX_PROG_INSNS 65536
#define MAX_REPEAT_BOUND 256

typedef struct {
    const char   *pat;
    size_t        len;
    size_t        pos;
    int           depth;
    ast_node_t   *nodes;
    size_t        node_count;
    size_t        node_cap;
    char_class_t *classes;
    size_t        class_count;
    size_t        class_cap;
    char         *errbuf;
    size_t        errlen;
    bool          error;
} parser_t;

static void set_error(parser_t *p, const char *fmt, ...)
{
    if (p->error) return;
    p->error = true;
    if (p->errbuf != NULL && p->errlen > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(p->errbuf, p->errlen, fmt, ap);
        va_end(ap);
    }
}

static int32_t new_node(parser_t *p, ast_kind_t kind, int flags)
{
    if (p->error) return -1;
    if (p->node_count == p->node_cap) {
        size_t ncap = p->node_cap ? p->node_cap * 2 : 64;
        ast_node_t *nn = realloc(p->nodes, ncap * sizeof(*nn));
        if (nn == NULL) {
            set_error(p, "out of memory");
            return -1;
        }
        p->nodes = nn;
        p->node_cap = ncap;
    }
    int32_t idx = (int32_t)p->node_count++;
    memset(&p->nodes[idx], 0, sizeof(ast_node_t));
    p->nodes[idx].kind = kind;
    p->nodes[idx].flags = flags;
    p->nodes[idx].left = -1;
    p->nodes[idx].right = -1;
    return idx;
}

static uint32_t new_class(parser_t *p, char_class_t cc)
{
    if (p->class_count == p->class_cap) {
        size_t ncap = p->class_cap ? p->class_cap * 2 : 16;
        char_class_t *nc = realloc(p->classes, ncap * sizeof(*nc));
        if (nc == NULL) {
            free(cc.ranges);
            set_error(p, "out of memory");
            return 0;
        }
        p->classes = nc;
        p->class_cap = ncap;
    }
    uint32_t idx = (uint32_t)p->class_count++;
    p->classes[idx] = cc;
    return idx;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_digits(parser_t *p, int count, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < count; i++) {
        if (p->pos >= p->len) return false;
        int d = hex_val(p->pat[p->pos]);
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
        p->pos++;
    }
    *out = v;
    return true;
}

static bool parse_escape_char(parser_t *p, uint32_t *cp_out)
{
    if (p->pos >= p->len) {
        set_error(p, "trailing backslash");
        return false;
    }
    char c = p->pat[p->pos++];
    switch (c) {
    case 'n': *cp_out = '\n'; return true;
    case 'r': *cp_out = '\r'; return true;
    case 't': *cp_out = '\t'; return true;
    case 'f': *cp_out = '\f'; return true;
    case 'v': *cp_out = '\v'; return true;
    case 'a': *cp_out = '\a'; return true;
    case 'x': {
        if (p->pos < p->len && p->pat[p->pos] == '{') {
            p->pos++;
            uint32_t v = 0;
            size_t digits = 0;
            while (p->pos < p->len && p->pat[p->pos] != '}') {
                int d = hex_val(p->pat[p->pos++]);
                if (d < 0) {
                    set_error(p, "invalid \\x{...} escape");
                    return false;
                }
                v = (v << 4) | (uint32_t)d;
                digits++;
            }
            if (digits == 0 || p->pos >= p->len || p->pat[p->pos] != '}') {
                set_error(p, "unterminated \\x{...}");
                return false;
            }
            p->pos++;
            *cp_out = v;
            return true;
        }
        uint32_t v = 0;
        if (!parse_hex_digits(p, 2, &v)) {
            set_error(p, "invalid \\xHH escape");
            return false;
        }
        *cp_out = v;
        return true;
    }
    case 'u': {
        uint32_t v = 0;
        if (!parse_hex_digits(p, 4, &v)) {
            set_error(p, "invalid \\uHHHH escape");
            return false;
        }
        *cp_out = v;
        return true;
    }
    case 'U': {
        uint32_t v = 0;
        if (!parse_hex_digits(p, 8, &v)) {
            set_error(p, "invalid \\UHHHHHHHH escape");
            return false;
        }
        *cp_out = v;
        return true;
    }
    default:
        p->pos--;
        p->pos += utf8_next(p->pat + p->pos, p->len - p->pos, cp_out);
        return true;
    }
}

static bool try_parse_posix_class(parser_t *p, char_class_t *cc)
{
    if (p->pos + 2 >= p->len || p->pat[p->pos] != '[' || p->pat[p->pos + 1] != ':') {
        return false;
    }
    const char *start = p->pat + p->pos + 2;
    size_t rem = p->len - (p->pos + 2);
    const char *end = NULL;
    for (size_t i = 0; i + 1 < rem; i++) {
        if (start[i] == ':' && start[i + 1] == ']') {
            end = start + i;
            break;
        }
    }
    if (end == NULL) return false;

    size_t nlen = (size_t)(end - start);
    bool ok = true;
    if (nlen == 5 && strncmp(start, "alnum", 5) == 0) {
        ok = cc_add_range(cc, '0', '9') && cc_add_range(cc, 'A', 'Z') && cc_add_range(cc, 'a', 'z');
    } else if (nlen == 5 && strncmp(start, "alpha", 5) == 0) {
        ok = cc_add_range(cc, 'A', 'Z') && cc_add_range(cc, 'a', 'z');
    } else if (nlen == 5 && strncmp(start, "digit", 5) == 0) {
        ok = cc_add_range(cc, '0', '9');
    } else if (nlen == 5 && strncmp(start, "graph", 5) == 0) {
        ok = cc_add_range(cc, 0x21, 0x7E);
    } else if (nlen == 5 && strncmp(start, "space", 5) == 0) {
        ok = cc_add_range(cc, '\t', '\r') && cc_add_range(cc, ' ', ' ');
    } else if (nlen == 6 && strncmp(start, "xdigit", 6) == 0) {
        ok = cc_add_range(cc, '0', '9') && cc_add_range(cc, 'A', 'F') && cc_add_range(cc, 'a', 'f');
    } else if (nlen == 5 && strncmp(start, "lower", 5) == 0) {
        ok = cc_add_range(cc, 'a', 'z');
    } else if (nlen == 5 && strncmp(start, "upper", 5) == 0) {
        ok = cc_add_range(cc, 'A', 'Z');
    } else if (nlen == 5 && strncmp(start, "print", 5) == 0) {
        ok = cc_add_range(cc, 0x20, 0x7E);
    } else if (nlen == 5 && strncmp(start, "punct", 5) == 0) {
        ok = cc_add_range(cc, '!', '/') && cc_add_range(cc, ':', '@') &&
             cc_add_range(cc, '[', '`') && cc_add_range(cc, '{', '~');
    } else if (nlen == 5 && strncmp(start, "ascii", 5) == 0) {
        ok = cc_add_range(cc, 0x00, 0x7F);
    } else if (nlen == 5 && strncmp(start, "blank", 5) == 0) {
        ok = cc_add_range(cc, '\t', '\t') && cc_add_range(cc, ' ', ' ');
    } else if (nlen == 5 && strncmp(start, "cntrl", 5) == 0) {
        ok = cc_add_range(cc, 0x00, 0x1F) && cc_add_range(cc, 0x7F, 0x7F);
    } else if (nlen == 4 && strncmp(start, "word", 4) == 0) {
        ok = cc_add_shorthand(cc, 'w');
    } else {
        set_error(p, "unsupported POSIX class");
        return false;
    }

    if (!ok) {
        set_error(p, "out of memory");
        return false;
    }
    p->pos += 2 + nlen + 2;
    return true;
}

static bool parse_unicode_prop(parser_t *p, char_class_t *cc)
{
    if (p->pos >= p->len || p->pat[p->pos] != '{') {
        set_error(p, "expected { after \\p");
        return false;
    }
    p->pos++;
    size_t start = p->pos;
    while (p->pos < p->len && p->pat[p->pos] != '}') p->pos++;
    if (p->pos >= p->len) {
        set_error(p, "unterminated \\p{...}");
        return false;
    }
    size_t plen = p->pos - start;
    p->pos++; /* consume '}' */

    if (plen == 2 && strncmp(p->pat + start, "Sm", 2) == 0) {
        if (!cc_add_ranges(cc, g_unicode_sm_ranges,
                           sizeof(g_unicode_sm_ranges) / sizeof(g_unicode_sm_ranges[0]))) {
            set_error(p, "out of memory");
            return false;
        }
        return true;
    }
    set_error(p, "unsupported unicode property");
    return false;
}

static int32_t parse_char_class(parser_t *p, int flags)
{
    char_class_t cc;
    memset(&cc, 0, sizeof(cc));
    cc.icase = (flags & TAM_REGEX_ICASE) != 0;

    if (p->pos < p->len && p->pat[p->pos] == '^') {
        cc.negated = true;
        p->pos++;
    }

    bool first = true;
    while (p->pos < p->len) {
        if (!first && p->pat[p->pos] == ']') {
            p->pos++;
            int32_t n = new_node(p, AST_CLASS, flags);
            if (n < 0) {
                free(cc.ranges);
                return -1;
            }
            p->nodes[n].class_idx = new_class(p, cc);
            return n;
        }
        first = false;

        if (p->pat[p->pos] == '[' && p->pos + 1 < p->len && p->pat[p->pos + 1] == ':') {
            if (try_parse_posix_class(p, &cc)) continue;
            if (p->error) {
                free(cc.ranges);
                return -1;
            }
        }

        uint32_t lo = 0;
        bool is_single = true;
        if (p->pat[p->pos] == '\\') {
            p->pos++;
            if (p->pos < p->len && strchr("dDwWsS", p->pat[p->pos]) != NULL) {
                if (!cc_add_shorthand(&cc, p->pat[p->pos++])) {
                    free(cc.ranges);
                    set_error(p, "out of memory");
                    return -1;
                }
                is_single = false;
            } else if (p->pos < p->len && p->pat[p->pos] == 'p') {
                p->pos++;
                if (!parse_unicode_prop(p, &cc)) {
                    free(cc.ranges);
                    return -1;
                }
                is_single = false;
            } else if (!parse_escape_char(p, &lo)) {
                free(cc.ranges);
                return -1;
            }
        } else {
            p->pos += utf8_next(p->pat + p->pos, p->len - p->pos, &lo);
        }

        if (!is_single) continue;

        /* Check for range a-b */
        if (p->pos + 1 < p->len && p->pat[p->pos] == '-' && p->pat[p->pos + 1] != ']') {
            p->pos++; /* consume '-' */
            uint32_t hi = 0;
            if (p->pat[p->pos] == '\\') {
                p->pos++;
                if (!parse_escape_char(p, &hi)) {
                    free(cc.ranges);
                    return -1;
                }
            } else {
                p->pos += utf8_next(p->pat + p->pos, p->len - p->pos, &hi);
            }
            if (lo > hi) {
                free(cc.ranges);
                set_error(p, "invalid character class range");
                return -1;
            }
            if (!cc_add_range(&cc, lo, hi)) {
                free(cc.ranges);
                set_error(p, "out of memory");
                return -1;
            }
        } else {
            if (!cc_add_range(&cc, lo, lo)) {
                free(cc.ranges);
                set_error(p, "out of memory");
                return -1;
            }
        }
    }

    free(cc.ranges);
    set_error(p, "unterminated character class");
    return -1;
}

static int32_t parse_alt(parser_t *p, int flags);

static bool parse_flag_changes(parser_t *p, int *flags_io, bool *ended_with_colon)
{
    int flags = *flags_io;
    bool neg = false;
    bool seen_any = false;
    *ended_with_colon = false;

    while (p->pos < p->len) {
        char c = p->pat[p->pos];
        if (c == '-') {
            neg = true;
            p->pos++;
            continue;
        }
        int bit = 0;
        if (c == 'i') bit = TAM_REGEX_ICASE;
        else if (c == 'm') bit = TAM_REGEX_MULTILINE;
        else if (c == 's') bit = TAM_REGEX_DOTALL;
        else if (c == 'U') bit = TAM_REGEX_UNGREEDY;
        else if (c == ':') {
            p->pos++;
            *ended_with_colon = true;
            *flags_io = flags;
            return true;
        } else if (c == ')' && seen_any) {
            p->pos++;
            *flags_io = flags;
            return true;
        } else {
            return false;
        }
        seen_any = true;
        if (neg) flags &= ~bit;
        else     flags |= bit;
        p->pos++;
    }
    return false;
}

static int32_t parse_atom(parser_t *p, int *flags_io, bool *was_flag_directive)
{
    *was_flag_directive = false;
    if (p->pos >= p->len || p->error) return -1;

    char c = p->pat[p->pos];
    if (c == '(') {
        if (p->depth >= 128) {
            set_error(p, "nesting depth exceeded");
            return -1;
        }
        p->pos++;
        if (p->pos < p->len && p->pat[p->pos] == '?') {
            p->pos++;
            if (p->pos < p->len && p->pat[p->pos] == ':') {
                p->pos++;
                p->depth++;
                int32_t sub = parse_alt(p, *flags_io);
                p->depth--;
                if (p->error) return -1;
                if (p->pos >= p->len || p->pat[p->pos] != ')') {
                    set_error(p, "unterminated non-capturing group");
                    return -1;
                }
                p->pos++;
                return sub;
            }
            int sub_flags = *flags_io;
            bool has_colon = false;
            if (!parse_flag_changes(p, &sub_flags, &has_colon)) {
                set_error(p, "unsupported group or flag syntax");
                return -1;
            }
            if (!has_colon) {
                /* Standalone inline flag directive (?i) / (?-i) */
                *flags_io = sub_flags;
                *was_flag_directive = true;
                return new_node(p, AST_EMPTY, *flags_io);
            }
            p->depth++;
            int32_t sub = parse_alt(p, sub_flags);
            p->depth--;
            if (p->error) return -1;
            if (p->pos >= p->len || p->pat[p->pos] != ')') {
                set_error(p, "unterminated scoped flag group");
                return -1;
            }
            p->pos++;
            return sub;
        }

        p->depth++;
        int32_t sub = parse_alt(p, *flags_io);
        p->depth--;
        if (p->error) return -1;
        if (p->pos >= p->len || p->pat[p->pos] != ')') {
            set_error(p, "unterminated group");
            return -1;
        }
        p->pos++;
        return sub;
    }

    if (c == '[') {
        p->pos++;
        return parse_char_class(p, *flags_io);
    }

    if (c == '.') {
        p->pos++;
        return new_node(p, AST_ANY, *flags_io);
    }
    if (c == '^') {
        p->pos++;
        return new_node(p, AST_BOL, *flags_io);
    }
    if (c == '$') {
        p->pos++;
        return new_node(p, AST_EOL, *flags_io);
    }

    if (c == '\\') {
        p->pos++;
        if (p->pos >= p->len) {
            set_error(p, "trailing backslash");
            return -1;
        }
        char esc = p->pat[p->pos];
        if (esc == 'b') {
            p->pos++;
            return new_node(p, AST_WB, *flags_io);
        }
        if (esc == 'B') {
            p->pos++;
            return new_node(p, AST_NWB, *flags_io);
        }
        if (strchr("dDwWsS", esc) != NULL) {
            p->pos++;
            char_class_t cc;
            memset(&cc, 0, sizeof(cc));
            cc.icase = (*flags_io & TAM_REGEX_ICASE) != 0;
            if (!cc_add_shorthand(&cc, esc)) {
                free(cc.ranges);
                set_error(p, "out of memory");
                return -1;
            }
            int32_t n = new_node(p, AST_CLASS, *flags_io);
            if (n < 0) {
                free(cc.ranges);
                return -1;
            }
            p->nodes[n].class_idx = new_class(p, cc);
            return n;
        }
        if (esc == 'p') {
            p->pos++;
            char_class_t cc;
            memset(&cc, 0, sizeof(cc));
            if (!parse_unicode_prop(p, &cc)) {
                free(cc.ranges);
                return -1;
            }
            int32_t n = new_node(p, AST_CLASS, *flags_io);
            if (n < 0) {
                free(cc.ranges);
                return -1;
            }
            p->nodes[n].class_idx = new_class(p, cc);
            return n;
        }
        uint32_t cp = 0;
        if (!parse_escape_char(p, &cp)) return -1;
        int32_t n = new_node(p, AST_CHAR, *flags_io);
        if (n >= 0) p->nodes[n].cp = cp;
        return n;
    }

    if (c == '*' || c == '+' || c == '?') {
        set_error(p, "unexpected quantifier");
        return -1;
    }

    uint32_t cp = 0;
    p->pos += utf8_next(p->pat + p->pos, p->len - p->pos, &cp);
    int32_t n = new_node(p, AST_CHAR, *flags_io);
    if (n >= 0) p->nodes[n].cp = cp;
    return n;
}

static bool try_parse_brace_quant(parser_t *p, int *min_out, int *max_out)
{
    size_t save = p->pos;
    if (p->pos >= p->len || p->pat[p->pos] != '{') return false;
    p->pos++;

    if (p->pos >= p->len || !isdigit((unsigned char)p->pat[p->pos])) {
        p->pos = save;
        return false;
    }
    int min_v = 0;
    while (p->pos < p->len && isdigit((unsigned char)p->pat[p->pos])) {
        min_v = min_v * 10 + (p->pat[p->pos++] - '0');
        if (min_v > MAX_REPEAT_BOUND) {
            set_error(p, "repetition count too large");
            return false;
        }
    }

    int max_v = min_v;
    if (p->pos < p->len && p->pat[p->pos] == ',') {
        p->pos++;
        if (p->pos < p->len && p->pat[p->pos] == '}') {
            max_v = -1;
        } else if (p->pos < p->len && isdigit((unsigned char)p->pat[p->pos])) {
            max_v = 0;
            while (p->pos < p->len && isdigit((unsigned char)p->pat[p->pos])) {
                max_v = max_v * 10 + (p->pat[p->pos++] - '0');
                if (max_v > MAX_REPEAT_BOUND) {
                    set_error(p, "repetition count too large");
                    return false;
                }
            }
            if (max_v < min_v) {
                set_error(p, "invalid repetition range");
                return false;
            }
        } else {
            p->pos = save;
            return false;
        }
    }

    if (p->pos >= p->len || p->pat[p->pos] != '}') {
        p->pos = save;
        return false;
    }
    p->pos++;
    *min_out = min_v;
    *max_out = max_v;
    return true;
}

static int32_t parse_quant(parser_t *p, int *flags_io)
{
    bool was_dir = false;
    int32_t atom = parse_atom(p, flags_io, &was_dir);
    if (atom < 0 || p->error) return atom;
    if (was_dir) return atom;

    while (p->pos < p->len) {
        char c = p->pat[p->pos];
        int min_r = 0, max_r = 0;
        if (c == '*') {
            min_r = 0; max_r = -1; p->pos++;
        } else if (c == '+') {
            min_r = 1; max_r = -1; p->pos++;
        } else if (c == '?') {
            min_r = 0; max_r = 1; p->pos++;
        } else if (c == '{') {
            if (!try_parse_brace_quant(p, &min_r, &max_r)) {
                if (p->error) return -1;
                break;
            }
        } else {
            break;
        }

        bool lazy = (*flags_io & TAM_REGEX_UNGREEDY) != 0;
        if (p->pos < p->len && p->pat[p->pos] == '?') {
            lazy = !lazy;
            p->pos++;
        }

        int32_t rep = new_node(p, AST_REPEAT, *flags_io);
        if (rep < 0) return -1;
        p->nodes[rep].left = atom;
        p->nodes[rep].min_rep = min_r;
        p->nodes[rep].max_rep = max_r;
        p->nodes[rep].lazy = lazy;
        atom = rep;
    }
    return atom;
}

static int32_t parse_concat(parser_t *p, int *flags_io)
{
    int32_t head = -1;
    while (p->pos < p->len && !p->error) {
        char c = p->pat[p->pos];
        if (c == '|' || c == ')') break;

        int32_t item = parse_quant(p, flags_io);
        if (item < 0) return -1;
        if (p->nodes[item].kind == AST_EMPTY && head >= 0) continue;

        if (head < 0) {
            head = item;
        } else {
            int32_t cat = new_node(p, AST_CONCAT, *flags_io);
            if (cat < 0) return -1;
            p->nodes[cat].left = head;
            p->nodes[cat].right = item;
            head = cat;
        }
    }
    if (head < 0) return new_node(p, AST_EMPTY, *flags_io);
    return head;
}

static int32_t parse_alt(parser_t *p, int flags)
{
    int32_t left = parse_concat(p, &flags);
    if (left < 0 || p->error) return -1;

    while (p->pos < p->len && p->pat[p->pos] == '|') {
        p->pos++;
        int32_t right = parse_concat(p, &flags);
        if (right < 0 || p->error) return -1;
        int32_t alt = new_node(p, AST_ALT, flags);
        if (alt < 0) return -1;
        p->nodes[alt].left = left;
        p->nodes[alt].right = right;
        left = alt;
    }
    return left;
}

/* ========================================================================= */
/* AST to Bytecode Compiler                                                  */
/* ========================================================================= */

static uint32_t emit(tam_regex_t *re, opcode_t op, uint8_t flags,
                     uint32_t arg, uint32_t arg2, bool *oom)
{
    if (*oom) return 0;
    if (re->prog_len >= MAX_PROG_INSNS) {
        *oom = true;
        return 0;
    }
    if (re->prog_len == re->prog_cap) {
        size_t ncap = re->prog_cap ? re->prog_cap * 2 : 64;
        insn_t *np = realloc(re->prog, ncap * sizeof(*np));
        if (np == NULL) {
            *oom = true;
            return 0;
        }
        re->prog = np;
        re->prog_cap = ncap;
    }
    uint32_t pc = (uint32_t)re->prog_len++;
    re->prog[pc].op = op;
    re->prog[pc].flags = flags;
    re->prog[pc].arg = arg;
    re->prog[pc].arg2 = arg2;
    return pc;
}

static void compile_node(tam_regex_t *re, const ast_node_t *nodes, int32_t idx, bool *oom)
{
    if (*oom || idx < 0) return;
    const ast_node_t *n = &nodes[idx];
    uint8_t fl = (uint8_t)n->flags;

    switch (n->kind) {
    case AST_EMPTY:
        break;
    case AST_CHAR:
        emit(re, OP_CHAR, fl, n->cp, 0, oom);
        break;
    case AST_ANY:
        emit(re, OP_ANY, fl, 0, 0, oom);
        break;
    case AST_CLASS:
        emit(re, OP_CLASS, fl, n->class_idx, 0, oom);
        break;
    case AST_BOL:
        emit(re, OP_BOL, fl, 0, 0, oom);
        break;
    case AST_EOL:
        emit(re, OP_EOL, fl, 0, 0, oom);
        break;
    case AST_WB:
        emit(re, OP_WB, fl, 0, 0, oom);
        break;
    case AST_NWB:
        emit(re, OP_NWB, fl, 0, 0, oom);
        break;
    case AST_CONCAT:
        compile_node(re, nodes, n->left, oom);
        compile_node(re, nodes, n->right, oom);
        break;
    case AST_ALT: {
        uint32_t split_pc = emit(re, OP_SPLIT, 0, 0, 0, oom);
        if (*oom) return;
        uint32_t l_start = (uint32_t)re->prog_len;
        compile_node(re, nodes, n->left, oom);
        uint32_t jmp_pc = emit(re, OP_JMP, 0, 0, 0, oom);
        if (*oom) return;
        uint32_t r_start = (uint32_t)re->prog_len;
        compile_node(re, nodes, n->right, oom);
        uint32_t after = (uint32_t)re->prog_len;
        re->prog[split_pc].arg = l_start;
        re->prog[split_pc].arg2 = r_start;
        re->prog[jmp_pc].arg = after;
        break;
    }
    case AST_REPEAT: {
        /* Mandatory min_rep copies */
        for (int i = 0; i < n->min_rep; i++) {
            compile_node(re, nodes, n->left, oom);
            if (*oom) return;
        }
        if (n->max_rep < 0) {
            /* Unbounded tail: e* or e*? */
            uint32_t split_pc = emit(re, OP_SPLIT, 0, 0, 0, oom);
            if (*oom) return;
            uint32_t body_pc = (uint32_t)re->prog_len;
            compile_node(re, nodes, n->left, oom);
            emit(re, OP_JMP, 0, split_pc, 0, oom);
            if (*oom) return;
            uint32_t after = (uint32_t)re->prog_len;
            if (n->lazy) {
                re->prog[split_pc].arg = after;
                re->prog[split_pc].arg2 = body_pc;
            } else {
                re->prog[split_pc].arg = body_pc;
                re->prog[split_pc].arg2 = after;
            }
        } else {
            /* Optional (max_rep - min_rep) copies */
            int opt = n->max_rep - n->min_rep;
            for (int i = 0; i < opt; i++) {
                uint32_t split_pc = emit(re, OP_SPLIT, 0, 0, 0, oom);
                if (*oom) return;
                uint32_t body_pc = (uint32_t)re->prog_len;
                compile_node(re, nodes, n->left, oom);
                if (*oom) return;
                uint32_t after = (uint32_t)re->prog_len;
                if (n->lazy) {
                    re->prog[split_pc].arg = after;
                    re->prog[split_pc].arg2 = body_pc;
                } else {
                    re->prog[split_pc].arg = body_pc;
                    re->prog[split_pc].arg2 = after;
                }
            }
        }
        break;
    }
    }
}

tam_regex_t *tam_regex_compile(const char *pattern, int flags,
                               char *errbuf, size_t errlen)
{
    if (pattern == NULL) {
        if (errbuf && errlen > 0) snprintf(errbuf, errlen, "NULL pattern");
        return NULL;
    }

    parser_t p;
    memset(&p, 0, sizeof(p));
    p.pat = pattern;
    p.len = strlen(pattern);
    p.errbuf = errbuf;
    p.errlen = errlen;

    int32_t root = parse_alt(&p, flags);
    if (!p.error && p.pos < p.len) {
        set_error(&p, "unexpected ')'");
    }

    if (p.error || root < 0) {
        free(p.nodes);
        for (size_t i = 0; i < p.class_count; i++) free(p.classes[i].ranges);
        free(p.classes);
        return NULL;
    }

    tam_regex_t *re = calloc(1, sizeof(*re));
    if (re == NULL) {
        free(p.nodes);
        for (size_t i = 0; i < p.class_count; i++) free(p.classes[i].ranges);
        free(p.classes);
        if (errbuf && errlen > 0) snprintf(errbuf, errlen, "out of memory");
        return NULL;
    }

    re->classes = p.classes;
    re->class_count = p.class_count;
    re->class_cap = p.class_cap;

    bool oom = false;
    compile_node(re, p.nodes, root, &oom);
    emit(re, OP_MATCH, 0, 0, 0, &oom);
    free(p.nodes);

    if (oom) {
        if (errbuf && errlen > 0) snprintf(errbuf, errlen, "pattern too complex");
        tam_regex_free(re);
        return NULL;
    }
    return re;
}

void tam_regex_free(tam_regex_t *re)
{
    if (re == NULL) return;
    free(re->prog);
    for (size_t i = 0; i < re->class_count; i++) {
        free(re->classes[i].ranges);
    }
    free(re->classes);
    free(re);
}

/* ========================================================================= */
/* Pike-VM Execution                                                         */
/* ========================================================================= */

typedef struct {
    uint32_t pc;
    size_t   start;
} vm_thread_t;

typedef struct {
    uint32_t pc;
    size_t   start;
} eps_frame_t;

#define STACK_SCRATCH_CAP 128

static bool eval_assertion(const insn_t *ins, size_t pos, size_t len,
                           uint32_t prev_cp, uint32_t next_cp, bool at_end)
{
    switch (ins->op) {
    case OP_BOL:
        if (pos == 0) return true;
        return (ins->flags & TAM_REGEX_MULTILINE) && prev_cp == '\n';
    case OP_EOL:
        if (pos == len) return true;
        return (ins->flags & TAM_REGEX_MULTILINE) && !at_end && next_cp == '\n';
    case OP_WB: {
        bool prev_w = (pos > 0) && is_word_char(prev_cp);
        bool next_w = !at_end && is_word_char(next_cp);
        return prev_w != next_w;
    }
    case OP_NWB: {
        bool prev_w = (pos > 0) && is_word_char(prev_cp);
        bool next_w = !at_end && is_word_char(next_cp);
        return prev_w == next_w;
    }
    default:
        return false;
    }
}

static void add_thread(const tam_regex_t *re, vm_thread_t *list, size_t *list_len,
                       uint32_t *gen, uint32_t cur_gen,
                       uint32_t start_pc, size_t start_off,
                       size_t pos, size_t len, uint32_t prev_cp,
                       uint32_t next_cp, bool at_end,
                       eps_frame_t *stk, size_t stk_cap)
{
    size_t sp = 0;
    stk[sp].pc = start_pc;
    stk[sp].start = start_off;
    sp++;

    while (sp > 0) {
        eps_frame_t f = stk[--sp];
        uint32_t pc = f.pc;
        if (pc >= re->prog_len) continue;
        if (gen[pc] == cur_gen) continue;
        gen[pc] = cur_gen;

        const insn_t *ins = &re->prog[pc];
        switch (ins->op) {
        case OP_JMP:
            if (sp < stk_cap) {
                stk[sp].pc = ins->arg;
                stk[sp].start = f.start;
                sp++;
            }
            break;
        case OP_SPLIT:
            /* Push arg2 first so arg1 pops first (higher priority) */
            if (sp < stk_cap) {
                stk[sp].pc = ins->arg2;
                stk[sp].start = f.start;
                sp++;
            }
            if (sp < stk_cap) {
                stk[sp].pc = ins->arg;
                stk[sp].start = f.start;
                sp++;
            }
            break;
        case OP_BOL:
        case OP_EOL:
        case OP_WB:
        case OP_NWB:
            if (eval_assertion(ins, pos, len, prev_cp, next_cp, at_end)) {
                if (sp < stk_cap) {
                    stk[sp].pc = pc + 1;
                    stk[sp].start = f.start;
                    sp++;
                }
            }
            break;
        default:
            list[*list_len].pc = pc;
            list[*list_len].start = f.start;
            (*list_len)++;
            break;
        }
    }
}

bool tam_regex_find(const tam_regex_t *re, const char *text, size_t len,
                    size_t start_byte, tam_regex_match_t *out)
{
    if (re == NULL || re->prog_len == 0) return false;
    if (text == NULL) text = "";
    if (start_byte > len) return false;

    size_t n = re->prog_len;

    vm_thread_t  s_clist[STACK_SCRATCH_CAP];
    vm_thread_t  s_nlist[STACK_SCRATCH_CAP];
    uint32_t     s_gen[STACK_SCRATCH_CAP];
    eps_frame_t  s_stk[STACK_SCRATCH_CAP * 2];

    vm_thread_t *clist = s_clist;
    vm_thread_t *nlist = s_nlist;
    uint32_t    *gen   = s_gen;
    eps_frame_t *stk   = s_stk;
    size_t       stk_cap = STACK_SCRATCH_CAP * 2;
    bool         heap_used = false;

    if (n > STACK_SCRATCH_CAP) {
        clist = malloc(n * sizeof(*clist));
        nlist = malloc(n * sizeof(*nlist));
        gen   = malloc(n * sizeof(*gen));
        stk_cap = n * 2;
        stk   = malloc(stk_cap * sizeof(*stk));
        heap_used = true;
        if (!clist || !nlist || !gen || !stk) {
            free(clist);
            free(nlist);
            free(gen);
            free(stk);
            return false;
        }
    }
    memset(gen, 0, n * sizeof(*gen));

    size_t clist_len = 0;
    size_t nlist_len = 0;
    uint32_t cur_gen = 1;

    bool has_best = false;
    tam_regex_match_t best = {0, 0};

    size_t pos = start_byte;
    uint32_t prev_cp = utf8_prev(text, start_byte);

    for (;;) {
        bool at_end = (pos >= len);
        uint32_t next_cp = 0;
        size_t step_bytes = 0;
        if (!at_end) {
            step_bytes = utf8_next(text + pos, len - pos, &next_cp);
        }

        if (!has_best) {
            add_thread(re, clist, &clist_len, gen, cur_gen,
                       0, pos, pos, len, prev_cp, next_cp, at_end, stk, stk_cap);
        }

        nlist_len = 0;
        cur_gen++;
        if (cur_gen == 0) {
            memset(gen, 0, n * sizeof(*gen));
            cur_gen = 1;
        }

        /* Precompute post-step context for epsilon closure into nlist */
        size_t next_pos = pos + step_bytes;
        bool next_at_end = (next_pos >= len);
        uint32_t after_cp = 0;
        if (!next_at_end) {
            utf8_next(text + next_pos, len - next_pos, &after_cp);
        }

        for (size_t i = 0; i < clist_len; i++) {
            vm_thread_t t = clist[i];
            if (has_best && t.start > best.start) break;

            const insn_t *ins = &re->prog[t.pc];
            if (ins->op == OP_MATCH) {
                best.start = t.start;
                best.end   = pos;
                has_best   = true;
                break; /* lower-priority threads in clist cannot beat this */
            }

            if (at_end) continue;

            bool matched = false;
            switch (ins->op) {
            case OP_CHAR:
                if (ins->flags & TAM_REGEX_ICASE) {
                    matched = fold_ascii_latin(next_cp) == fold_ascii_latin(ins->arg);
                } else {
                    matched = (next_cp == ins->arg);
                }
                break;
            case OP_ANY:
                matched = (ins->flags & TAM_REGEX_DOTALL) || (next_cp != '\n');
                break;
            case OP_CLASS:
                matched = cc_match(&re->classes[ins->arg], next_cp);
                break;
            default:
                break;
            }

            if (matched) {
                add_thread(re, nlist, &nlist_len, gen, cur_gen,
                           t.pc + 1, t.start, next_pos, len, next_cp,
                           after_cp, next_at_end, stk, stk_cap);
            }
        }

        vm_thread_t *tmp = clist;
        clist = nlist;
        nlist = tmp;
        clist_len = nlist_len;

        if (at_end || (has_best && clist_len == 0)) break;

        prev_cp = next_cp;
        pos = next_pos;
    }

    if (heap_used) {
        free(clist);
        free(nlist);
        free(gen);
        free(stk);
    }

    if (has_best && out != NULL) {
        *out = best;
    }
    return has_best;
}

bool tam_regex_match(const tam_regex_t *re, const char *text, size_t len)
{
    return tam_regex_find(re, text, len, 0, NULL);
}

size_t tam_regex_find_all(const tam_regex_t *re, const char *text, size_t len,
                          tam_regex_match_t *out, size_t max_out)
{
    if (re == NULL) return 0;
    if (text == NULL) text = "";

    size_t count = 0;
    size_t pos = 0;
    size_t last_nonempty_end = (size_t)-1;

    while (pos <= len && count < max_out) {
        tam_regex_match_t m;
        if (!tam_regex_find(re, text, len, pos, &m)) break;

        /* Avoid duplicate empty match immediately at end of previous non-empty match */
        if (m.start == m.end && m.start == last_nonempty_end) {
            if (pos >= len) break;
            uint32_t dummy = 0;
            pos += utf8_next(text + pos, len - pos, &dummy);
            continue;
        }

        if (out != NULL) out[count] = m;
        count++;

        if (m.end > m.start) {
            pos = m.end;
            last_nonempty_end = m.end;
        } else {
            if (m.end >= len) break;
            uint32_t dummy = 0;
            pos = m.end + utf8_next(text + m.end, len - m.end, &dummy);
        }
    }

    return count;
}
