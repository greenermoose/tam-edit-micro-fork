#ifndef TAM_EDIT_ENCODING_H
#define TAM_EDIT_ENCODING_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Normalize encoding aliases (e.g. "utf8" -> "utf-8", "latin1" -> "iso-8859-1",
 * "cp1252" -> "windows-1252") into canonical lowercase form.
 */
void encoding_canonical_name(const char *name, char *out, size_t out_size);

/*
 * Return true if glibc iconv recognizes the encoding name/alias for conversion
 * to and from UTF-8.
 */
bool encoding_is_valid(const char *name);

/*
 * Decode raw file bytes into a newly allocated NUL-terminated UTF-8 buffer.
 * Detection order:
 *   1. BOM (UTF-8 EF BB BF, UTF-16LE FF FE, UTF-16BE FE FF)
 *   2. Valid UTF-8
 *   3. Configured encoding (if non-empty and valid)
 *   4. Fallback to windows-1252, then iso-8859-1 (sets *out_fallback = true)
 */
bool encoding_decode_buffer(const char *raw, size_t raw_len,
                            const char *configured_enc,
                            char **out_utf8, size_t *out_len,
                            char *detected_enc, size_t enc_size,
                            bool *out_bom, bool *out_fallback,
                            char *err_buf, size_t err_size);

/*
 * Encode internal UTF-8 bytes to target_enc. Fails without silent loss if
 * any character is unrepresentable in target_enc.
 */
bool encoding_encode_buffer(const char *utf8, size_t utf8_len,
                            const char *target_enc, bool with_bom,
                            char **out_bytes, size_t *out_len,
                            char *err_buf, size_t err_size);

#endif
