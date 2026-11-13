#include "buffer.h"
#include "syntax.h"
#include "undo.h"

#include <tam.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* Empty buffer has one empty line */
    doc_insert_line(&b->doc, 0, "", 0);
    buffer_syntax_init(b, NULL, 100);
    return b;
}

buffer_t *buffer_open(const char *path)
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

    size_t len = 0;
    char *data = tam_fs_read_file(path, &len);
    if (data == NULL) {
        if (errno == ENOENT) {
            /* Missing file opens empty without error */
            doc_insert_line(&b->doc, 0, "", 0);
            buffer_syntax_init(b, NULL, 100);
            return b;
        }
        free(b->path);
        free(b);
        return NULL;
    }

    bool crlf = false;
    bool ok = doc_from_text(&b->doc, data, len, &crlf);
    free(data);

    if (!ok) {
        doc_clear(&b->doc);
        free(b->path);
        free(b);
        return NULL;
    }

    b->crlf = crlf;
    buffer_syntax_init(b, NULL, 100);
    return b;
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

bool buffer_save(buffer_t *buf)
{
    if (buf == NULL || buf->path == NULL || buf->path[0] == '\0') {
        return false;
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

    bool ok = tam_fs_write_atomic(buf->path, out, tam_str_len(out), 0644);
    tam_str_free(out);

    if (ok) {
        buf->dirty = false;
    }
    return ok;
}

bool buffer_save_to(buffer_t *buf, const char *path)
{
    if (buf == NULL || path == NULL || *path == '\0') return false;
    free(buf->path);
    buf->path = strdup(path);
    bool ok = buffer_save(buf);
    if (ok && strcmp(buf->filetype, "unknown") == 0) {
        buffer_syntax_init(buf, NULL, 100);
    }
    return ok;
}
