#include "search.h"

#include "buffer.h"
#include "cursor.h"
#include "undo.h"

#include <tam.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t byte_to_rune(const char *s, size_t byte_off)
{
    return edit_line_runes(s, byte_off);
}

static tam_str_t escape_regex_literal(const char *pat)
{
    tam_str_t out = tam_str_new("");
    if (!out) return NULL;
    for (const char *p = pat; *p; p++) {
        if (strchr("\\.+*?()|[]{}^$", *p) != NULL) {
            char esc[3] = {'\\', *p, '\0'};
            out = tam_str_cat(out, esc);
        } else {
            out = tam_str_cat_len(out, p, 1);
        }
        if (!out) return NULL;
    }
    return out;
}

void search_invalidate(editor_t *ed)
{
    if (!ed) return;
    if (ed->search.compiled) {
        tam_regex_free(ed->search.compiled);
        ed->search.compiled = NULL;
    }
    ed->search.compiled_query[0] = '\0';
}

bool search_compile(editor_t *ed, const char *query, bool literal,
                    char *err_buf, size_t err_size)
{
    if (err_buf && err_size > 0) err_buf[0] = '\0';
    if (!ed || !query || !query[0]) {
        search_invalidate(ed);
        return false;
    }

    bool icase = ed->config.ignorecase;
    if (ed->search.compiled != NULL &&
        strcmp(ed->search.compiled_query, query) == 0 &&
        ed->search.compiled_literal == literal &&
        ed->search.compiled_ignorecase == icase) {
        return true;
    }

    search_invalidate(ed);

    int flags = icase ? TAM_REGEX_ICASE : 0;
    tam_regex_t *re = NULL;
    if (literal) {
        tam_str_t esc = escape_regex_literal(query);
        if (!esc) {
            if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Out of memory");
            return false;
        }
        re = tam_regex_compile(esc, flags, err_buf, err_size);
        tam_str_free(esc);
    } else {
        re = tam_regex_compile(query, flags, err_buf, err_size);
    }

    if (!re) return false;

    ed->search.compiled = re;
    snprintf(ed->search.compiled_query, sizeof(ed->search.compiled_query), "%s", query);
    ed->search.compiled_literal = literal;
    ed->search.compiled_ignorecase = icase;
    ed->search.ignorecase = icase;
    return true;
}

static bool search_ensure(editor_t *ed, char *err_buf, size_t err_size)
{
    if (!ed || !ed->search.query[0]) return false;
    return search_compile(ed, ed->search.query, ed->search.literal, err_buf, err_size);
}

static bool find_on_line_forward(const tam_regex_t *re, const char *line, size_t len,
                                 size_t from_col, size_t *out_start_col, size_t *out_end_col)
{
    size_t from_byte = edit_rune_to_byte(line, len, from_col);
    if (from_byte > len) return false;

    tam_regex_match_t m;
    if (!tam_regex_find(re, line, len, from_byte, &m)) return false;

    *out_start_col = byte_to_rune(line, m.start);
    *out_end_col = byte_to_rune(line, m.end);
    return true;
}

static bool find_on_line_backward(const tam_regex_t *re, const char *line, size_t len,
                                  size_t before_col, size_t *out_start_col, size_t *out_end_col)
{
    size_t limit_byte = edit_rune_to_byte(line, len, before_col);
    if (limit_byte == 0) return false;

    bool found = false;
    tam_regex_match_t best = {0, 0};
    size_t pos = 0;

    while (pos <= limit_byte && pos <= len) {
        tam_regex_match_t m;
        if (!tam_regex_find(re, line, len, pos, &m)) break;
        if (m.start >= limit_byte) break;
        if (m.end <= limit_byte) {
            best = m;
            found = true;
        } else {
            break;
        }
        pos = (m.end > pos) ? m.end : (pos + 1);
    }

    if (!found) return false;
    *out_start_col = byte_to_rune(line, best.start);
    *out_end_col = byte_to_rune(line, best.end);
    return true;
}

