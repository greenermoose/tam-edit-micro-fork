#include "prompt.h"
#include "cursor.h"

#include <tam.h>
#include <tam_uni.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void prompt_close(editor_t *ed)
{
    memset(&ed->prompt, 0, sizeof(ed->prompt));
}

void prompt_open_command(editor_t *ed)
{
    memset(&ed->prompt, 0, sizeof(ed->prompt));
    ed->prompt.kind = PROMPT_COMMAND;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "> ");
}

void prompt_open_find(editor_t *ed)
{
    memset(&ed->prompt, 0, sizeof(ed->prompt));
    ed->prompt.kind = PROMPT_FIND;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "Find: ");
}

void prompt_open_goto(editor_t *ed)
{
    memset(&ed->prompt, 0, sizeof(ed->prompt));
    ed->prompt.kind = PROMPT_GOTO;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "Goto line: ");
}

void prompt_open_save_as(editor_t *ed)
{
    memset(&ed->prompt, 0, sizeof(ed->prompt));
    ed->prompt.kind = PROMPT_SAVE_AS;
    ed->prompt.active = true;
    snprintf(ed->prompt.label, sizeof(ed->prompt.label), "Save as: ");
}

void prompt_open_quit_confirm(editor_t *ed)
{
    memset(&ed->prompt, 0, sizeof(ed->prompt));
    ed->prompt.kind = PROMPT_QUIT_CONFIRM;
    ed->prompt.active = true;
    const char *name = ed->path != NULL ? ed->path : "[No Name]";
    snprintf(ed->prompt.label, sizeof(ed->prompt.label),
             "Save changes to %s? (y/n/esc): ", name);
}

static size_t byte_to_rune(const char *line, size_t byte_offset)
{
    size_t col = 0;
    size_t i = 0;
    while (i < byte_offset) {
        uint32_t cp;
        size_t used = tam_utf8_decode(line + i, byte_offset - i, &cp);
        if (used == 0) break;
        i += used;
        col++;
    }
    return col;
}

static bool search_line_forward(const char *line, size_t len, size_t from_col,
                                const char *needle, size_t needle_len,
                                size_t *out_col)
{
    size_t from_byte = edit_rune_to_byte(line, len, from_col);
    if (from_byte >= len) return false;

    const char *start = line + from_byte;
    size_t search_len = len - from_byte;

    if (needle_len > search_len) return false;

    for (size_t i = 0; i + needle_len <= search_len; i++) {
        if (strncasecmp(start + i, needle, needle_len) == 0) {
            *out_col = byte_to_rune(line, from_byte + i);
            return true;
        }
    }
    return false;
}

static bool search_line_backward(const char *line, size_t len, size_t before_col,
                                 const char *needle, size_t needle_len,
                                 size_t *out_col)
{
    size_t max_byte = edit_rune_to_byte(line, len, before_col);
    if (needle_len > max_byte) return false;

    for (ssize_t i = (ssize_t)(max_byte - needle_len); i >= 0; i--) {
        if (strncasecmp(line + i, needle, needle_len) == 0) {
            *out_col = byte_to_rune(line, (size_t)i);
            return true;
        }
    }
    return false;
}

void search_find_next(editor_t *ed)
{
    const char *needle = ed->search.query;
    size_t nlen = strlen(needle);
    if (nlen == 0 || ed->doc.count == 0) return;

    size_t cur_line = ed->cx_line;
    size_t cur_col  = ed->cx_col + 1;

    /* 1. Search remaining part of current line */
    size_t match_col = 0;
    if (search_line_forward(ed->doc.lines[cur_line], ed->doc.lens[cur_line],
                            cur_col, needle, nlen, &match_col)) {
        ed->cx_line = cur_line;
        ed->cx_col  = match_col;
        ed->goal_col = match_col;
        cursor_clamp(ed);
        ed->search.has_match = true;
        editor_set_message(ed, "Match found");
        return;
    }

    /* 2. Search lines below current line */
    for (size_t l = cur_line + 1; l < ed->doc.count; l++) {
        if (search_line_forward(ed->doc.lines[l], ed->doc.lens[l], 0,
                                needle, nlen, &match_col)) {
            ed->cx_line = l;
            ed->cx_col  = match_col;
            ed->goal_col = match_col;
            cursor_clamp(ed);
            ed->search.has_match = true;
            editor_set_message(ed, "Match found");
            return;
        }
    }

    /* 3. Wrap around to lines from start up to current line */
    for (size_t l = 0; l <= cur_line; l++) {
        size_t limit_col = (l == cur_line) ? cur_col : (size_t)-1;
        if (search_line_forward(ed->doc.lines[l], ed->doc.lens[l], 0,
                                needle, nlen, &match_col)) {
            if (l < cur_line || match_col < limit_col) {
                ed->cx_line = l;
                ed->cx_col  = match_col;
                ed->goal_col = match_col;
                cursor_clamp(ed);
                ed->search.has_match = true;
                editor_set_message(ed, "Match found (wrapped)");
                return;
            }
        }
    }

    editor_set_message(ed, "Pattern not found: %s", needle);
}

