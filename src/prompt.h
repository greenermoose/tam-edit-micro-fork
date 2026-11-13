#ifndef TAM_EDIT_PROMPT_H
#define TAM_EDIT_PROMPT_H

#include "editor.h"
#include <tam_key.h>

void prompt_open_command(editor_t *ed);
void prompt_open_find(editor_t *ed);
void prompt_open_goto(editor_t *ed);
void prompt_open_save_as(editor_t *ed);
void prompt_open_quit_confirm(editor_t *ed);
void prompt_close(editor_t *ed);

bool prompt_handle_key(editor_t *ed, const tam_key_event_t *ev);
void prompt_execute_command(editor_t *ed, const char *cmd);

void search_find_next(editor_t *ed);
void search_find_prev(editor_t *ed);

#endif