static void apply_match_cursor(editor_t *ed, size_t line, size_t start_col, size_t end_col,
                               bool select_match)
{
    ed->cx_line = line;
    ed->cx_col = start_col;
    ed->goal_col = start_col;
    ed->last_match_len = end_col >= start_col ? (end_col - start_col) : 0;
    if (select_match && end_col > start_col) {
        ed->sel.active = true;
        ed->sel.anchor = (loc_t){line, end_col};
        ed->sel.cursor = (loc_t){line, start_col};
    }
    cursor_clamp(ed);
    ed->search.last_line = ed->cx_line;
    ed->search.last_col = ed->cx_col;
    ed->search.has_match = true;
}

static bool find_next_from(editor_t *ed, size_t start_line, size_t start_col,
                           bool select_match, bool *wrapped_out)
{
    if (wrapped_out) *wrapped_out = false;
    if (!ed || ed->doc.count == 0) return false;

    char err[128];
    if (!search_ensure(ed, err, sizeof(err))) {
        if (ed->search.query[0] && err[0]) {
            editor_set_message(ed, "Invalid regex: %s", err);
        }
        return false;
    }

    const tam_regex_t *re = ed->search.compiled;
    size_t sc = 0, ec = 0;

    /* 1. Remaining part of start_line */
    if (start_line < ed->doc.count &&
        find_on_line_forward(re, ed->doc.lines[start_line], ed->doc.lens[start_line],
                             start_col, &sc, &ec)) {
        apply_match_cursor(ed, start_line, sc, ec, select_match);
        return true;
    }

    /* 2. Subsequent lines */
    for (size_t l = start_line + 1; l < ed->doc.count; l++) {
        if (find_on_line_forward(re, ed->doc.lines[l], ed->doc.lens[l], 0, &sc, &ec)) {
            apply_match_cursor(ed, l, sc, ec, select_match);
            return true;
        }
    }

    /* 3. Wrap from line 0 up to start_line:start_col */
    for (size_t l = 0; l <= start_line && l < ed->doc.count; l++) {
        if (find_on_line_forward(re, ed->doc.lines[l], ed->doc.lens[l], 0, &sc, &ec)) {
            if (l < start_line || sc < start_col) {
                apply_match_cursor(ed, l, sc, ec, select_match);
                if (wrapped_out) *wrapped_out = true;
                return true;
            }
        }
    }

    return false;
}

void search_find_next(editor_t *ed)
{
    if (!ed || !ed->search.query[0] || ed->doc.count == 0) return;

    size_t start_col = ed->cx_col + 1;
    bool wrapped = false;
    if (find_next_from(ed, ed->cx_line, start_col, false, &wrapped)) {
        editor_set_message(ed, wrapped ? "Match found (wrapped)" : "Match found");
    } else {
        editor_set_message(ed, "Pattern not found: %s", ed->search.query);
    }
}

