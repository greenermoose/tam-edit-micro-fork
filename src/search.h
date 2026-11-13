#ifndef TAM_EDIT_SEARCH_H
#define TAM_EDIT_SEARCH_H

#include "editor.h"
#include "regex.h"

#include <stdbool.h>
#include <stddef.h>

void search_invalidate(editor_t *ed);
bool search_compile(editor_t *ed, const char *query, bool literal,
                    char *err_buf, size_t err_size);

void search_find_next(editor_t *ed);
void search_find_prev(editor_t *ed);

/* Incremental search prompt lifecycle */
void search_inc_begin(editor_t *ed);
void search_inc_update(editor_t *ed, bool forward);
void search_inc_commit(editor_t *ed);
void search_inc_cancel(editor_t *ed);

/* Collect hlsearch matches for a line (returns count written up to max_out) */
size_t search_line_matches(editor_t *ed, size_t line,
                           tam_regex_match_t *out, size_t max_out);

/* Expand $0/$1/${1}/$name/${name}/$$ capture template against captures */
char *search_expand_template(const tam_regex_t *re, const char *text,
                             const tam_regex_captures_t *caps,
                             const char *tmpl, size_t *out_len);

/* Non-interactive replaceall (single grouped undo). Returns replacement count, or -1 on regex error. */
long search_replace_all(editor_t *ed, const char *pattern, const char *replacement,
                        bool in_selection, bool literal,
                        char *err_buf, size_t err_size);

/* Interactive step-by-step replace */
bool search_replace_begin(editor_t *ed, const char *pattern, const char *replacement,
                          bool in_selection, char *err_buf, size_t err_size);
bool search_replace_step(editor_t *ed, char decision);

#endif