void search_find_prev(editor_t *ed)
{
    const char *needle = ed->search.query;
    size_t nlen = strlen(needle);
    if (nlen == 0 || ed->doc.count == 0) return;

    size_t cur_line = ed->cx_line;
    size_t cur_col  = ed->cx_col;

    /* 1. Search line before current col */
    size_t match_col = 0;
    if (cur_col > 0 &&
        search_line_backward(ed->doc.lines[cur_line], ed->doc.lens[cur_line],
                             cur_col, needle, nlen, &match_col)) {
        ed->cx_line = cur_line;
        ed->cx_col  = match_col;
        ed->goal_col = match_col;
        cursor_clamp(ed);
        ed->search.has_match = true;
        editor_set_message(ed, "Match found");
        return;
    }

    /* 2. Search lines above current line */
    for (ssize_t l = (ssize_t)cur_line - 1; l >= 0; l--) {
        size_t line_r = edit_line_runes(ed->doc.lines[l], ed->doc.lens[l]);
        if (search_line_backward(ed->doc.lines[l], ed->doc.lens[l], line_r,
                                 needle, nlen, &match_col)) {
            ed->cx_line = (size_t)l;
            ed->cx_col  = match_col;
            ed->goal_col = match_col;
            cursor_clamp(ed);
            ed->search.has_match = true;
            editor_set_message(ed, "Match found");
            return;
        }
    }

    /* 3. Wrap around from bottom */
    for (ssize_t l = (ssize_t)ed->doc.count - 1; l >= (ssize_t)cur_line; l--) {
        size_t line_r = edit_line_runes(ed->doc.lines[l], ed->doc.lens[l]);
        if (l == (ssize_t)cur_line && cur_col > 0) line_r = cur_col;
        if (search_line_backward(ed->doc.lines[l], ed->doc.lens[l], line_r,
                                 needle, nlen, &match_col)) {
            ed->cx_line = (size_t)l;
            ed->cx_col  = match_col;
            ed->goal_col = match_col;
            cursor_clamp(ed);
            ed->search.has_match = true;
            editor_set_message(ed, "Match found (wrapped)");
            return;
        }
    }

    editor_set_message(ed, "Pattern not found: %s", needle);
}

static void execute_command(editor_t *ed, const char *cmd)
{
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return;

    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
        if (ed->dirty) {
            prompt_open_quit_confirm(ed);
            return;
        }
        ed->running = false;
        return;
    }
    if (strcmp(cmd, "q!") == 0) {
        ed->running = false;
        return;
    }
    if (strcmp(cmd, "save") == 0 || strcmp(cmd, "w") == 0) {
        editor_save(ed);
        return;
    }
    if (strncmp(cmd, "save-as ", 8) == 0 || strncmp(cmd, "w ", 2) == 0) {
        const char *arg = (cmd[0] == 's') ? cmd + 8 : cmd + 2;
        while (*arg == ' ') arg++;
        if (*arg != '\0') editor_save_to(ed, arg);
        return;
    }
    if (strncmp(cmd, "open ", 5) == 0 || strncmp(cmd, "e ", 2) == 0) {
        const char *arg = (cmd[0] == 'o') ? cmd + 5 : cmd + 2;
        while (*arg == ' ') arg++;
        if (*arg != '\0') editor_load(ed, arg);
        return;
    }
    if (strncmp(cmd, "goto ", 5) == 0) {
        long target = atol(cmd + 5);
        if (target > 0) {
            ed->cx_line = (size_t)(target - 1);
            ed->cx_col  = 0;
            ed->goal_col = 0;
            cursor_clamp(ed);
            editor_set_message(ed, "Jumped to line %ld", target);
        }
        return;
    }
    if (strcmp(cmd, "help") == 0) {
        editor_set_message(ed, "Commands: save, save-as <file>, open <file>, goto <line>, quit");
        return;
    }

    editor_set_message(ed, "Unknown command '%s'", cmd);
}