void search_find_prev(editor_t *ed)
{
    if (!ed || !ed->search.query[0] || ed->doc.count == 0) return;

    char err[128];
    if (!search_ensure(ed, err, sizeof(err))) {
        if (err[0]) editor_set_message(ed, "Invalid regex: %s", err);
        return;
    }

    const tam_regex_t *re = ed->search.compiled;
    size_t cur_line = ed->cx_line;
    size_t cur_col = ed->cx_col;
    size_t sc = 0, ec = 0;

    /* 1. Earlier on current line */
    if (cur_col > 0 &&
        find_on_line_backward(re, ed->doc.lines[cur_line], ed->doc.lens[cur_line],
                              cur_col, &sc, &ec)) {
        apply_match_cursor(ed, cur_line, sc, ec, false);
        editor_set_message(ed, "Match found");
        return;
    }

    /* 2. Previous lines */
    for (size_t l = cur_line; l > 0; l--) {
        size_t idx = l - 1;
        size_t lrunes = edit_line_runes(ed->doc.lines[idx], ed->doc.lens[idx]);
        if (lrunes > 0 && find_on_line_backward(re, ed->doc.lines[idx], ed->doc.lens[idx],
                                                lrunes, &sc, &ec)) {
            apply_match_cursor(ed, idx, sc, ec, false);
            editor_set_message(ed, "Match found");
            return;
        }
    }

    /* 3. Wrap from bottom */
    for (size_t l = ed->doc.count; l > cur_line; l--) {
        size_t idx = l - 1;
        size_t lrunes = edit_line_runes(ed->doc.lines[idx], ed->doc.lens[idx]);
        if (lrunes > 0 && find_on_line_backward(re, ed->doc.lines[idx], ed->doc.lens[idx],
                                                lrunes, &sc, &ec)) {
            if (idx > cur_line || sc > cur_col) {
                apply_match_cursor(ed, idx, sc, ec, false);
                editor_set_message(ed, "Match found (wrapped)");
                return;
            }
        }
    }

    editor_set_message(ed, "Pattern not found: %s", ed->search.query);
}

void search_inc_begin(editor_t *ed)
{
    if (!ed) return;
    ed->search.inc_saved = true;
    ed->search.inc_cx_line = ed->cx_line;
    ed->search.inc_cx_col = ed->cx_col;
    ed->search.inc_top_line = ed->top_line;
    ed->search.inc_left_col = ed->left_col;
    ed->search.inc_sel = ed->sel;
}

static void inc_restore_snapshot(editor_t *ed)
{
    if (!ed || !ed->search.inc_saved) return;
    ed->cx_line = ed->search.inc_cx_line;
    ed->cx_col = ed->search.inc_cx_col;
    ed->goal_col = ed->cx_col;
    ed->top_line = ed->search.inc_top_line;
    ed->left_col = ed->search.inc_left_col;
    ed->sel = ed->search.inc_sel;
    cursor_clamp(ed);
}

void search_inc_update(editor_t *ed, bool forward)
{
    if (!ed || !ed->config.incsearch) return;
    (void)forward;
    if (!ed->search.inc_saved) search_inc_begin(ed);

    inc_restore_snapshot(ed);
    if (!ed->prompt.buf[0]) {
        ed->message[0] = '\0';
        return;
    }

    snprintf(ed->search.query, sizeof(ed->search.query), "%.255s", ed->prompt.buf);
    bool wrapped = false;
    if (find_next_from(ed, ed->search.inc_cx_line, ed->search.inc_cx_col,
                       true, &wrapped)) {
        ed->message[0] = '\0';
    }
}

void search_inc_commit(editor_t *ed)
{
    if (!ed) return;
    ed->search.inc_saved = false;
}

void search_inc_cancel(editor_t *ed)
{
    if (!ed) return;
    if (ed->search.inc_saved) {
        inc_restore_snapshot(ed);
        ed->search.inc_saved = false;
    }
}

size_t search_line_matches(editor_t *ed, size_t line,
                           tam_regex_match_t *out, size_t max_out)
{
    if (!ed || !out || max_out == 0 || !ed->config.hlsearch || !ed->search.query[0]) {
        return 0;
    }
    if (line >= ed->doc.count) return 0;

    char err[128];
    if (!search_ensure(ed, err, sizeof(err))) return 0;

    const char *text = ed->doc.lines[line];
    size_t len = ed->doc.lens[line];
    size_t count = 0;
    size_t pos = 0;

    while (pos <= len && count < max_out) {
        tam_regex_match_t m;
        if (!tam_regex_find(ed->search.compiled, text, len, pos, &m)) break;
        if (m.end > m.start) {
            out[count++] = m;
        }
        pos = (m.end > pos) ? m.end : (pos + 1);
    }
    return count;
}

