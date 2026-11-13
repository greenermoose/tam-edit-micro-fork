#include "actions.h"
#include "buffer.h"
#include "clipboard.h"
#include "cursor.h"
#include "editor.h"
#include "prompt.h"
#include "render.h"
#include "syntax.h"

#include <tam.h>
#include <tam_key.h>
#include <tam_tty.h>

#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef TAM_EDIT_VERSION
#define TAM_EDIT_VERSION "0.4.0"
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
        "Usage: tam-edit [OPTIONS] [FILE...]\n"
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
        "  Ctrl-T                                    add tab\n"
        "  Alt-, / Alt-.                             previous / next tab\n"
        "  Ctrl-W                                    next split\n"
        "  Alt-W                                     toggle softwrap\n"
        "  Ctrl-R                                    toggle line numbers\n"
        "\n"
        "Options:\n"
        "  +LINE[:COL]                 jump to line (and optional column) on open\n"
        "  -h, --help                  show this help\n"
        "  -v, --version               show the version\n"
        "  --check-config              validate ~/.config/tam-edit/settings.ini\n"
        "  --import-micro-config [DIR] import settings & bindings from micro\n"
        "  --dump-syntax FILE          print per-line syntax highlight spans\n");
}

static int dump_syntax_file(const char *path)
{
    setlocale(LC_CTYPE, "");
    buffer_t *b = buffer_open(path);
    if (b == NULL) {
        fprintf(stderr, "tam-edit: cannot open '%s'\n", path);
        syntax_registry_free();
        return 1;
    }
    b->syntax_enabled = true;
    buffer_syntax_update_to(b, b->doc.count);

    printf("filetype:%s\n", b->filetype);
    for (size_t l = 0; l < b->doc.count; l++) {
        size_t nrunes = 0;
        const uint16_t *groups = buffer_syntax_get_line_groups(b, l, &nrunes);
        printf("%zu:", l);
        if (groups != NULL && nrunes > 0) {
            size_t run_start = 0;
            uint16_t cur = groups[0];
            for (size_t r = 1; r <= nrunes; r++) {
                bool boundary = (r == nrunes) || (groups[r] != cur);
                if (boundary) {
                    const char *gname = (cur == 0) ? "default" : syntax_group_name(cur);
                    if (gname == NULL) gname = "default";
                    printf(" %zu-%zu=%s", run_start, r, gname);
                    if (r < nrunes) {
                        run_start = r;
                        cur = groups[r];
                    }
                }
            }
        }
        putchar('\n');
    }

    buffer_unref(b);
    syntax_registry_free();
    return 0;
}

