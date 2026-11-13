#include "buffer.h"
#include "encoding.h"
#include "syntax.h"
#include "undo.h"

#include <tam.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint64_t g_buffer_id_seq = 1;

static bool doc_reserve(edit_doc_t *d, size_t need)
{
    if (need <= d->cap) return true;
    size_t cap = d->cap ? d->cap : 32;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) return false;
        cap *= 2;
    }
    char **lines = realloc(d->lines, cap * sizeof(*lines));
    if (lines == NULL) return false;
    d->lines = lines;

    size_t *lens = realloc(d->lens, cap * sizeof(*lens));
    if (lens == NULL) return false;
    d->lens = lens;

    d->cap = cap;
    return true;
}

static bool doc_insert_line(edit_doc_t *d, size_t at, const char *data, size_t len)
{
    if (at > d->count) return false;
    if (!doc_reserve(d, d->count + 1)) return false;

    char *copy = malloc(len + 1);
    if (copy == NULL) return false;
    if (len > 0) memcpy(copy, data, len);
    copy[len] = '\0';

    memmove(d->lines + at + 1, d->lines + at, (d->count - at) * sizeof(*d->lines));
    memmove(d->lens + at + 1, d->lens + at, (d->count - at) * sizeof(*d->lens));

    d->lines[at] = copy;
    d->lens[at]  = len;
    d->count++;
    return true;
}

static void doc_clear(edit_doc_t *d)
{
    if (d == NULL) return;
    for (size_t i = 0; i < d->count; i++) free(d->lines[i]);
    free(d->lines);
    free(d->lens);
    memset(d, 0, sizeof(*d));
}

static bool doc_from_text(edit_doc_t *d, const char *data, size_t len, bool *crlf_out)
{
    bool crlf = false;
    for (size_t i = 0; i + 1 < len; i++) {
        if (data[i] == '\n') {
            crlf = i > 0 && data[i - 1] == '\r';
            break;
        }
    }

    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i != len && data[i] != '\n') continue;

        size_t end = i;
        if (crlf && end > start && data[end - 1] == '\r') end--;

        if (!doc_insert_line(d, d->count, data + start, end - start)) {
            return false;
        }
        start = i + 1;
    }

    if (crlf_out != NULL) *crlf_out = crlf;
    return true;
}

buffer_t *buffer_new(const char *path)
{
    buffer_t *b = calloc(1, sizeof(*b));
    if (b == NULL) return NULL;

    b->id = g_buffer_id_seq++;
    b->refcount = 0;
    if (path != NULL && *path != '\0') {
        b->path = strdup(path);
    }
    b->crlf = false;
    b->dirty = false;
    b->scrollsync = false;
    b->syntax_enabled = true;
    snprintf(b->encoding, sizeof(b->encoding), "utf-8");

    /* Empty buffer has one empty line */
    doc_insert_line(&b->doc, 0, "", 0);
    buffer_syntax_init(b, NULL, 100);
    return b;
}

buffer_t *buffer_new_scratch(const char *title, const char *text)
{
    buffer_t *b = calloc(1, sizeof(*b));
    if (b == NULL) return NULL;

    b->id = g_buffer_id_seq++;
    b->refcount = 0;
    if (title != NULL && *title != '\0') {
        b->path = strdup(title);
    }
    b->crlf = false;
    b->dirty = false;
    b->scrollsync = false;
    b->syntax_enabled = false;
    b->readonly = true;
    b->scratch = true;
    snprintf(b->encoding, sizeof(b->encoding), "utf-8");
    snprintf(b->filetype, sizeof(b->filetype), "unknown");

    const char *src = text ? text : "";
    bool crlf = false;
    if (!doc_from_text(&b->doc, src, strlen(src), &crlf)) {
        doc_clear(&b->doc);
        doc_insert_line(&b->doc, 0, "", 0);
    }
    return b;
}

static void record_stat_mtime(buffer_t *b, const char *path)
{
    if (!b || !path) return;
    struct stat st;
    if (stat(path, &st) == 0) {
        b->mtime = st.st_mtim;
        b->mtime_valid = true;
    } else {
        b->mtime_valid = false;
    }
}