static void submit_prompt(editor_t *ed)
{
    prompt_kind_t kind = ed->prompt.kind;
    char text[512];
    snprintf(text, sizeof(text), "%s", ed->prompt.buf);
    prompt_close(ed);

    switch (kind) {
    case PROMPT_COMMAND:
        execute_command(ed, text);
        break;

    case PROMPT_FIND:
        if (text[0] != '\0') {
            snprintf(ed->search.query, sizeof(ed->search.query), "%.*s",
                     (int)(sizeof(ed->search.query) - 1), text);
            search_find_next(ed);
        }
        break;

    case PROMPT_GOTO:
        if (text[0] != '\0') {
            long target = atol(text);
            if (target > 0) {
                ed->cx_line = (size_t)(target - 1);
                ed->cx_col  = 0;
                ed->goal_col = 0;
                cursor_clamp(ed);
                editor_set_message(ed, "Jumped to line %ld", target);
            }
        }
        break;

    case PROMPT_SAVE_AS:
        if (text[0] != '\0') {
            editor_save_to(ed, text);
        }
        break;

    case PROMPT_QUIT_CONFIRM:
        if (text[0] == 'y' || text[0] == 'Y') {
            if (editor_save(ed)) ed->running = false;
        } else if (text[0] == 'n' || text[0] == 'N') {
            ed->running = false;
        }
        break;

    case PROMPT_NONE:
    default:
        break;
    }
}

bool prompt_handle_key(editor_t *ed, const tam_key_event_t *ev)
{
    if (!ed->prompt.active) return false;

    if (ev->type == TAM_KEY_ESC ||
        (ev->type == TAM_KEY_CHAR && (ev->mods & TAM_MOD_CTRL) && ev->codepoint == 'q')) {
        prompt_close(ed);
        editor_set_message(ed, "Cancelled");
        return true;
    }

    if (ed->prompt.kind == PROMPT_QUIT_CONFIRM) {
        if (ev->type == TAM_KEY_CHAR && ev->mods == 0) {
            uint32_t cp = ev->codepoint;
            if (cp == 'y' || cp == 'Y') {
                prompt_close(ed);
                if (editor_save(ed)) ed->running = false;
                return true;
            }
            if (cp == 'n' || cp == 'N') {
                prompt_close(ed);
                ed->running = false;
                return true;
            }
            if (cp == 'c' || cp == 'C' || cp == 0x1B) {
                prompt_close(ed);
                editor_set_message(ed, "Cancelled");
                return true;
            }
        }
    }

    switch (ev->type) {
    case TAM_KEY_ENTER:
        submit_prompt(ed);
        return true;

    case TAM_KEY_CHAR:
        if (ev->mods == 0 && ev->codepoint >= 0x20 && ev->codepoint != 0x7F) {
            char utf8[4];
            size_t n = tam_utf8_encode(ev->codepoint, utf8);
            if (ed->prompt.len + n < sizeof(ed->prompt.buf)) {
                size_t byte_pos = edit_rune_to_byte(ed->prompt.buf, ed->prompt.len, ed->prompt.cx);
                memmove(ed->prompt.buf + byte_pos + n, ed->prompt.buf + byte_pos,
                        ed->prompt.len - byte_pos + 1);
                memcpy(ed->prompt.buf + byte_pos, utf8, n);
                ed->prompt.len += n;
                ed->prompt.cx++;
            }
            return true;
        }
        return false;

    case TAM_KEY_BACKSPACE:
        if (ed->prompt.cx > 0) {
            size_t from = edit_rune_to_byte(ed->prompt.buf, ed->prompt.len, ed->prompt.cx - 1);
            size_t to   = edit_rune_to_byte(ed->prompt.buf, ed->prompt.len, ed->prompt.cx);
            size_t n = to - from;
            memmove(ed->prompt.buf + from, ed->prompt.buf + to,
                    ed->prompt.len - to + 1);
            ed->prompt.len -= n;
            ed->prompt.cx--;
            return true;
        }
        return false;

    case TAM_KEY_DELETE: {
        size_t runes = edit_line_runes(ed->prompt.buf, ed->prompt.len);
        if (ed->prompt.cx < runes) {
            size_t from = edit_rune_to_byte(ed->prompt.buf, ed->prompt.len, ed->prompt.cx);
            size_t to   = edit_rune_to_byte(ed->prompt.buf, ed->prompt.len, ed->prompt.cx + 1);
            size_t n = to - from;
            memmove(ed->prompt.buf + from, ed->prompt.buf + to,
                    ed->prompt.len - to + 1);
            ed->prompt.len -= n;
            return true;
        }
        return false;
    }

    case TAM_KEY_LEFT:
        if (ed->prompt.cx > 0) ed->prompt.cx--;
        return true;

    case TAM_KEY_RIGHT: {
        size_t runes = edit_line_runes(ed->prompt.buf, ed->prompt.len);
        if (ed->prompt.cx < runes) ed->prompt.cx++;
        return true;
    }

    case TAM_KEY_HOME:
        ed->prompt.cx = 0;
        return true;

    case TAM_KEY_END:
        ed->prompt.cx = edit_line_runes(ed->prompt.buf, ed->prompt.len);
        return true;

    default:
        return false;
    }
}