char *search_expand_template(const tam_regex_t *re, const char *text,
                             const tam_regex_captures_t *caps,
                             const char *tmpl, size_t *out_len)
{
    tam_str_t out = tam_str_new("");
    if (!out) return NULL;
    if (!tmpl) tmpl = "";

    for (const char *p = tmpl; *p; p++) {
        if (*p != '$') {
            out = tam_str_cat_len(out, p, 1);
            if (!out) return NULL;
            continue;
        }
        if (p[1] == '$') {
            out = tam_str_cat_len(out, "$", 1);
            if (!out) return NULL;
            p++;
            continue;
        }

        int grp_idx = -1;
        if (p[1] == '{') {
            const char *close = strchr(p + 2, '}');
            if (close) {
                char key[64];
                size_t klen = (size_t)(close - (p + 2));
                if (klen > 0 && klen < sizeof(key)) {
                    memcpy(key, p + 2, klen);
                    key[klen] = '\0';
                    bool all_digits = true;
                    for (size_t i = 0; i < klen; i++) {
                        if (!isdigit((unsigned char)key[i])) {
                            all_digits = false;
                            break;
                        }
                    }
                    if (all_digits) {
                        grp_idx = atoi(key);
                    } else if (re) {
                        grp_idx = tam_regex_group_index(re, key);
                    }
                }
                p = close;
                if (grp_idx >= 0 && caps && (size_t)grp_idx < TAM_REGEX_MAX_CAPTURES &&
                    caps->valid[grp_idx]) {
                    tam_regex_match_t g = caps->groups[grp_idx];
                    if (g.end >= g.start) {
                        out = tam_str_cat_len(out, text + g.start, g.end - g.start);
                    }
                }
                if (!out) return NULL;
                continue;
            }
        } else if (isdigit((unsigned char)p[1])) {
            int val = 0;
            const char *q = p + 1;
            while (isdigit((unsigned char)*q)) {
                val = val * 10 + (*q - '0');
                q++;
            }
            p = q - 1;
            grp_idx = val;
            if (grp_idx >= 0 && caps && (size_t)grp_idx < TAM_REGEX_MAX_CAPTURES &&
                caps->valid[grp_idx]) {
                tam_regex_match_t g = caps->groups[grp_idx];
                if (g.end >= g.start) {
                    out = tam_str_cat_len(out, text + g.start, g.end - g.start);
                }
            }
            if (!out) return NULL;
            continue;
        } else if (isalpha((unsigned char)p[1]) || p[1] == '_') {
            char key[64];
            size_t klen = 0;
            const char *q = p + 1;
            while ((isalnum((unsigned char)*q) || *q == '_') && klen + 1 < sizeof(key)) {
                key[klen++] = *q++;
            }
            key[klen] = '\0';
            p = q - 1;
            if (re) grp_idx = tam_regex_group_index(re, key);
            if (grp_idx >= 0 && caps && (size_t)grp_idx < TAM_REGEX_MAX_CAPTURES &&
                caps->valid[grp_idx]) {
                tam_regex_match_t g = caps->groups[grp_idx];
                if (g.end >= g.start) {
                    out = tam_str_cat_len(out, text + g.start, g.end - g.start);
                }
            }
            if (!out) return NULL;
            continue;
        }

        out = tam_str_cat_len(out, "$", 1);
        if (!out) return NULL;
    }

    size_t len = tam_str_len(out);
    char *res = malloc(len + 1);
    if (res) {
        memcpy(res, out, len + 1);
        if (out_len) *out_len = len;
    }
    tam_str_free(out);
    return res;
}

