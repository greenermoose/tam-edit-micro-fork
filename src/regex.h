#ifndef TAM_REGEX_H
#define TAM_REGEX_H

#include <stdbool.h>
#include <stddef.h>

enum {
    TAM_REGEX_ICASE     = 1 << 0,
    TAM_REGEX_MULTILINE = 1 << 1, /* ^ and $ also match around '\n' */
    TAM_REGEX_DOTALL    = 1 << 2, /* '.' also matches '\n' */
    TAM_REGEX_UNGREEDY  = 1 << 3  /* invert default greedy/lazy behavior */
};

typedef struct tam_regex tam_regex_t;

typedef struct {
    size_t start; /* byte offset, inclusive */
    size_t end;   /* byte offset, exclusive */
} tam_regex_match_t;

/* Compile a UTF-8 regular expression. Returns NULL on error (writing a message
 * to errbuf if non-NULL and errlen > 0). */
tam_regex_t *tam_regex_compile(const char *pattern, int flags,
                               char *errbuf, size_t errlen);

/* Free a compiled regular expression (NULL is safe). */
void tam_regex_free(tam_regex_t *re);

/* Return true if pattern matches anywhere in text[0..len). */
bool tam_regex_match(const tam_regex_t *re, const char *text, size_t len);

/* Find the leftmost-first match in text[0..len) starting at or after
 * start_byte. Preserves full-text context before start_byte for ^ and \b. */
bool tam_regex_find(const tam_regex_t *re, const char *text, size_t len,
                    size_t start_byte, tam_regex_match_t *out);

/* Find up to max_out non-overlapping leftmost-first matches in text[0..len).
 * Returns the total number of matches found (may exceed max_out if truncated,
 * or capped at max_out: returns number written to out[0..max_out)). */
size_t tam_regex_find_all(const tam_regex_t *re, const char *text, size_t len,
                          tam_regex_match_t *out, size_t max_out);

#endif /* TAM_REGEX_H */
