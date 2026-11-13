#include "encoding.h"

#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool utf8_validate(const char *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t b0 = (uint8_t)s[i];
        if (b0 <= 0x7F) {
            i++;
            continue;
        }
        if (b0 >= 0xC2 && b0 <= 0xDF) {
            if (i + 1 >= len) return false;
            uint8_t b1 = (uint8_t)s[i + 1];
            if ((b1 & 0xC0) != 0x80) return false;
            i += 2;
        } else if (b0 >= 0xE0 && b0 <= 0xEF) {
            if (i + 2 >= len) return false;
            uint8_t b1 = (uint8_t)s[i + 1];
            uint8_t b2 = (uint8_t)s[i + 2];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
            if (b0 == 0xE0 && b1 < 0xA0) return false; /* overlong */
            if (b0 == 0xED && b1 >= 0xA0) return false; /* surrogate D800-DFFF */
            i += 3;
        } else if (b0 >= 0xF0 && b0 <= 0xF4) {
            if (i + 3 >= len) return false;
            uint8_t b1 = (uint8_t)s[i + 1];
            uint8_t b2 = (uint8_t)s[i + 2];
            uint8_t b3 = (uint8_t)s[i + 3];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
                return false;
            }
            if (b0 == 0xF0 && b1 < 0x90) return false; /* overlong */
            if (b0 == 0xF4 && b1 > 0x8F) return false; /* > 0x10FFFF */
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

static void str_lower_trim(const char *in, char *out, size_t out_size) {
    if (out_size == 0) return;
    if (!in) {
        out[0] = '\0';
        return;
    }
    while (*in && isspace((unsigned char)*in)) in++;
    size_t len = strlen(in);
    while (len > 0 && isspace((unsigned char)in[len - 1])) len--;
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < out_size; i++) {
        out[n++] = (char)tolower((unsigned char)in[i]);
    }
    out[n] = '\0';
}

void encoding_canonical_name(const char *name, char *out, size_t out_size) {
    char tmp[64];
    str_lower_trim(name, tmp, sizeof(tmp));
    if (tmp[0] == '\0' || strcmp(tmp, "utf8") == 0 || strcmp(tmp, "utf-8") == 0) {
        snprintf(out, out_size, "utf-8");
    } else if (strcmp(tmp, "utf16") == 0 || strcmp(tmp, "utf16le") == 0 ||
               strcmp(tmp, "utf-16") == 0 || strcmp(tmp, "utf-16le") == 0) {
        snprintf(out, out_size, "utf-16le");
    } else if (strcmp(tmp, "utf16be") == 0 || strcmp(tmp, "utf-16be") == 0) {
        snprintf(out, out_size, "utf-16be");
    } else if (strcmp(tmp, "latin1") == 0 || strcmp(tmp, "latin-1") == 0 ||
               strcmp(tmp, "iso8859-1") == 0 || strcmp(tmp, "iso_8859-1") == 0 ||
               strcmp(tmp, "iso-8859-1") == 0) {
        snprintf(out, out_size, "iso-8859-1");
    } else if (strcmp(tmp, "latin9") == 0 || strcmp(tmp, "iso-8859-15") == 0) {
        snprintf(out, out_size, "iso-8859-15");
    } else if (strcmp(tmp, "cp1252") == 0 || strcmp(tmp, "win1252") == 0 ||
               strcmp(tmp, "windows1252") == 0 || strcmp(tmp, "windows-1252") == 0) {
        snprintf(out, out_size, "windows-1252");
    } else if (strcmp(tmp, "cp1251") == 0 || strcmp(tmp, "windows-1251") == 0) {
        snprintf(out, out_size, "windows-1251");
    } else if (strcmp(tmp, "sjis") == 0 || strcmp(tmp, "shiftjis") == 0 ||
               strcmp(tmp, "shift-jis") == 0 || strcmp(tmp, "shift_jis") == 0) {
        snprintf(out, out_size, "shift_jis");
    } else if (strcmp(tmp, "eucjp") == 0 || strcmp(tmp, "euc-jp") == 0) {
        snprintf(out, out_size, "euc-jp");
    } else if (strcmp(tmp, "ascii") == 0 || strcmp(tmp, "us-ascii") == 0) {
        snprintf(out, out_size, "us-ascii");
    } else {
        snprintf(out, out_size, "%s", tmp);
    }
}

static bool has_iconv_suffix(const char *s) {
    return strstr(s, "//") != NULL;
}

