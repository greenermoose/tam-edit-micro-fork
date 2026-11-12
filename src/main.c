#include "actions.h"
#include "clipboard.h"
#include "cursor.h"
#include "editor.h"
#include "prompt.h"
#include "render.h"

#include <tam.h>
#include <tam_key.h>
#include <tam_tty.h>

#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef TAM_EDIT_VERSION
#define TAM_EDIT_VERSION "0.1.0"
#endif

static volatile sig_atomic_t g_resized;
static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_suspend;

static void on_winch(int sig)
{
    (void)sig;
    g_resized = 1;
}

static void on_stop(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void on_tstp(int sig)
{
    (void)sig;
    g_suspend = 1;
}

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);

    sa.sa_handler = on_stop;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    sa.sa_handler = on_tstp;
    sigaction(SIGTSTP, &sa, NULL);
}

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: tam-edit [OPTIONS] [FILE]\n"
        "\n"
        "A small, fast terminal text editor built on libtam.\n"
        "\n"
        "Keybindings:\n"
        "  Arrows, Home, End, PageUp, PageDown       move cursor\n"
        "  Shift + Move                              select text\n"
        "  Ctrl-Left, Ctrl-Right                     move by word\n"
        "  Enter, Tab, Backspace, Delete             edit\n"
        "  Ctrl-Z, Ctrl-Y                            undo / redo\n"
        "  Ctrl-C, Ctrl-X, Ctrl-V                    copy / cut / paste\n"
        "  Ctrl-A                                    select all\n"
        "  Ctrl-S                                    save\n"
        "  Ctrl-Q                                    quit\n"
        "  Ctrl-F, Ctrl-N, Ctrl-P                    find / next / previous\n"
        "  Ctrl-E                                    command prompt\n"
        "  Ctrl-L                                    go to line\n"
        "\n"
        "Options:\n"
        "  -h, --help       show this help\n"
        "  -v, --version    show the version\n");
}

/* Parse SGR mouse escape sequences: \x1b[<btn;col;row[M|m] */
static bool handle_mouse_sgr(editor_t *ed, const unsigned char *buf, size_t len,
                             size_t *consumed, int text_rows)
{
    if (len < 6 || buf[0] != '\x1b' || buf[1] != '[' || buf[2] != '<') {
        return false;
    }

    /* Find terminating 'M' or 'm' */
    size_t end = 3;
    while (end < len && buf[end] != 'M' && buf[end] != 'm') end++;
    if (end >= len) return false;

    char end_char = (char)buf[end];
    *consumed = end + 1;

    int btn = 0, x = 0, y = 0;
    if (sscanf((const char *)buf + 3, "%d;%d;%d", &btn, &x, &y) != 3) {
        return true;
    }

    if (btn == 64) {
        /* Wheel up: scroll up 3 lines */
        cursor_move(ed, 0, -3, false);
        return true;
    }
    if (btn == 65) {
        /* Wheel down: scroll down 3 lines */
        cursor_move(ed, 0, 3, false);
        return true;
    }

    int screen_r = y - 1;
    int screen_c = x - 1;
    if (screen_r < 0) screen_r = 0;
    if (screen_r >= text_rows) screen_r = text_rows - 1;

    size_t doc_l = ed->top_line + (size_t)screen_r;
    if (doc_l >= ed->doc.count) {
        doc_l = ed->doc.count > 0 ? ed->doc.count - 1 : 0;
    }

    size_t rune_col = 0;
    if (doc_l < ed->doc.count) {
        int target_visual = screen_c + (int)ed->left_col;
        rune_col = edit_visual_to_rune(ed->doc.lines[doc_l],
                                       ed->doc.lens[doc_l],
                                       target_visual);
    }

    if (end_char == 'M') {
        if (btn == 0) {
            /* Left click */
            ed->cx_line  = doc_l;
            ed->cx_col   = rune_col;
            ed->goal_col = rune_col;
            cursor_clamp(ed);
            ed->sel.active = false;
            return true;
        } else if (btn == 32) {
            /* Left drag */
            if (!ed->sel.active) {
                ed->sel.active = true;
                ed->sel.anchor = (loc_t){ed->cx_line, ed->cx_col};
            }
            ed->cx_line  = doc_l;
            ed->cx_col   = rune_col;
            ed->goal_col = rune_col;
            cursor_clamp(ed);
            ed->sel.cursor = (loc_t){ed->cx_line, ed->cx_col};
            return true;
        }
    }

    return true;
}