static void apply_line_span_replace(editor_t *ed, size_t line,
                                    size_t byte_start, size_t byte_end,
                                    const char *rep, size_t rep_len)
{
    const char *cur_line = ed->doc.lines[line];
    size_t start_col = byte_to_rune(cur_line, byte_start);
    size_t match_bytes = byte_end - byte_start;

    if (match_bytes > 0) {
        undo_record_remove(ed, line, start_col, cur_line + byte_start, match_bytes);
        editor_raw_remove_str(ed, line, start_col, cur_line + byte_start, match_bytes);
    }
    if (rep_len > 0) {
        undo_record_insert(ed, line, start_col, rep, rep_len);
        editor_raw_insert(ed, line, start_col, rep, rep_len);
    }
}

typedef struct {
    tam_regex_captures_t caps;
} line_cap_match_t;

long search_replace_all(editor_t *ed, const char *pattern, const char *replacement,
                        bool in_selection, bool literal,
                        char *err_buf, size_t err_size)
{
    if (!ed || !pattern || !pattern[0]) return 0;

    if (search_compile(ed, pattern, literal, err_buf, err_size)) {
        snprintf(ed->search.query, sizeof(ed->search.query), "%s", pattern);
        ed->search.literal = literal;
    } else {
        return -1;
    }

    size_t min_line = 0, max_line = ed->doc.count ? (ed->doc.count - 1) : 0;
    loc_t sel_start = {0, 0}, sel_end = {0, 0};
    bool has_sel = false;
    if (in_selection && ed->sel.active) {
        selection_get_range(ed, &sel_start, &sel_end);
        min_line = sel_start.line;
        max_line = sel_end.line;
        has_sel = true;
    }

    long total = 0;
    undo_begin_group(ed);

    for (size_t line = max_line + 1; line > min_line; line--) {
        size_t l = line - 1;
        const char *text = ed->doc.lines[l];
        size_t len = ed->doc.lens[l];

        size_t byte_lo = 0;
        size_t byte_hi = len;
        if (has_sel) {
            if (l == sel_start.line) byte_lo = edit_rune_to_byte(text, len, sel_start.col);
            if (l == sel_end.line)   byte_hi = edit_rune_to_byte(text, len, sel_end.col);
        }

        /* Collect all matches left-to-right within [byte_lo, byte_hi] */
        size_t cap = 16;
        size_t count = 0;
        line_cap_match_t *matches = malloc(cap * sizeof(*matches));
        if (!matches) break;

        size_t pos = byte_lo;
        while (pos <= byte_hi && pos <= len) {
            tam_regex_captures_t caps;
            if (!tam_regex_find_captures(ed->search.compiled, text, len, pos, &caps)) break;
            size_t m_start = caps.groups[0].start;
            size_t m_end = caps.groups[0].end;
            if (m_end > byte_hi || m_start >= byte_hi) {
                if (!(m_start == byte_hi && m_end == byte_hi)) break;
            }
            if (m_end == m_start && m_start == len && count > 0) break;

            if (count >= cap) {
                cap *= 2;
                line_cap_match_t *nm = realloc(matches, cap * sizeof(*matches));
                if (!nm) break;
                matches = nm;
            }
            matches[count++].caps = caps;
            pos = (m_end > pos) ? m_end : (pos + 1);
            if (m_start == byte_hi) break;
        }

        /* Apply right-to-left so earlier byte offsets stay exact */
        for (size_t idx = count; idx > 0; idx--) {
            tam_regex_captures_t *c = &matches[idx - 1].caps;
            size_t rep_len = 0;
            char *rep = NULL;
            if (literal) {
                rep_len = strlen(replacement ? replacement : "");
                rep = strdup(replacement ? replacement : "");
            } else {
                rep = search_expand_template(ed->search.compiled, ed->doc.lines[l],
                                             c, replacement, &rep_len);
            }
            if (!rep) continue;

            apply_line_span_replace(ed, l, c->groups[0].start, c->groups[0].end, rep, rep_len);
            free(rep);
            total++;
        }
        free(matches);
    }

    if (total > 0) {
        undo_clear_redo(ed);
        ed->dirty = true;
        ed->sel.active = false;
        cursor_clamp(ed);
    }
    undo_end_group(ed);
    return total;
}

