#ifndef TAM_EDIT_ACTIONS_H
#define TAM_EDIT_ACTIONS_H

#include "editor.h"
#include <tam_key.h>

typedef void (*action_fn)(editor_t *ed);

void actions_init(void);
void actions_free(void);
bool actions_execute(editor_t *ed, const char *name);
bool actions_handle_key(editor_t *ed, const tam_key_event_t *ev, int text_rows);

#endif