/* Parse SGR mouse escape sequences: \x1b[<btn;col;row[M|m] */
static bool handle_mouse_sgr(editor_t *ed, const unsigned char *buf, size_t len,
                             size_t *consumed, int tty_rows)
{
    (void)tty_rows;
    if (len < 6 || buf[0] != '\x1b' || buf[1] != '[' || buf[2] != '<') {
        return false;
    }

    size_t end = 3;
    while (end < len && buf[end] != 'M' && buf[end] != 'm') end++;
    if (end >= len) return false;

    char end_char = (char)buf[end];
    *consumed = end + 1;

    int btn = 0, x = 0, y = 0;
    if (sscanf((const char *)buf + 3, "%d;%d;%d", &btn, &x, &y) != 3) {
        return true;
    }

    /* Check click on Tab Bar (row 1 in 1-based coordinates) */
    bool show_tabbar = (ed->config.tabbar == TABBAR_ON) ||
                       (ed->config.tabbar == TABBAR_AUTO && ed->tabs.count > 1);
    if (show_tabbar && y == 1 && end_char == 'M' && btn == 0) {
        int col = 1;
        for (size_t i = 0; i < ed->tabs.count; i++) {
            tab_t *t = ed->tabs.tabs[i];
            char title[128];
            tab_get_title(t, title, sizeof(title));
            int tab_w = (int)strlen(title) + (tab_is_dirty(t) ? 6 : 4);
            if (x >= col && x < col + tab_w) {
                ed->tabs.active = i;
                editor_set_active_view(ed, t->active_view);
                return true;
            }
            col += tab_w + 1;
        }
        return true;
    }

    /* Locate clicked split pane */
    if (ed->tabs.count == 0 || ed->tabs.active >= ed->tabs.count) return true;
    tab_t *cur_tab = ed->tabs.tabs[ed->tabs.active];
    view_t *hit = pane_hit_test(cur_tab->root, x - 1, y - 1);
    if (hit != NULL) {
        if (hit != ed->active_view) {
            editor_set_active_view(ed, hit);
        }
    }

    if (btn == 64) {
        /* Wheel up */
        cursor_move(ed, 0, -3, false);
        return true;
    }
    if (btn == 65) {
        /* Wheel down */
        cursor_move(ed, 0, 3, false);
        return true;
    }

    if (hit == NULL || hit->buf == NULL) return true;

    int gw = 0;
    if (ed->config.ruler) {
        gw = 4;
        if (hit->buf->doc.count >= 1000) gw = 5;
        if (hit->buf->doc.count >= 10000) gw = 6;
        if (gw >= hit->w / 2) gw = 0;
    }

    int rel_x = x - 1 - hit->x - gw;
    int rel_y = y - 1 - hit->y;
    int max_text_rows = hit->h > 1 ? hit->h - 1 : 1;
    if (rel_y < 0) rel_y = 0;
    if (rel_y >= max_text_rows) rel_y = max_text_rows - 1;
    if (rel_x < 0) rel_x = 0;

    size_t doc_l = hit->top_line + (size_t)rel_y;
    if (doc_l >= hit->buf->doc.count) {
        doc_l = hit->buf->doc.count > 0 ? hit->buf->doc.count - 1 : 0;
    }

    size_t rune_col = 0;
    if (doc_l < hit->buf->doc.count) {
        if (!hit->softwrap) {
            int target_visual = rel_x + (int)hit->left_col;
            rune_col = edit_visual_to_rune(hit->buf->doc.lines[doc_l],
                                           hit->buf->doc.lens[doc_l],
                                           target_visual);
        } else {
            int tabsize = ed->config.tabsize > 0 ? ed->config.tabsize : 4;
            int text_cols = hit->w > gw ? hit->w - gw : 80;
            rune_col = edit_softwrap_vloc_to_rune(hit->buf->doc.lines[doc_l],
                                                  hit->buf->doc.lens[doc_l],
                                                  0, rel_x, text_cols, tabsize,
                                                  hit->wordwrap);
        }
    }

    if (end_char == 'M') {
        if (btn == 16) {
            /* Ctrl + Left click -> toggle multi-cursor */
            editor_toggle_cursor_at(ed, doc_l, rune_col);
            return true;
        }
        if (btn == 0) {
            /* Left click */
            editor_remove_all_cursors(ed);
            ed->cx_line  = doc_l;
            ed->cx_col   = rune_col;
            ed->goal_col = rune_col;
            cursor_clamp(ed);
            ed->sel.active = false;
            editor_sync_to_active(ed);
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
            editor_sync_to_active(ed);
            return true;
        }
    }

    return true;
}

