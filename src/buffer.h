#ifndef TAM_EDIT_BUFFER_H
#define TAM_EDIT_BUFFER_H

#include "editor.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

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
    char                          encoding[64];
    bool                          has_bom;
    bool                          used_fallback_enc;
    char                          fallback_msg[128];
    struct timespec               mtime;
    bool                          mtime_valid;
    bool                          readonly;
    bool                          scratch;
} buffer_t;

buffer_t *buffer_new(const char *path);
buffer_t *buffer_new_scratch(const char *title, const char *text);
buffer_t *buffer_open(const char *path);
buffer_t *buffer_open_with_encoding(const char *path, const char *configured_enc, int detectlimit);
bool      buffer_reopen(buffer_t *buf, const char *enc_override, int detectlimit,
                        char *err_buf, size_t err_size);
bool      buffer_check_external_change(const buffer_t *buf);
bool      buffer_save(buffer_t *buf);
bool      buffer_save_with_opts(buffer_t *buf, bool rmtrailingws, bool eofnewline,
                                bool mkparents, char *err_buf, size_t err_size);
bool      buffer_save_to(buffer_t *buf, const char *path);
bool      buffer_save_to_with_opts(buffer_t *buf, const char *path,
                                   bool rmtrailingws, bool eofnewline,
                                   bool mkparents, char *err_buf, size_t err_size);
size_t    buffer_retab(buffer_t *buf, int tabsize, bool tabstospaces);
void      buffer_ref(buffer_t *buf);
void      buffer_unref(buffer_t *buf);

#endif
