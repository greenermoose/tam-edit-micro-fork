#include "undo.h"
#include "cursor.h"

#include <tam.h>
#include <tam_uni.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Forward declaration of raw document operations implemented in editor.c */
void editor_raw_insert(editor_t *ed, size_t line, size_t col, const char *text, size_t len);
void editor_raw_remove(editor_t *ed, size_t line, size_t col, size_t runes);

static int64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void undo_init(editor_t *ed)
{
    ed->undo_stack.head = NULL;
    ed->undo_stack.count = 0;
    ed->redo_stack.head = NULL;
    ed->redo_stack.count = 0;
}

static void free_group(undo_group_t *grp)
{
    if (grp == NULL) return;
    for (size_t i = 0; i < grp->count; i++) {
        free(grp->deltas[i].text);
    }
    free(grp->deltas);
    free(grp);
}

void undo_clear_stack(undo_stack_t *stack)
{
    if (stack == NULL) return;
    undo_group_t *curr = stack->head;
    while (curr != NULL) {
        undo_group_t *next = curr->next;
        free_group(curr);
        curr = next;
    }
    stack->head = NULL;
    stack->count = 0;
}

void undo_clear_redo(editor_t *ed)
{
    undo_clear_stack(&ed->redo_stack);
}

void undo_free(editor_t *ed)
{
    undo_clear_stack(&ed->undo_stack);
    undo_clear_stack(&ed->redo_stack);
}

static bool group_append_delta(undo_group_t *grp, delta_type_t type,
                               size_t line, size_t col,
                               const char *text, size_t len)
{
    if (grp->count >= grp->cap) {
        size_t new_cap = grp->cap ? grp->cap * 2 : 8;
        delta_t *d = realloc(grp->deltas, new_cap * sizeof(*d));
        if (d == NULL) return false;
        grp->deltas = d;
        grp->cap = new_cap;
    }

    char *copy = malloc(len + 1);
    if (copy == NULL) return false;
    memcpy(copy, text, len);
    copy[len] = '\0';

    grp->deltas[grp->count].type = type;
    grp->deltas[grp->count].line = line;
    grp->deltas[grp->count].col  = col;
    grp->deltas[grp->count].text = copy;
    grp->deltas[grp->count].len  = len;
    grp->count++;
    return true;
}

static undo_group_t *create_group(editor_t *ed, int64_t now)
{
    undo_group_t *grp = calloc(1, sizeof(*grp));
    if (grp == NULL) return NULL;
    grp->cursor_before = (loc_t){ed->cx_line, ed->cx_col};
    grp->cursor_after  = (loc_t){ed->cx_line, ed->cx_col};
    grp->time_ms       = now;
    grp->next          = ed->undo_stack.head;
    ed->undo_stack.head = grp;
    ed->undo_stack.count++;
    return grp;
}

void undo_record_insert(editor_t *ed, size_t line, size_t col,
                        const char *text, size_t len)
{
    int64_t now = get_time_ms();
    undo_group_t *grp = ed->undo_stack.head;

    bool merge = false;
    if (grp != NULL && (now - grp->time_ms < 1000) && grp->count > 0) {
        if (grp->deltas[grp->count - 1].type == DELTA_INSERT) {
            if (line == grp->cursor_after.line && col == grp->cursor_after.col) {
                merge = true;
            }
        } else if (grp->deltas[grp->count - 1].type == DELTA_REMOVE) {
            if (line == grp->cursor_after.line && col == grp->cursor_after.col) {
                merge = true;
            }
        }
    }

    if (!merge) {
        grp = create_group(ed, now);
        if (grp == NULL) return;
    }

    if (!group_append_delta(grp, DELTA_INSERT, line, col, text, len)) return;

    if (strcmp(text, "\n") == 0) {
        grp->cursor_after.line = line + 1;
        grp->cursor_after.col  = 0;
    } else {
        grp->cursor_after.line = line;
        grp->cursor_after.col  = col + edit_line_runes(text, len);
    }
    grp->time_ms = now;
}

void undo_record_remove(editor_t *ed, size_t line, size_t col,
                        const char *text, size_t len)
{
    int64_t now = get_time_ms();
    undo_group_t *grp = ed->undo_stack.head;

    bool merge = false;
    if (grp != NULL && (now - grp->time_ms < 1000) && grp->count > 0) {
        if (grp->deltas[grp->count - 1].type == DELTA_REMOVE) {
            if (grp->cursor_after.line == line &&
                (grp->cursor_after.col == col + edit_line_runes(text, len) ||
                 (col == 0 && grp->cursor_after.line == line + 1))) {
                merge = true;
            }
        }
    }

    if (!merge) {
        grp = create_group(ed, now);
        if (grp == NULL) return;
    }

    if (!group_append_delta(grp, DELTA_REMOVE, line, col, text, len)) return;

    grp->cursor_after.line = line;
    grp->cursor_after.col  = col;
    grp->time_ms = now;
}

bool undo_perform(editor_t *ed)
{
    if (ed->undo_stack.head == NULL) {
        editor_set_message(ed, "Nothing to undo");
        return false;
    }

    undo_group_t *grp = ed->undo_stack.head;
    ed->undo_stack.head = grp->next;
    ed->undo_stack.count--;

    /* Apply inverse operations in reverse order */
    for (size_t i = grp->count; i > 0; i--) {
        delta_t *d = &grp->deltas[i - 1];
        if (d->type == DELTA_INSERT) {
            editor_raw_remove_str(ed, d->line, d->col, d->text, d->len);
        } else if (d->type == DELTA_REMOVE) {
            editor_raw_insert(ed, d->line, d->col, d->text, d->len);
        }
    }

    ed->cx_line = grp->cursor_before.line;
    ed->cx_col  = grp->cursor_before.col;
    ed->goal_col = ed->cx_col;
    cursor_clamp(ed);
    ed->sel.active = false;
    ed->dirty = true;

    /* Push to redo stack */
    grp->next = ed->redo_stack.head;
    ed->redo_stack.head = grp;
    ed->redo_stack.count++;

    editor_set_message(ed, "Undone");
    return true;
}

bool redo_perform(editor_t *ed)
{
    if (ed->redo_stack.head == NULL) {
        editor_set_message(ed, "Nothing to redo");
        return false;
    }

    undo_group_t *grp = ed->redo_stack.head;
    ed->redo_stack.head = grp->next;
    ed->redo_stack.count--;

    /* Apply operations in original forward order */
    for (size_t i = 0; i < grp->count; i++) {
        delta_t *d = &grp->deltas[i];
        if (d->type == DELTA_INSERT) {
            editor_raw_insert(ed, d->line, d->col, d->text, d->len);
        } else if (d->type == DELTA_REMOVE) {
            editor_raw_remove_str(ed, d->line, d->col, d->text, d->len);
        }
    }

    ed->cx_line = grp->cursor_after.line;
    ed->cx_col  = grp->cursor_after.col;
    ed->goal_col = ed->cx_col;
    cursor_clamp(ed);
    ed->sel.active = false;
    ed->dirty = true;

    /* Push back to undo stack */
    grp->next = ed->undo_stack.head;
    ed->undo_stack.head = grp;
    ed->undo_stack.count++;

    editor_set_message(ed, "Redone");
    return true;
}