int main(int argc, char **argv)
{
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("tam-edit %s (libtam %s)\n", TAM_EDIT_VERSION, tam_version());
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "tam-edit: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
        if (path != NULL) {
            fprintf(stderr, "tam-edit: only one file may be opened\n");
            return 2;
        }
        path = argv[i];
    }

    setlocale(LC_CTYPE, "");

    editor_t ed;
    editor_init(&ed);
    actions_init();
    clipboard_init();

    if (path != NULL) {
        bool existed = tam_fs_file_exists(path);
        if (!editor_load(&ed, path)) {
            fprintf(stderr, "tam-edit: %s\n", ed.message);
            editor_free(&ed);
            return 1;
        }
        if (!existed) editor_set_message(&ed, "New file");
    }

    tam_tty_t tty = {0};
    if (tam_tty_open(&tty, STDIN_FILENO, STDOUT_FILENO) < 0 ||
        !tty.interactive) {
        fprintf(stderr,
                "tam-edit: standard input and output must be a terminal\n");
        editor_free(&ed);
        return 1;
    }

    ed.tty_out_fd = tty.out_fd;

    install_signals();

    tam_tty_alt_screen(&tty, true);
    tam_tty_raw(&tty, true);
    tam_tty_paste(&tty, true);
    tam_tty_mouse(&tty, true);

    renderer_t ren;
    renderer_init(&ren);

    tam_key_parser_t keys;
    tam_key_init(&keys);

    bool need_draw = true;
    bool in_paste  = false;
    tam_str_t paste_buf = NULL;
    unsigned char buf[512];

    while (ed.running && !g_stop) {
        if (g_suspend) {
            g_suspend = 0;
            tam_tty_mouse(&tty, false);
            tam_tty_paste(&tty, false);
            tam_tty_alt_screen(&tty, false);
            tam_tty_raw(&tty, false);
            tam_tty_cursor_visible(&tty, true);

            signal(SIGTSTP, SIG_DFL);
            raise(SIGTSTP);

            /* Resume */
            install_signals();
            tam_tty_alt_screen(&tty, true);
            tam_tty_raw(&tty, true);
            tam_tty_paste(&tty, true);
            tam_tty_mouse(&tty, true);
            ren.force_redraw = true;
            need_draw = true;
        }

        if (g_resized) {
            g_resized = 0;
            tam_tty_size(&tty, NULL, NULL);
            ren.force_redraw = true;
            need_draw = true;
        }

        if (need_draw) {
            render_draw(&ren, &ed, &tty);
            need_draw = false;
        }

        int timeout = tam_key_pending(&keys) > 0 ? 20 : -1;
        int n = tam_tty_read(&tty, buf, sizeof(buf), timeout);

        if (n < 0) break;
        if (n == 0) {
            tam_key_event_t ev;
            if (tam_key_timeout(&keys, &ev)) {
                if (ed.prompt.active) {
                    prompt_handle_key(&ed, &ev);
                } else {
                    actions_handle_key(&ed, &ev, render_text_rows(&tty));
                }
                need_draw = true;
            }
            continue;
        }

        /* Bracketed paste check */
        size_t b_idx = 0;
        size_t b_len = (size_t)n;

        while (b_idx < b_len) {
            if (!in_paste && b_idx + 6 <= b_len &&
                memcmp(buf + b_idx, "\x1b[200~", 6) == 0) {
                in_paste = true;
                b_idx += 6;
                paste_buf = tam_str_new("");
                continue;
            }

            if (in_paste) {
                const char *end_paste = memmem(buf + b_idx, b_len - b_idx,
                                               "\x1b[201~", 6);
                if (end_paste != NULL) {
                    size_t chunk_len = (const unsigned char *)end_paste - (buf + b_idx);
                    if (chunk_len > 0) {
                        paste_buf = tam_str_cat_len(paste_buf, buf + b_idx, chunk_len);
                    }
                    if (paste_buf != NULL) {
                        editor_insert_text(&ed, paste_buf, tam_str_len(paste_buf));
                        tam_str_free(paste_buf);
                        paste_buf = NULL;
                    }
                    in_paste = false;
                    b_idx = ((const unsigned char *)end_paste - buf) + 6;
                    need_draw = true;
                    continue;
                } else {
                    paste_buf = tam_str_cat_len(paste_buf, buf + b_idx, b_len - b_idx);
                    b_idx = b_len;
                    continue;
                }
            }

            /* SGR Mouse check */
            size_t consumed = 0;
            if (handle_mouse_sgr(&ed, buf + b_idx, b_len - b_idx, &consumed,
                                 render_text_rows(&tty))) {
                b_idx += consumed;
                need_draw = true;
                continue;
            }

            /* Feed byte into key decoder */
            tam_key_feed(&keys, buf + b_idx, 1);
            b_idx++;

            tam_key_event_t ev;
            while (tam_key_next(&keys, &ev) == 1) {
                if (ed.prompt.active) {
                    prompt_handle_key(&ed, &ev);
                } else {
                    actions_handle_key(&ed, &ev, render_text_rows(&tty));
                }
                need_draw = true;
                if (!ed.running || g_stop) break;
            }
        }
    }

    tam_str_free(paste_buf);
    tam_tty_mouse(&tty, false);
    tam_tty_paste(&tty, false);
    tam_tty_alt_screen(&tty, false);
    tam_tty_raw(&tty, false);
    tam_tty_close(&tty);

    renderer_free(&ren);
    editor_free(&ed);
    clipboard_free();
    actions_free();

    if (g_stop) {
        static const char note[] = "tam-edit: terminated\n";
        ssize_t ignored = write(STDOUT_FILENO, note, sizeof(note) - 1);
        (void)ignored;
    }

    return 0;
}