buffer_t *buffer_open_with_encoding(const char *path, const char *configured_enc, int detectlimit)
{
    if (path == NULL || *path == '\0') {
        return buffer_new(NULL);
    }

    buffer_t *b = calloc(1, sizeof(*b));
    if (b == NULL) return NULL;

    b->id = g_buffer_id_seq++;
    b->refcount = 0;
    b->path = strdup(path);
    b->dirty = false;
    b->scrollsync = false;
    b->syntax_enabled = true;
    encoding_canonical_name(configured_enc && configured_enc[0] ? configured_enc : "utf-8",
                            b->encoding, sizeof(b->encoding));

    size_t len = 0;
    char *data = tam_fs_read_file(path, &len);
    if (data == NULL) {
        if (errno == ENOENT) {
            /* Missing file opens empty without error */
            doc_insert_line(&b->doc, 0, "", 0);
            buffer_syntax_init(b, NULL, detectlimit > 0 ? detectlimit : 100);
            return b;
        }
        free(b->path);
        free(b);
        return NULL;
    }

    char *utf8 = NULL;
    size_t utf8_len = 0;
    char detected[64] = "";
    bool has_bom = false;
    bool fallback = false;
    char err_buf[128] = "";

    if (!encoding_decode_buffer(data, len, configured_enc,
                                &utf8, &utf8_len,
                                detected, sizeof(detected),
                                &has_bom, &fallback,
                                err_buf, sizeof(err_buf))) {
        free(data);
        free(b->path);
        free(b);
        return NULL;
    }
    free(data);

    bool crlf = false;
    bool ok = doc_from_text(&b->doc, utf8, utf8_len, &crlf);
    free(utf8);

    if (!ok) {
        doc_clear(&b->doc);
        free(b->path);
        free(b);
        return NULL;
    }

    b->crlf = crlf;
    snprintf(b->encoding, sizeof(b->encoding), "%s", detected);
    b->has_bom = has_bom;
    b->used_fallback_enc = fallback;
    if (fallback) {
        snprintf(b->fallback_msg, sizeof(b->fallback_msg),
                 "Decoded with fallback encoding '%s'", detected);
    }
    record_stat_mtime(b, path);
    buffer_syntax_init(b, NULL, detectlimit > 0 ? detectlimit : 100);
    return b;
}

buffer_t *buffer_open(const char *path)
{
    return buffer_open_with_encoding(path, "utf-8", 100);
}

void buffer_ref(buffer_t *buf)
{
    if (buf != NULL) {
        buf->refcount++;
    }
}

static void undo_stack_free(undo_stack_t *st)
{
    undo_group_t *g = st->head;
    while (g != NULL) {
        undo_group_t *next = g->next;
        for (size_t i = 0; i < g->count; i++) free(g->deltas[i].text);
        free(g->deltas);
        free(g);
        g = next;
    }
    st->head  = NULL;
    st->count = 0;
}

void buffer_unref(buffer_t *buf)
{
    if (buf == NULL) return;
    buf->refcount--;
    if (buf->refcount <= 0) {
        buffer_syntax_free(buf);
        doc_clear(&buf->doc);
        free(buf->path);
        undo_stack_free(&buf->undo_stack);
        undo_stack_free(&buf->redo_stack);
        free(buf);
    }
}

bool buffer_check_external_change(const buffer_t *buf)
{
    if (!buf || !buf->path || !buf->mtime_valid || buf->scratch) return false;
    struct stat st;
    if (stat(buf->path, &st) != 0) return false;
    return st.st_mtim.tv_sec != buf->mtime.tv_sec ||
           st.st_mtim.tv_nsec != buf->mtime.tv_nsec;
}