bool encoding_is_valid(const char *name) {
    if (!name || !name[0]) return false;
    char canon[64];
    encoding_canonical_name(name, canon, sizeof(canon));
    if (has_iconv_suffix(canon)) return false;

    iconv_t cd1 = iconv_open("UTF-8", canon);
    if (cd1 == (iconv_t)-1) return false;
    iconv_close(cd1);

    iconv_t cd2 = iconv_open(canon, "UTF-8");
    if (cd2 == (iconv_t)-1) return false;
    iconv_close(cd2);
    return true;
}

static bool run_iconv(const char *to_enc, const char *from_enc,
                      const char *in_data, size_t in_len,
                      char **out_data, size_t *out_len,
                      size_t prepend_len, const char *prepend_bytes) {
    iconv_t cd = iconv_open(to_enc, from_enc);
    if (cd == (iconv_t)-1) return false;

    size_t cap = in_len * 4 + prepend_len + 16;
    if (cap < 64) cap = 64;
    char *buf = malloc(cap);
    if (!buf) {
        iconv_close(cd);
        return false;
    }

    size_t used = 0;
    if (prepend_len > 0 && prepend_bytes) {
        memcpy(buf, prepend_bytes, prepend_len);
        used = prepend_len;
    }

    /* Make a mutable copy if non-empty so iconv's char ** parameter stays strict C11 */
    char *in_copy = NULL;
    if (in_len > 0) {
        in_copy = malloc(in_len);
        if (!in_copy) {
            free(buf);
            iconv_close(cd);
            return false;
        }
        memcpy(in_copy, in_data, in_len);
    }

    char *in_ptr = in_copy;
    size_t in_left = in_len;

    while (in_left > 0) {
        if (used + 16 >= cap) {
            size_t new_cap = cap * 2;
            char *nb = realloc(buf, new_cap);
            if (!nb) {
                free(in_copy);
                free(buf);
                iconv_close(cd);
                return false;
            }
            buf = nb;
            cap = new_cap;
        }
        char *out_ptr = buf + used;
        size_t out_left = cap - 1 - used;
        size_t res = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
        used = (size_t)(out_ptr - buf);
        if (res == (size_t)-1) {
            if (errno == E2BIG) {
                continue;
            }
            free(in_copy);
            free(buf);
            iconv_close(cd);
            return false;
        }
    }

    /* Flush any shift state */
    for (;;) {
        if (used + 16 >= cap) {
            size_t new_cap = cap * 2;
            char *nb = realloc(buf, new_cap);
            if (!nb) {
                free(in_copy);
                free(buf);
                iconv_close(cd);
                return false;
            }
            buf = nb;
            cap = new_cap;
        }
        char *out_ptr = buf + used;
        size_t out_left = cap - 1 - used;
        size_t res = iconv(cd, NULL, NULL, &out_ptr, &out_left);
        used = (size_t)(out_ptr - buf);
        if (res == (size_t)-1) {
            if (errno == E2BIG) continue;
            free(in_copy);
            free(buf);
            iconv_close(cd);
            return false;
        }
        break;
    }

    free(in_copy);
    iconv_close(cd);
    buf[used] = '\0';
    *out_data = buf;
    *out_len = used;
    return true;
}

static bool dup_slice(const char *src, size_t len, char **out, size_t *out_len) {
    char *copy = malloc(len + 1);
    if (!copy) return false;
    if (len > 0) memcpy(copy, src, len);
    copy[len] = '\0';
    *out = copy;
    *out_len = len;
    return true;
}

