#ifndef TAM_EDIT_CLIPBOARD_H
#define TAM_EDIT_CLIPBOARD_H

#include <stdbool.h>
#include <stddef.h>

void  clipboard_init(void);
void  clipboard_free(void);
bool  clipboard_set(const char *text, size_t len, int tty_fd);
char *clipboard_get(size_t *out_len);

#endif