static bool replace_advance_to_next(editor_t *ed, size_t from_line, size_t from_byte)
{
    size_t max_line = ed->doc.count ? ed->doc.count - 1 : 0;
    if (ed->search.replace_in_selection && ed->search.rep_end_line <= max_line) {
        max_line = ed->search.rep_end_line;
    }

    for (size_t l = from_line; l <= max_line && l < ed->doc.count; l++) {
        const char *text = ed->doc.lines[l];
        size_t len = ed->doc.lens[l];
        size_t pos = (l == from_line) ? from_byte : 0;
        size_t byte_hi = len;
        if (ed->search.replace_in_selection && l == ed->search.rep_end_line) {
            byte_hi = edit_rune_to_byte(text, len, ed->search.rep_end_col);
        }

        while (pos <= byte_hi && pos <= len) {
            tam_regex_match_t m;
            if (!tam_regex_find(ed->search.compiled, text, len, pos, &m)) break;
            if (m.end > byte_hi) break;

            size_t sc = byte_to_rune(text, m.start);
            size_t ec = byte_to_rune(text, m.end);
            ed->search.rep_cur_line = l;
            ed->search.rep_cur_col = sc;
            ed->search.rep_match_len = m.end - m.start;

            apply_match_cursor(ed, l, sc, ec, true);
            return true;
        }
    }
    return false;
}

bool search_replace_begin(editor_t *ed, const char *pattern, const char *replacement,
                          bool in_selection, char *err_buf, size_t err_size)
{
    if (!ed || !pattern || !pattern[0]) return false;

    if (!search_compile(ed, pattern, false, err_buf, err_size)) {
        return false;
    }
    snprintf(ed->search.query, sizeof(ed->search.query), "%s", pattern);
    ed->search.literal = false;
    snprintf(ed->search.replace_with, sizeof(ed->search.replace_with), "%s",
             replacement ? replacement : "");

    size_t start_line = 0, start_col = 0;
    ed->search.replace_in_selection = in_selection && ed->sel.active;
    if (ed->search.replace_in_selection) {
        loc_t s = {0, 0}, e = {0, 0};
        selection_get_range(ed, &s, &e);
        start_line = s.line;
        start_col = s.col;
        ed->search.rep_end_line = e.line;
        ed->search.rep_end_col = e.col;
    } else {
        start_line = ed->cx_line;
        start_col = ed->cx_col;
        ed->search.rep_end_line = ed->doc.count ? ed->doc.count - 1 : 0;
        ed->search.rep_end_col = ed->doc.count
                                 ? edit_line_runes(ed->doc.lines[ed->doc.count - 1],
                                                   ed->doc.lens[ed->doc.count - 1])
                                 : 0;
    }

    ed->search.rep_count = 0;
    size_t start_byte = (start_line < ed->doc.count)
                        ? edit_rune_to_byte(ed->doc.lines[start_line],
                                            ed->doc.lens[start_line], start_col)
                        : 0;

    if (!replace_advance_to_next(ed, start_line, start_byte)) {
        editor_set_message(ed, "Pattern not found: %s", pattern);
        return false;
    }

    ed->search.replace_active = true;
    undo_begin_group(ed);
    return true;
}

