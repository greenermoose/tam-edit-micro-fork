#ifndef TAM_EDIT_BUFFER_H
#define TAM_EDIT_BUFFER_H

#include "editor.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct syntax_def;
struct syntax_region;
struct syntax_line_cache;

typedef struct buffer {
    edit_doc_t                    doc;
    char                         *path;
    bool                          crlf;
    bool                          dirty;
    undo_stack_t                  undo_stack;
    undo_stack_t                  redo_stack;
    int                           refcount;
    uint64_t                      id;
    bool                          scrollsync;
    char                          filetype[64];
    bool                          syntax_enabled;
    const struct syntax_def      *syntax_def;
    const struct syntax_region  **line_states;
    size_t                        state_count;
    size_t                        state_cap;
    size_t                        syntax_valid_through;
    struct syntax_line_cache     *syntax_cache;
} buffer_t;

buffer_t *buffer_new(const char *path);
buffer_t *buffer_open(const char *path);
bool      buffer_save(buffer_t *buf);
bool      buffer_save_to(buffer_t *buf, const char *path);
void      buffer_ref(buffer_t *buf);
void      buffer_unref(buffer_t *buf);

#endif