bool buffer_reopen(buffer_t *buf, const char *enc_override, int detectlimit,
                   char *err_buf, size_t err_size)
{
    if (err_buf && err_size > 0) err_buf[0] = '\0';
    if (!buf || !buf->path || !buf->path[0]) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Buffer has no file path");
        return false;
    }

    char target_enc[64] = "";
    if (enc_override && enc_override[0]) {
        if (!encoding_is_valid(enc_override)) {
            if (err_buf && err_size > 0) {
                snprintf(err_buf, err_size, "Unknown encoding '%s'", enc_override);
            }
            return false;
        }
        encoding_canonical_name(enc_override, target_enc, sizeof(target_enc));
    }

    size_t raw_len = 0;
    char *raw = tam_fs_read_file(buf->path, &raw_len);
    if (!raw) {
        if (err_buf && err_size > 0) {
            snprintf(err_buf, err_size, "Cannot read '%s': %s", buf->path, strerror(errno));
        }
        return false;
    }

    char *utf8 = NULL;
    size_t utf8_len = 0;
    char detected[64] = "";
    bool has_bom = false;
    bool fallback = false;

    /* If explicit encoding requested, force conversion via UTF-8/BOM rules if matching or configured */
    bool ok = encoding_decode_buffer(raw, raw_len,
                                     target_enc[0] ? target_enc : buf->encoding,
                                     &utf8, &utf8_len,
                                     detected, sizeof(detected),
                                     &has_bom, &fallback,
                                     err_buf, err_size);
    free(raw);
    if (!ok) return false;

    edit_doc_t new_doc = {0};
    bool crlf = false;
    if (!doc_from_text(&new_doc, utf8, utf8_len, &crlf)) {
        free(utf8);
        doc_clear(&new_doc);
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Out of memory");
        return false;
    }
    free(utf8);

    doc_clear(&buf->doc);
    buf->doc = new_doc;
    buf->crlf = crlf;
    buf->dirty = false;
    undo_stack_free(&buf->undo_stack);
    undo_stack_free(&buf->redo_stack);
    if (target_enc[0] && !fallback) {
        snprintf(buf->encoding, sizeof(buf->encoding), "%s", target_enc);
    } else {
        snprintf(buf->encoding, sizeof(buf->encoding), "%s", detected);
    }
    buf->has_bom = has_bom;
    buf->used_fallback_enc = fallback;
    if (fallback) {
        snprintf(buf->fallback_msg, sizeof(buf->fallback_msg),
                 "Decoded with fallback encoding '%s'", detected);
    } else {
        buf->fallback_msg[0] = '\0';
    }
    record_stat_mtime(buf, buf->path);
    buffer_syntax_free(buf);
    buffer_syntax_init(buf, NULL, detectlimit > 0 ? detectlimit : 100);
    return true;
}

static bool ensure_parent_dir(const char *path, char *err_buf, size_t err_size)
{
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) return true;
    size_t dlen = (size_t)(slash - path);
    char *dir = malloc(dlen + 1);
    if (!dir) return false;
    memcpy(dir, path, dlen);
    dir[dlen] = '\0';
    bool ok = tam_fs_mkdir_p(dir, 0755);
    if (!ok && err_buf && err_size > 0) {
        snprintf(err_buf, err_size, "Cannot create directory '%s': %s", dir, strerror(errno));
    }
    free(dir);
    return ok;
}

