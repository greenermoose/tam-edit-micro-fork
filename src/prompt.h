#ifndef TAM_EDIT_PROMPT_H
#define TAM_EDIT_PROMPT_H

#include "editor.h"
#include "search.h"
#include <tam_key.h>

void prompt_open_command(editor_t *ed);
void prompt_open_command_prefill(editor_t *ed, const char *prefill);
void prompt_open_find(editor_t *ed);
void prompt_open_find_literal(editor_t *ed);
void prompt_open_goto(editor_t *ed);
void prompt_open_save_as(editor_t *ed);
void prompt_open_quit_confirm(editor_t *ed);
void prompt_open_shell(editor_t *ed);
void prompt_open_replace_confirm(editor_t *ed);
void prompt_open_confirm_reload(editor_t *ed, const char *path);
void prompt_open_show_key(editor_t *ed);
void prompt_close(editor_t *ed);

bool prompt_handle_key(editor_t *ed, const tam_key_event_t *ev);
void prompt_execute_command(editor_t *ed, const char *cmd);

#endif
