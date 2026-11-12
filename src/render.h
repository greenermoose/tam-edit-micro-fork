#ifndef TAM_EDIT_RENDER_H
#define TAM_EDIT_RENDER_H

#include "editor.h"
#include <tam_tty.h>

/* Rows available for document text, keeping two rows for the message line
 * and the status line. */
int render_text_rows(const tam_tty_t *tty);

/* Scroll the editor so the cursor is visible, then paint a complete frame. */
void render_draw(editor_t *ed, tam_tty_t *tty);

#endif