bool buffer_save_with_opts(buffer_t *buf, bool rmtrailingws, bool eofnewline,
                           bool mkparents, char *err_buf, size_t err_size)
{
    if (err_buf && err_size > 0) err_buf[0] = '\0';
    if (buf == NULL || buf->path == NULL || buf->path[0] == '\0') {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "No file name");
        return false;
    }
    if (buf->readonly) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Buffer is read-only");
        return false;
    }

    if (mkparents && !ensure_parent_dir(buf->path, err_buf, err_size)) {
        return false;
    }

    if (rmtrailingws) {
        for (size_t i = 0; i < buf->doc.count; i++) {
            size_t len = buf->doc.lens[i];
            while (len > 0 && (buf->doc.lines[i][len - 1] == ' ' ||
                               buf->doc.lines[i][len - 1] == '\t')) {
                len--;
            }
            if (len != buf->doc.lens[i]) {
                buf->doc.lines[i][len] = '\0';
                buf->doc.lens[i] = len;
            }
        }
    }

    tam_str_t out = tam_str_new("");
    if (out == NULL) return false;

    const char *sep = buf->crlf ? "\r\n" : "\n";
    size_t sep_len = strlen(sep);

    for (size_t i = 0; i < buf->doc.count; i++) {
        if (i > 0) {
            out = tam_str_cat_len(out, sep, sep_len);
            if (out == NULL) return false;
        }
        out = tam_str_cat_len(out, buf->doc.lines[i], buf->doc.lens[i]);
        if (out == NULL) return false;
    }

    if (eofnewline && buf->doc.count > 0 && buf->doc.lens[buf->doc.count - 1] > 0) {
        out = tam_str_cat_len(out, sep, sep_len);
        if (out == NULL) return false;
    }

    char *encoded = NULL;
    size_t encoded_len = 0;
    const char *enc = buf->encoding[0] ? buf->encoding : "utf-8";
    if (!encoding_encode_buffer(out, tam_str_len(out), enc, buf->has_bom,
                                &encoded, &encoded_len, err_buf, err_size)) {
        tam_str_free(out);
        return false;
    }
    tam_str_free(out);

    bool ok = tam_fs_write_atomic(buf->path, encoded, encoded_len, 0644);
    if (!ok && err_buf && err_size > 0) {
        snprintf(err_buf, err_size, "Error saving '%s': %s", buf->path, strerror(errno));
    }
    free(encoded);

    if (ok) {
        buf->dirty = false;
        record_stat_mtime(buf, buf->path);
    }
    return ok;
}

bool buffer_save(buffer_t *buf)
{
    return buffer_save_with_opts(buf, false, false, false, NULL, 0);
}

bool buffer_save_to_with_opts(buffer_t *buf, const char *path,
                              bool rmtrailingws, bool eofnewline,
                              bool mkparents, char *err_buf, size_t err_size)
{
    if (buf == NULL || path == NULL || *path == '\0') {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "No file name");
        return false;
    }
    if (buf->readonly) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Buffer is read-only");
        return false;
    }
    free(buf->path);
    buf->path = strdup(path);
    bool ok = buffer_save_with_opts(buf, rmtrailingws, eofnewline, mkparents, err_buf, err_size);
    if (ok && strcmp(buf->filetype, "unknown") == 0) {
        buffer_syntax_init(buf, NULL, 100);
    }
    return ok;
}

bool buffer_save_to(buffer_t *buf, const char *path)
{
    return buffer_save_to_with_opts(buf, path, false, false, false, NULL, 0);
}

size_t buffer_retab(buffer_t *buf, int tabsize, bool tabstospaces)
{
    if (!buf || tabsize < 1) return 0;
    size_t changed = 0;

    for (size_t i = 0; i < buf->doc.count; i++) {
        const char *line = buf->doc.lines[i];
        size_t len = buf->doc.lens[i];
        size_t ws_bytes = 0;
        size_t cols = 0;
        while (ws_bytes < len && (line[ws_bytes] == ' ' || line[ws_bytes] == '\t')) {
            if (line[ws_bytes] == ' ') {
                cols += 1;
            } else {
                cols += (size_t)tabsize - (cols % (size_t)tabsize);
            }
            ws_bytes++;
        }
        if (ws_bytes == 0) continue;

        size_t new_tabs = tabstospaces ? 0 : (cols / (size_t)tabsize);
        size_t new_spaces = tabstospaces ? cols : (cols % (size_t)tabsize);
        size_t new_ws = new_tabs + new_spaces;
        size_t rest_len = len - ws_bytes;

        char *nl = malloc(new_ws + rest_len + 1);
        if (!nl) continue;
        if (new_tabs > 0) memset(nl, '\t', new_tabs);
        if (new_spaces > 0) memset(nl + new_tabs, ' ', new_spaces);
        if (rest_len > 0) memcpy(nl + new_ws, line + ws_bytes, rest_len);
        nl[new_ws + rest_len] = '\0';

        if (new_ws + rest_len != len || memcmp(nl, line, len) != 0) {
            free(buf->doc.lines[i]);
            buf->doc.lines[i] = nl;
            buf->doc.lens[i] = new_ws + rest_len;
            changed++;
        } else {
            free(nl);
        }
    }

    if (changed > 0) {
        buf->dirty = true;
    }
    return changed;
}