int main(int argc, char **argv)
{
    const char *files[64];
    size_t file_count = 0;
    long init_line = 0;
    long init_col = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("tam-edit %s (libtam %s)\n", TAM_EDIT_VERSION, tam_version());
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--check-config") == 0) {
            const char *cfg_path = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : NULL;
            bool ok = config_check(cfg_path, stdout);
            return ok ? 0 : 1;
        }
        if (strcmp(argv[i], "--import-micro-config") == 0) {
            const char *m_dir = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : NULL;
            bool ok = config_import_micro(m_dir, NULL, stdout);
            return ok ? 0 : 1;
        }
        if (strcmp(argv[i], "--dump-syntax") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "tam-edit: --dump-syntax requires a file path\n");
                return 2;
            }
            return dump_syntax_file(argv[++i]);
        }
        if (argv[i][0] == '+' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            char *colon = NULL;
            long ln = strtol(argv[i] + 1, &colon, 10);
            if (ln > 0) init_line = ln;
            if (colon && *colon == ':' && colon[1] >= '0' && colon[1] <= '9') {
                long cl = strtol(colon + 1, NULL, 10);
                if (cl > 0) init_col = cl;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "tam-edit: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
        if (file_count < 64) {
            files[file_count++] = argv[i];
        }
    }

    setlocale(LC_CTYPE, "");

    editor_t ed;
    editor_init(&ed);
    actions_init();
    clipboard_init();

    if (file_count > 0) {
        if (!editor_load(&ed, files[0])) {
            fprintf(stderr, "tam-edit: %s\n", ed.message);
            editor_free(&ed);
            syntax_registry_free();
            return 1;
        }
        for (size_t f = 1; f < file_count; f++) {
            editor_add_tab(&ed, files[f]);
        }
        ed.tabs.active = 0;
        editor_set_active_view(&ed, ed.tabs.tabs[0]->active_view);
        if (init_line > 0) {
            size_t ln = (size_t)(init_line - 1);
            if (ln >= ed.doc.count) ln = ed.doc.count > 0 ? ed.doc.count - 1 : 0;
            ed.cx_line = ln;
            size_t max_col = edit_line_runes(ed.doc.lines[ln], ed.doc.lens[ln]);
            size_t cl = (init_col > 0) ? (size_t)(init_col - 1) : 0;
            if (cl > max_col) cl = max_col;
            ed.cx_col = cl;
            ed.goal_col = cl;
            editor_sync_to_active(&ed);
        }
    }

    tam_tty_t tty = {0};
    if (tam_tty_open(&tty, STDIN_FILENO, STDOUT_FILENO) < 0 ||
        !tty.interactive) {
        fprintf(stderr,
                "tam-edit: standard input and output must be a terminal\n");
        editor_free(&ed);
        syntax_registry_free();
        return 1;
    }

    ed.tty = &tty;
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
            tam_tty_size(&tty, &tty.rows, &tty.cols);
            ren.force_redraw = true;
            need_draw = true;
        }

        if (!ed.prompt.active && ed.active_view && ed.active_view->buf &&
            strcmp(ed.config.reload, "disabled") != 0 &&
            buffer_check_external_change(ed.active_view->buf)) {
            if (strcmp(ed.config.reload, "auto") == 0 && !ed.dirty) {
                editor_reopen(&ed, NULL);
                need_draw = true;
            } else if (strcmp(ed.config.reload, "prompt") == 0) {
                prompt_open_confirm_reload(&ed, ed.path);
                need_draw = true;
            }
        }

        if (need_draw) {
            render_draw(&ren, &ed, &tty);
            need_draw = false;
        }

        int timeout_ms = -1;
        if (ed.bg_job.active) {
            if (shell_bg_check_timeout(&ed.bg_job, &timeout_ms)) {
                editor_set_message(&ed, "Command timed out: %s", ed.bg_job.cmd_desc);
                need_draw = true;
                continue;
            }
        }
        if (tam_key_pending(&keys) > 0) {
            if (timeout_ms < 0 || timeout_ms > 50) timeout_ms = 50;
        }

        struct pollfd fds[2];
        nfds_t nfds = 1;
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        if (ed.bg_job.active && ed.bg_job.pipe_fd >= 0) {
            fds[1].fd = ed.bg_job.pipe_fd;
            fds[1].events = POLLIN;
            fds[1].revents = 0;
            nfds = 2;
        }

        int prc = poll(fds, nfds, timeout_ms);
        if (prc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (prc == 0) {
            if (tam_key_pending(&keys) > 0) {
                tam_key_event_t ev;
                while (tam_key_timeout(&keys, &ev) == 1) {
                    if (ed.prompt.active) {
                        prompt_handle_key(&ed, &ev);
                    } else {
                        actions_handle_key(&ed, &ev, render_text_rows(&tty));
                    }
                    need_draw = true;
                    if (!ed.running || g_stop) break;
                }
            }
            continue;
        }

        if (nfds == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (!shell_bg_poll_read(&ed.bg_job)) {
                if (ed.bg_job.timed_out) {
                    editor_set_message(&ed, "Command timed out: %s", ed.bg_job.cmd_desc);
                } else if (ed.bg_job.out_len > 0) {
                    bool multiline = memchr(ed.bg_job.output, '\n', ed.bg_job.out_len) != NULL;
                    if (multiline || ed.bg_job.out_len > 120) {
                        editor_open_scratch(&ed, "[run]", ed.bg_job.output, true);
                        editor_set_message(&ed, "Finished (%d): %s",
                                           ed.bg_job.exit_code, ed.bg_job.cmd_desc);
                    } else {
                        size_t mlen = ed.bg_job.out_len;
                        while (mlen > 0 && (ed.bg_job.output[mlen - 1] == '\n' ||
                                            ed.bg_job.output[mlen - 1] == '\r')) {
                            mlen--;
                        }
                        ed.bg_job.output[mlen] = '\0';
                        editor_set_message(&ed, "(%d) %s",
                                           ed.bg_job.exit_code, ed.bg_job.output);
                    }
                } else {
                    editor_set_message(&ed, "Command exited (%d)", ed.bg_job.exit_code);
                }
                need_draw = true;
            }
        }

        if (!(fds[0].revents & POLLIN)) {
            continue;
        }

        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) continue;

        size_t b_len = (size_t)n;
        size_t b_idx = 0;

        while (b_idx < b_len) {
            /* Bracketed paste check */
            if (!in_paste && b_len - b_idx >= 6 &&
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
            if (handle_mouse_sgr(&ed, buf + b_idx, b_len - b_idx, &consumed, tty.rows)) {
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
    syntax_registry_free();

    if (g_stop) {
        static const char note[] = "tam-edit: terminated\n";
        ssize_t ignored = write(STDOUT_FILENO, note, sizeof(note) - 1);
        (void)ignored;
    }

    return 0;
}