bool search_replace_step(editor_t *ed, char decision)
{
    if (!ed || !ed->search.replace_active) return false;

    if (decision == 'q' || decision == 'Q' || decision == 27) {
        ed->search.replace_active = false;
        undo_end_group(ed);
        ed->prompt.active = false;
        ed->prompt.kind = PROMPT_NONE;
        editor_set_message(ed, "Replaced %zu occurrence(s)", ed->search.rep_count);
        return false;
    }

    size_t l = ed->search.rep_cur_line;
    const char *text = ed->doc.lines[l];
    size_t len = ed->doc.lens[l];
    size_t m_start_byte = edit_rune_to_byte(text, len, ed->search.rep_cur_col);
    size_t next_byte = m_start_byte;

    if (decision == 'y' || decision == 'Y' || decision == 'a' || decision == 'A') {
        tam_regex_captures_t caps;
        if (tam_regex_find_captures(ed->search.compiled, text, len, m_start_byte, &caps)) {
            size_t rep_len = 0;
            char *rep = search_expand_template(ed->search.compiled, text, &caps,
                                               ed->search.replace_with, &rep_len);
            if (rep) {
                size_t old_bytes = caps.groups[0].end - caps.groups[0].start;
                if (ed->search.replace_in_selection && l == ed->search.rep_end_line) {
                    /* Keep end_col aligned in runes */
                    size_t old_runes = byte_to_rune(text + caps.groups[0].start, old_bytes);
                    size_t new_runes = byte_to_rune(rep, rep_len);
                    ed->search.rep_end_col = ed->search.rep_end_col - old_runes + new_runes;
                }
                apply_line_span_replace(ed, l, caps.groups[0].start, caps.groups[0].end,
                                        rep, rep_len);
                next_byte = caps.groups[0].start + rep_len;
                if (rep_len == 0 && caps.groups[0].end == caps.groups[0].start) {
                    next_byte++;
                }
                free(rep);
                ed->search.rep_count++;
                ed->dirty = true;
                undo_clear_redo(ed);
            }
        }
    } else if (decision == 'n' || decision == 'N') {
        next_byte = m_start_byte + (ed->search.rep_match_len > 0 ? ed->search.rep_match_len : 1);
    } else {
        return true; /* keep prompting on invalid key */
    }

    if (decision == 'a' || decision == 'A') {
        while (replace_advance_to_next(ed, ed->search.rep_cur_line, next_byte)) {
            l = ed->search.rep_cur_line;
            text = ed->doc.lines[l];
            len = ed->doc.lens[l];
            m_start_byte = edit_rune_to_byte(text, len, ed->search.rep_cur_col);
            tam_regex_captures_t caps;
            if (!tam_regex_find_captures(ed->search.compiled, text, len, m_start_byte, &caps)) {
                break;
            }
            size_t rep_len = 0;
            char *rep = search_expand_template(ed->search.compiled, text, &caps,
                                               ed->search.replace_with, &rep_len);
            if (!rep) break;
            if (ed->search.replace_in_selection && l == ed->search.rep_end_line) {
                size_t old_bytes = caps.groups[0].end - caps.groups[0].start;
                size_t old_runes = byte_to_rune(text + caps.groups[0].start, old_bytes);
                size_t new_runes = byte_to_rune(rep, rep_len);
                ed->search.rep_end_col = ed->search.rep_end_col - old_runes + new_runes;
            }
            apply_line_span_replace(ed, l, caps.groups[0].start, caps.groups[0].end,
                                    rep, rep_len);
            next_byte = caps.groups[0].start + rep_len;
            if (rep_len == 0) next_byte++;
            free(rep);
            ed->search.rep_count++;
            ed->dirty = true;
            undo_clear_redo(ed);
        }
        ed->search.replace_active = false;
        ed->sel.active = false;
        cursor_clamp(ed);
        undo_end_group(ed);
        ed->prompt.active = false;
        ed->prompt.kind = PROMPT_NONE;
        editor_set_message(ed, "Replaced %zu occurrence(s)", ed->search.rep_count);
        return false;
    }

    if (!replace_advance_to_next(ed, ed->search.rep_cur_line, next_byte)) {
        ed->search.replace_active = false;
        ed->sel.active = false;
        cursor_clamp(ed);
        undo_end_group(ed);
        ed->prompt.active = false;
        ed->prompt.kind = PROMPT_NONE;
        editor_set_message(ed, "Replaced %zu occurrence(s)", ed->search.rep_count);
        return false;
    }

    return true;
}
