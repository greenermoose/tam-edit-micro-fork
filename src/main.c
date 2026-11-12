#include "editor.h"
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

/* Injected by the Makefile from its VERSION variable. */
#ifndef TAM_EDIT_VERSION
#define TAM_EDIT_VERSION "0.0.0-unknown"
#endif

static volatile sig_atomic_t g_resized;
static volatile sig_atomic_t g_stop;

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
}

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: tam-edit [OPTIONS] [FILE]\n"
        "\n"
        "A small terminal text editor built on libtam.\n"
        "\n"
        "Keys:\n"
        "  Arrow keys, Home, End, PageUp, PageDown   move\n"
        "  Enter, Tab, Backspace, Delete             edit\n"
        "  Ctrl-S                                    save\n"
        "  Ctrl-Q                                    quit (twice when modified)\n"
        "\n"
        "Options:\n"
        "  -h, --help       show this help\n"
        "  -v, --version    show the version\n");
}

static bool handle_key(editor_t *ed, const tam_key_event_t *ev, int text_rows)
{
    bool was_armed = ed->quit_armed;
    ed->quit_armed = false;

    switch (ev->type) {
    case TAM_KEY_CHAR:
        if (ev->mods & TAM_MOD_CTRL) {
            uint32_t cp = ev->codepoint;
            if (cp == 's') {
                editor_save(ed);
                return true;
            }
            if (cp == 'q') {
                if (ed->dirty && !was_armed) {
                    editor_set_message(ed,
                        "Unsaved changes: Ctrl-Q again to quit, Ctrl-S to save");
                    ed->quit_armed = true;
                    return true;
                }
                ed->running = false;
                return true;
            }
            return false;
        }
        if (ev->mods == 0 && ev->codepoint >= 0x20 && ev->codepoint != 0x7F) {
            editor_insert_codepoint(ed, ev->codepoint);
            return true;
        }
        return false;

    case TAM_KEY_ENTER:
        editor_insert_newline(ed);
        return true;
    case TAM_KEY_TAB:
        editor_insert_tab(ed);
        return true;
    case TAM_KEY_BACKSPACE:
        editor_backspace(ed);
        return true;
    case TAM_KEY_DELETE:
        editor_delete(ed);
        return true;

    case TAM_KEY_UP:
        editor_move(ed, 0, -1);
        return true;
    case TAM_KEY_DOWN:
        editor_move(ed, 0, 1);
        return true;
    case TAM_KEY_LEFT:
        editor_move(ed, -1, 0);
        return true;
    case TAM_KEY_RIGHT:
        editor_move(ed, 1, 0);
        return true;
    case TAM_KEY_HOME:
        editor_home(ed);
        return true;
    case TAM_KEY_END:
        editor_end(ed);
        return true;
    case TAM_KEY_PAGE_UP:
        editor_page(ed, -1, text_rows);
        return true;
    case TAM_KEY_PAGE_DOWN:
        editor_page(ed, 1, text_rows);
        return true;

    case TAM_KEY_ESC:
    case TAM_KEY_INSERT:
    case TAM_KEY_F1:
    case TAM_KEY_F2:
    case TAM_KEY_F3:
    case TAM_KEY_F4:
    case TAM_KEY_F5:
    case TAM_KEY_F6:
    case TAM_KEY_F7:
    case TAM_KEY_F8:
    case TAM_KEY_F9:
    case TAM_KEY_F10:
    case TAM_KEY_F11:
    case TAM_KEY_F12:
    case TAM_KEY_NONE:
    default:
        return false;
    }
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

    /* Unicode width and character data come from the C library locale. */
    setlocale(LC_CTYPE, "");

    editor_t ed;
    editor_init(&ed);

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

    install_signals();

    tam_tty_alt_screen(&tty, true);
    tam_tty_raw(&tty, true);
    tam_tty_paste(&tty, true);

    tam_key_parser_t keys;
    tam_key_init(&keys);

    bool need_draw = true;
    unsigned char buf[256];

    while (ed.running && !g_stop) {
        if (g_resized) {
            g_resized = 0;
            tam_tty_size(&tty, NULL, NULL);
            need_draw = true;
        }

        if (need_draw) {
            render_draw(&ed, &tty);
            need_draw = false;
        }

        /* A lone ESC is a key of its own, but only after a short wait for a
         * sequence that may still be arriving. */
        int timeout = tam_key_pending(&keys) > 0 ? 20 : -1;
        int n = tam_tty_read(&tty, buf, sizeof(buf), timeout);

        if (n < 0) break;
        if (n == 0) {
            tam_key_event_t ev;
            if (tam_key_timeout(&keys, &ev)) {
                handle_key(&ed, &ev, render_text_rows(&tty));
                need_draw = true;
            }
            continue;
        }

        if (tam_key_feed(&keys, buf, (size_t)n) < 0) {
            /* The pending sequence grew past the buffer; drop it. */
            tam_key_init(&keys);
            continue;
        }

        tam_key_event_t ev;
        while (tam_key_next(&keys, &ev) == 1) {
            handle_key(&ed, &ev, render_text_rows(&tty));
            need_draw = true;
            if (!ed.running || g_stop) break;
        }
    }

    tam_tty_paste(&tty, false);
    tam_tty_alt_screen(&tty, false);
    tam_tty_raw(&tty, false);
    tam_tty_close(&tty);
    editor_free(&ed);

    if (g_stop) {
        static const char note[] = "tam-edit: terminated\n";
        ssize_t ignored = write(STDOUT_FILENO, note, sizeof(note) - 1);
        (void)ignored;
    }

    return 0;
}
