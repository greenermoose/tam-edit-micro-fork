#include "clipboard.h"

#include <tam.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char  *g_internal_clip = NULL;
static size_t g_internal_len  = 0;

void clipboard_init(void)
{
    g_internal_clip = NULL;
    g_internal_len = 0;
}

void clipboard_free(void)
{
    free(g_internal_clip);
    g_internal_clip = NULL;
    g_internal_len = 0;
}

static char *base64_encode(const unsigned char *data, size_t input_len)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t output_len = 4 * ((input_len + 2) / 3);
    char *encoded = malloc(output_len + 1);
    if (encoded == NULL) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < input_len;) {
        uint32_t octet_a = i < input_len ? data[i++] : 0;
        uint32_t octet_b = i < input_len ? data[i++] : 0;
        uint32_t octet_c = i < input_len ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        encoded[j++] = table[(triple >> 18) & 0x3F];
        encoded[j++] = table[(triple >> 12) & 0x3F];
        encoded[j++] = (i > input_len + 1) ? '=' : table[(triple >> 6) & 0x3F];
        encoded[j++] = (i > input_len)     ? '=' : table[triple & 0x3F];
    }
    encoded[output_len] = '\0';
    return encoded;
}

bool clipboard_set(const char *text, size_t len, int tty_fd)
{
    if (text == NULL) return false;

    /* 1. Internal buffer */
    char *copy = malloc(len + 1);
    if (copy != NULL) {
        memcpy(copy, text, len);
        copy[len] = '\0';
        free(g_internal_clip);
        g_internal_clip = copy;
        g_internal_len = len;
    }

    /* 2. OSC 52 sequence to the terminal */
    if (tty_fd >= 0) {
        char *b64 = base64_encode((const unsigned char *)text, len);
        if (b64 != NULL) {
            tam_str_t osc = tam_str_new("\x1b]52;c;");
            if (osc != NULL) {
                osc = tam_str_cat(osc, b64);
                osc = tam_str_cat(osc, "\x07");
                if (osc != NULL) {
                    ssize_t ignored = write(tty_fd, osc, tam_str_len(osc));
                    (void)ignored;
                }
                tam_str_free(osc);
            }
            free(b64);
        }
    }

    /* 3. External clipboard helpers if available */
    if (getenv("WAYLAND_DISPLAY") != NULL) {
        const char *const argv[] = {"wl-copy", NULL};
        tam_exec_opts_t opts = {
            .timeout_ms = 300,
            .input      = text,
            .input_len  = len,
            .max_output = 1024,
        };
        tam_exec_result_t res = tam_exec_opts(argv, &opts);
        tam_exec_result_free(&res);
    } else if (getenv("DISPLAY") != NULL) {
        const char *const argv[] = {"xclip", "-selection", "clipboard", NULL};
        tam_exec_opts_t opts = {
            .timeout_ms = 300,
            .input      = text,
            .input_len  = len,
            .max_output = 1024,
        };
        tam_exec_result_t res = tam_exec_opts(argv, &opts);
        tam_exec_result_free(&res);
    }

    return true;
}

char *clipboard_get(size_t *out_len)
{
    /* 1. Try Wayland wl-paste */
    if (getenv("WAYLAND_DISPLAY") != NULL) {
        const char *const argv[] = {"wl-paste", "--no-newline", NULL};
        tam_exec_result_t res = tam_exec(argv, 300);
        if (res.exit_code == 0 && res.stdout_buf != NULL && res.stdout_len > 0) {
            char *ret = strdup(res.stdout_buf);
            if (out_len != NULL) *out_len = res.stdout_len;
            tam_exec_result_free(&res);
            return ret;
        }
        tam_exec_result_free(&res);
    }

    /* 2. Try X11 xclip */
    if (getenv("DISPLAY") != NULL) {
        const char *const argv[] = {"xclip", "-out", "-selection", "clipboard", NULL};
        tam_exec_result_t res = tam_exec(argv, 300);
        if (res.exit_code == 0 && res.stdout_buf != NULL && res.stdout_len > 0) {
            char *ret = strdup(res.stdout_buf);
            if (out_len != NULL) *out_len = res.stdout_len;
            tam_exec_result_free(&res);
            return ret;
        }
        tam_exec_result_free(&res);
    }

    /* 3. Fallback to internal buffer */
    if (g_internal_clip != NULL) {
        char *ret = strdup(g_internal_clip);
        if (out_len != NULL) *out_len = g_internal_len;
        return ret;
    }

    if (out_len != NULL) *out_len = 0;
    return NULL;
}
