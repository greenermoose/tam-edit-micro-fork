#ifndef TAM_EDIT_UNDO_H
#define TAM_EDIT_UNDO_H

#include "editor.h"

void undo_init(editor_t *ed);
void undo_free(editor_t *ed);
void undo_clear_stack(undo_stack_t *stack);
void undo_clear_redo(editor_t *ed);

void undo_begin_group(editor_t *ed);
void undo_end_group(editor_t *ed);

void undo_record_insert(editor_t *ed, size_t line, size_t col, const char *text, size_t len);
void undo_record_remove(editor_t *ed, size_t line, size_t col, const char *text, size_t len);

bool undo_perform(editor_t *ed);
bool redo_perform(editor_t *ed);

#endif
