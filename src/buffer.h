#ifndef TAM_EDIT_BUFFER_H
#define TAM_EDIT_BUFFER_H

#include "editor.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct buffer {
    edit_doc_t   doc;
    char        *path;
    bool         crlf;
    bool         dirty;
    undo_stack_t undo_stack;
    undo_stack_t redo_stack;
    int          refcount;
    uint64_t     id;
    bool         scrollsync;
} buffer_t;

buffer_t *buffer_new(const char *path);
buffer_t *buffer_open(const char *path);
bool      buffer_save(buffer_t *buf);
bool      buffer_save_to(buffer_t *buf, const char *path);
void      buffer_ref(buffer_t *buf);
void      buffer_unref(buffer_t *buf);

#endif
