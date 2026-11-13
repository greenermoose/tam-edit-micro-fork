#ifndef TAM_EDIT_COMPLETE_H
#define TAM_EDIT_COMPLETE_H

#include "editor.h"

void prompt_comp_clear(prompt_t *p);
bool prompt_complete_step(editor_t *ed, int dir);

#endif
