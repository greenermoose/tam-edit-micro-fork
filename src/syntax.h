#ifndef TAM_EDIT_SYNTAX_H
#define TAM_EDIT_SYNTAX_H

#include "colorscheme.h"
#include "regex.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYNTAX_MAX_GROUPS 512

typedef struct syntax_def syntax_def_t;
typedef struct syntax_region syntax_region_t;

typedef struct {
    uint16_t     group_id;
    tam_regex_t *regex;
} syntax_pattern_t;

struct syntax_region {
    uint16_t               group_id;
    uint16_t               limit_group_id;
    struct syntax_region  *parent;
    tam_regex_t           *start;
    tam_regex_t           *end;
    tam_regex_t           *skip;
    syntax_pattern_t      *patterns;
    size_t                 pattern_count;
    struct syntax_region **regions;
    size_t                 region_count;
};

struct syntax_def {
    char             *filetype;
    tam_regex_t      *filename_re;
    tam_regex_t      *header_re;
    tam_regex_t      *signature_re;
    syntax_pattern_t *patterns;
    size_t            pattern_count;
    syntax_region_t **regions;
    size_t            region_count;
    bool              compiled;
    bool              is_override;
};

/* Global syntax registry lifecycle */
void syntax_registry_init(void);
void syntax_registry_free(void);

/* Load or reload user YAML overrides from config directory (e.g. for tests) */
bool syntax_registry_load_yaml_file(const char *path, char *errbuf, size_t errlen);
bool syntax_registry_load_yaml_string(const char *yaml_text, const char *name,
                                      char *errbuf, size_t errlen);

/* Group name <-> ID mapping */
uint16_t    syntax_intern_group(const char *name);
const char *syntax_group_name(uint16_t group_id);
size_t      syntax_group_count(void);

/* Build O(1) style lookup cache for the active colorscheme */
void syntax_build_style_cache(const colorscheme_t *cs, style_t cache[SYNTAX_MAX_GROUPS]);

/* Filetype detection and definition lookup (ensures returned def is compiled) */
syntax_def_t *syntax_find_by_filetype(const char *filetype);
syntax_def_t *syntax_detect(const char *path, const char *requested_ft,
                            const char *const *lines, const size_t *lens,
                            size_t line_count, int detectlimit);

/* Line-state machine (matching micro's pkg/highlight semantics) */
const syntax_region_t *syntax_highlight_line(const syntax_def_t *def,
                                             const syntax_region_t *prev_state,
                                             const char *line, size_t byte_len,
                                             bool states_only,
                                             uint16_t *out_groups, size_t max_runes,
                                             size_t *out_rune_len);

/* Forward-declare buffer_t to integrate incremental buffer highlighting */
struct buffer;

void buffer_syntax_init(struct buffer *buf, const char *requested_ft, int detectlimit);
void buffer_syntax_free(struct buffer *buf);
void buffer_syntax_update_to(struct buffer *buf, size_t target_line);
void buffer_syntax_on_edit(struct buffer *buf, size_t start_line,
                           size_t old_line_count, size_t new_line_count);
const uint16_t *buffer_syntax_get_line_groups(struct buffer *buf, size_t line_idx,
                                              size_t *out_runes);

#endif /* TAM_EDIT_SYNTAX_H */
