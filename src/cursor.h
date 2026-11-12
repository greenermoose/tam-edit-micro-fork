#ifndef TAM_EDIT_CURSOR_H
#define TAM_EDIT_CURSOR_H

#include "editor.h"

void  cursor_clamp(editor_t *ed);
void  cursor_move(editor_t *ed, long dx, long dy, bool select);
void  cursor_home(editor_t *ed, bool select);
void  cursor_end(editor_t *ed, bool select);
void  cursor_page(editor_t *ed, long pages, int text_rows, bool select);
void  cursor_start(editor_t *ed, bool select);
void  cursor_end_doc(editor_t *ed, bool select);
void  cursor_word_left(editor_t *ed, bool select);
void  cursor_word_right(editor_t *ed, bool select);

void  cursor_select_all(editor_t *ed);
void  cursor_deselect(editor_t *ed);
bool  selection_get_range(const editor_t *ed, loc_t *start, loc_t *end);
bool  selection_delete(editor_t *ed);
char *selection_get_text(const editor_t *ed, size_t *out_len);

#endif