bool encoding_decode_buffer(const char *raw, size_t raw_len,
                            const char *configured_enc,
                            char **out_utf8, size_t *out_len,
                            char *detected_enc, size_t enc_size,
                            bool *out_bom, bool *out_fallback,
                            char *err_buf, size_t err_size) {
    *out_utf8 = NULL;
    *out_len = 0;
    if (out_bom) *out_bom = false;
    if (out_fallback) *out_fallback = false;
    if (err_buf && err_size > 0) err_buf[0] = '\0';

    char cfg[64];
    encoding_canonical_name(configured_enc && configured_enc[0] ? configured_enc : "utf-8",
                            cfg, sizeof(cfg));

    if (raw_len == 0) {
        if (detected_enc && enc_size > 0) snprintf(detected_enc, enc_size, "%s", cfg);
        return dup_slice("", 0, out_utf8, out_len);
    }

    const unsigned char *u = (const unsigned char *)raw;

    /* 1. BOM check */
    if (raw_len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        if (utf8_validate(raw + 3, raw_len - 3)) {
            if (detected_enc && enc_size > 0) snprintf(detected_enc, enc_size, "utf-8");
            if (out_bom) *out_bom = true;
            return dup_slice(raw + 3, raw_len - 3, out_utf8, out_len);
        }
    } else if (raw_len >= 2 && u[0] == 0xFF && u[1] == 0xFE) {
        if (run_iconv("UTF-8", "UTF-16LE", raw + 2, raw_len - 2, out_utf8, out_len, 0, NULL)) {
            if (detected_enc && enc_size > 0) snprintf(detected_enc, enc_size, "utf-16le");
            if (out_bom) *out_bom = true;
            return true;
        }
    } else if (raw_len >= 2 && u[0] == 0xFE && u[1] == 0xFF) {
        if (run_iconv("UTF-8", "UTF-16BE", raw + 2, raw_len - 2, out_utf8, out_len, 0, NULL)) {
            if (detected_enc && enc_size > 0) snprintf(detected_enc, enc_size, "utf-16be");
            if (out_bom) *out_bom = true;
            return true;
        }
    }

    /* 2. Valid UTF-8 */
    if (utf8_validate(raw, raw_len)) {
        if (detected_enc && enc_size > 0) snprintf(detected_enc, enc_size, "utf-8");
        return dup_slice(raw, raw_len, out_utf8, out_len);
    }

    /* 3. Configured encoding (if valid and non-UTF-8) */
    if (strcmp(cfg, "utf-8") != 0 && encoding_is_valid(cfg)) {
        if (run_iconv("UTF-8", cfg, raw, raw_len, out_utf8, out_len, 0, NULL)) {
            if (detected_enc && enc_size > 0) snprintf(detected_enc, enc_size, "%s", cfg);
            return true;
        }
    }

    /* 4. Fallback to windows-1252, then iso-8859-1 */
    if (run_iconv("UTF-8", "WINDOWS-1252", raw, raw_len, out_utf8, out_len, 0, NULL)) {
        if (detected_enc && enc_size > 0) snprintf(detected_enc, enc_size, "windows-1252");
        if (out_fallback) *out_fallback = true;
        return true;
    }

    if (run_iconv("UTF-8", "ISO-8859-1", raw, raw_len, out_utf8, out_len, 0, NULL)) {
        if (detected_enc && enc_size > 0) snprintf(detected_enc, enc_size, "iso-8859-1");
        if (out_fallback) *out_fallback = true;
        return true;
    }

    if (err_buf && err_size > 0) {
        snprintf(err_buf, err_size, "Failed to decode file using '%s'", cfg);
    }
    return false;
}

bool encoding_encode_buffer(const char *utf8, size_t utf8_len,
                            const char *target_enc, bool with_bom,
                            char **out_bytes, size_t *out_len,
                            char *err_buf, size_t err_size) {
    *out_bytes = NULL;
    *out_len = 0;
    if (err_buf && err_size > 0) err_buf[0] = '\0';

    char canon[64];
    encoding_canonical_name(target_enc && target_enc[0] ? target_enc : "utf-8",
                            canon, sizeof(canon));

    if (!encoding_is_valid(canon)) {
        if (err_buf && err_size > 0) {
            snprintf(err_buf, err_size, "Unknown encoding '%s'", canon);
        }
        return false;
    }

    const char *bom = NULL;
    size_t bom_len = 0;
    if (with_bom) {
        if (strcmp(canon, "utf-8") == 0) {
            bom = "\xEF\xBB\xBF";
            bom_len = 3;
        } else if (strcmp(canon, "utf-16le") == 0) {
            bom = "\xFF\xFE";
            bom_len = 2;
        } else if (strcmp(canon, "utf-16be") == 0) {
            bom = "\xFE\xFF";
            bom_len = 2;
        }
    }

    if (strcmp(canon, "utf-8") == 0) {
        size_t total = bom_len + utf8_len;
        char *buf = malloc(total + 1);
        if (!buf) {
            if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Out of memory");
            return false;
        }
        if (bom_len > 0) memcpy(buf, bom, bom_len);
        if (utf8_len > 0 && utf8) memcpy(buf + bom_len, utf8, utf8_len);
        buf[total] = '\0';
        *out_bytes = buf;
        *out_len = total;
        return true;
    }

    if (!run_iconv(canon, "UTF-8", utf8 ? utf8 : "", utf8_len,
                   out_bytes, out_len, bom_len, bom)) {
        if (err_buf && err_size > 0) {
            if (errno == EILSEQ || errno == EINVAL) {
                snprintf(err_buf, err_size,
                         "Cannot save: character cannot be represented in encoding '%s'",
                         canon);
            } else {
                snprintf(err_buf, err_size,
                         "Encoding conversion to '%s' failed: %s",
                         canon, strerror(errno));
            }
        }
        return false;
    }
    return true;
}
