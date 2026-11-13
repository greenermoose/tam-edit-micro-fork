#include "shell.h"

#include "tam.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void shell_split_free(char **argv, int argc) {
    if (!argv) return;
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

bool shell_split(const char *cmd, char ***out_argv, int *out_argc) {
    *out_argv = NULL;
    *out_argc = 0;
    if (!cmd) return true;

    int cap = 16;
    char **argv = calloc((size_t)cap, sizeof(char *));
    if (!argv) return false;
    int argc = 0;

    const char *p = cmd;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        size_t tcap = 64;
        size_t tlen = 0;
        char *tok = malloc(tcap);
        if (!tok) {
            shell_split_free(argv, argc);
            return false;
        }

        while (*p && !isspace((unsigned char)*p)) {
            char c = *p;
            if (c == '\'') {
                p++;
                while (*p && *p != '\'') {
                    if (tlen + 2 >= tcap) {
                        tcap *= 2;
                        char *nt = realloc(tok, tcap);
                        if (!nt) {
                            free(tok);
                            shell_split_free(argv, argc);
                            return false;
                        }
                        tok = nt;
                    }
                    tok[tlen++] = *p++;
                }
                if (*p != '\'') {
                    free(tok);
                    shell_split_free(argv, argc);
                    return false;
                }
                p++;
            } else if (c == '"') {
                p++;
                while (*p && *p != '"') {
                    char ch = *p++;
                    if (ch == '\\' && *p) {
                        char nx = *p;
                        if (nx == '"' || nx == '\\' || nx == '$' || nx == '`') {
                            ch = nx;
                            p++;
                        }
                    }
                    if (tlen + 2 >= tcap) {
                        tcap *= 2;
                        char *nt = realloc(tok, tcap);
                        if (!nt) {
                            free(tok);
                            shell_split_free(argv, argc);
                            return false;
                        }
                        tok = nt;
                    }
                    tok[tlen++] = ch;
                }
                if (*p != '"') {
                    free(tok);
                    shell_split_free(argv, argc);
                    return false;
                }
                p++;
            } else if (c == '\\') {
                p++;
                if (*p) {
                    if (tlen + 2 >= tcap) {
                        tcap *= 2;
                        char *nt = realloc(tok, tcap);
                        if (!nt) {
                            free(tok);
                            shell_split_free(argv, argc);
                            return false;
                        }
                        tok = nt;
                    }
                    tok[tlen++] = *p++;
                }
            } else {
                if (tlen + 2 >= tcap) {
                    tcap *= 2;
                    char *nt = realloc(tok, tcap);
                    if (!nt) {
                        free(tok);
                        shell_split_free(argv, argc);
                        return false;
                    }
                    tok = nt;
                }
                tok[tlen++] = *p++;
            }
        }

        tok[tlen] = '\0';
        if (argc + 2 >= cap) {
            cap *= 2;
            char **na = realloc(argv, (size_t)cap * sizeof(char *));
            if (!na) {
                free(tok);
                shell_split_free(argv, argc);
                return false;
            }
            argv = na;
        }
        argv[argc++] = tok;
        argv[argc] = NULL;
    }

    *out_argv = argv;
    *out_argc = argc;
    return true;
}

bool shell_needs_sh(const char *cmd) {
    if (!cmd) return false;
    bool in_sq = false;
    bool in_dq = false;
    for (const char *p = cmd; *p; p++) {
        char c = *p;
        if (c == '\\' && !in_sq && p[1]) {
            p++;
            continue;
        }
        if (c == '\'' && !in_dq) {
            in_sq = !in_sq;
            continue;
        }
        if (c == '"' && !in_sq) {
            in_dq = !in_dq;
            continue;
        }
        if (!in_sq && !in_dq) {
            if (strchr("|&;<>()$`*?[]~", c) != NULL) {
                return true;
            }
        }
        if (in_dq && (c == '$' || c == '`')) {
            return true;
        }
    }
    return false;
}

bool shell_textfilter(const char *cmd,
                      const char *input, size_t input_len,
                      int timeout_ms,
                      char **out_text, size_t *out_len,
                      char *err_buf, size_t err_size) {
    *out_text = NULL;
    *out_len = 0;
    if (err_buf && err_size > 0) err_buf[0] = '\0';
    if (!cmd || !cmd[0]) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "textfilter: missing command");
        return false;
    }

    char **split_argv = NULL;
    int split_argc = 0;
    const char *sh_argv[4] = { "sh", "-c", cmd, NULL };
    const char *const *argv = NULL;

    if (!shell_needs_sh(cmd) && shell_split(cmd, &split_argv, &split_argc) && split_argc > 0) {
        argv = (const char *const *)split_argv;
    } else {
        shell_split_free(split_argv, split_argc);
        split_argv = NULL;
        split_argc = 0;
        argv = sh_argv;
    }

    tam_exec_opts_t opts = {
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 30000,
        .input = input ? input : "",
        .input_len = input_len,
        .max_output = 16 * 1024 * 1024
    };

    tam_exec_result_t res = tam_exec_opts(argv, &opts);
    shell_split_free(split_argv, split_argc);

    if (res.timed_out) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Command timed out");
        tam_exec_result_free(&res);
        return false;
    }
    if (res.truncated) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Command output exceeded limit");
        tam_exec_result_free(&res);
        return false;
    }
    if (res.exit_code != 0) {
        if (err_buf && err_size > 0) {
            if (res.stderr_buf && res.stderr_len > 0) {
                char msg[192];
                size_t n = res.stderr_len < sizeof(msg) - 1 ? res.stderr_len : sizeof(msg) - 1;
                memcpy(msg, res.stderr_buf, n);
                msg[n] = '\0';
                char *nl = strpbrk(msg, "\r\n");
                if (nl) *nl = '\0';
                snprintf(err_buf, err_size, "Exit %d: %s", res.exit_code, msg);
            } else {
                snprintf(err_buf, err_size, "Command exited with code %d", res.exit_code);
            }
        }
        tam_exec_result_free(&res);
        return false;
    }

    char *copy = malloc(res.stdout_len + 1);
    if (!copy) {
        tam_exec_result_free(&res);
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Out of memory");
        return false;
    }
    if (res.stdout_len > 0 && res.stdout_buf) {
        memcpy(copy, res.stdout_buf, res.stdout_len);
    }
    copy[res.stdout_len] = '\0';
    *out_text = copy;
    *out_len = res.stdout_len;
    tam_exec_result_free(&res);
    return true;
}

static struct timespec timespec_add_ms(struct timespec t, int ms) {
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) {
        t.tv_sec += 1;
        t.tv_nsec -= 1000000000L;
    }
    return t;
}

bool shell_bg_start(shell_bg_t *bg, const char *cmd, int timeout_ms,
                    char *err_buf, size_t err_size) {
    if (err_buf && err_size > 0) err_buf[0] = '\0';
    if (!bg || !cmd || !cmd[0]) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "run: missing command");
        return false;
    }
    if (bg->active) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Background command already running");
        return false;
    }

    char **split_argv = NULL;
    int split_argc = 0;
    bool use_direct = !shell_needs_sh(cmd) &&
                      shell_split(cmd, &split_argv, &split_argc) &&
                      split_argc > 0;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        shell_split_free(split_argv, split_argc);
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "pipe failed: %s", strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        shell_split_free(split_argv, split_argc);
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "fork failed: %s", strerror(errno));
        return false;
    }

    if (pid == 0) {
        setpgid(0, 0);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        if (use_direct) {
            execvp(split_argv[0], split_argv);
        } else {
            const char *sh_argv[4] = { "sh", "-c", cmd, NULL };
            execvp("sh", (char *const *)(uintptr_t)sh_argv);
        }
        _exit(127);
    }

    shell_split_free(split_argv, split_argc);
    close(pipefd[1]);

    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    free(bg->output);
    memset(bg, 0, sizeof(*bg));
    bg->active = true;
    bg->pid = pid;
    bg->pipe_fd = pipefd[0];
    bg->max_output = 4 * 1024 * 1024;
    bg->exit_code = -1;
    snprintf(bg->cmd_desc, sizeof(bg->cmd_desc), "%s", cmd);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    bg->deadline = timespec_add_ms(now, timeout_ms > 0 ? timeout_ms : 30000);
    return true;
}

static void bg_reap(shell_bg_t *bg) {
    if (bg->pipe_fd >= 0) {
        close(bg->pipe_fd);
        bg->pipe_fd = -1;
    }
    if (bg->pid > 0) {
        int status = 0;
        pid_t r = waitpid(bg->pid, &status, 0);
        if (r > 0) {
            if (WIFEXITED(status)) {
                bg->exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                bg->exit_code = 128 + WTERMSIG(status);
            }
        }
        bg->pid = 0;
    }
    bg->active = false;
}

bool shell_bg_poll_read(shell_bg_t *bg) {
    if (!bg || !bg->active || bg->pipe_fd < 0) return false;

    char chunk[4096];
    for (;;) {
        ssize_t n = read(bg->pipe_fd, chunk, sizeof(chunk));
        if (n > 0) {
            size_t take = (size_t)n;
            if (bg->out_len + take > bg->max_output) {
                bg->truncated = true;
                take = bg->max_output > bg->out_len ? bg->max_output - bg->out_len : 0;
            }
            if (take > 0) {
                if (bg->out_len + take + 1 > bg->out_cap) {
                    size_t new_cap = bg->out_cap ? bg->out_cap * 2 : 4096;
                    while (new_cap < bg->out_len + take + 1) new_cap *= 2;
                    char *nb = realloc(bg->output, new_cap);
                    if (nb) {
                        bg->output = nb;
                        bg->out_cap = new_cap;
                    } else {
                        take = 0;
                    }
                }
                if (take > 0) {
                    memcpy(bg->output + bg->out_len, chunk, take);
                    bg->out_len += take;
                    bg->output[bg->out_len] = '\0';
                }
            }
            if (bg->truncated) {
                if (bg->pid > 0) kill(-bg->pid, SIGKILL);
                bg_reap(bg);
                return false;
            }
            continue;
        }
        if (n == 0) {
            bg_reap(bg);
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        bg_reap(bg);
        return false;
    }
}

bool shell_bg_check_timeout(shell_bg_t *bg, int *out_timeout_ms) {
    if (out_timeout_ms) *out_timeout_ms = -1;
    if (!bg || !bg->active) return false;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long diff_ms = (long)(bg->deadline.tv_sec - now.tv_sec) * 1000L +
                   (bg->deadline.tv_nsec - now.tv_nsec) / 1000000L;
    if (diff_ms <= 0) {
        bg->timed_out = true;
        if (bg->pid > 0) kill(-bg->pid, SIGKILL);
        bg_reap(bg);
        return true;
    }
    if (out_timeout_ms) {
        *out_timeout_ms = diff_ms > 500 ? 500 : (int)diff_ms;
    }
    return false;
}

void shell_bg_cancel(shell_bg_t *bg) {
    if (!bg || !bg->active) return;
    if (bg->pid > 0) {
        kill(-bg->pid, SIGKILL);
    }
    bg_reap(bg);
}

void shell_bg_free(shell_bg_t *bg) {
    if (!bg) return;
    shell_bg_cancel(bg);
    free(bg->output);
    bg->output = NULL;
    bg->out_len = 0;
    bg->out_cap = 0;
}

int shell_run_interactive(tam_tty_t *tty, const char *cmd,
                          bool wait_for_enter, bool mouse_enabled) {
    if (tty) {
        tam_tty_mouse(tty, false);
        tam_tty_paste(tty, false);
        tam_tty_raw(tty, false);
        tam_tty_alt_screen(tty, false);
        tam_tty_cursor_visible(tty, true);
    }

    int exit_code = 0;
    pid_t pid = fork();
    if (pid == 0) {
        if (cmd && cmd[0]) {
            const char *argv[4] = { "sh", "-c", cmd, NULL };
            execvp("sh", (char *const *)(uintptr_t)argv);
        } else {
            const char *sh = getenv("SHELL");
            if (!sh || !sh[0]) sh = "sh";
            const char *argv[2] = { sh, NULL };
            execvp(sh, (char *const *)(uintptr_t)argv);
        }
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);
    } else {
        exit_code = -1;
    }

    if (wait_for_enter) {
        const char *msg = "\nPress enter to continue\n";
        ssize_t unused = write(STDOUT_FILENO, msg, strlen(msg));
        (void)unused;
        char c = 0;
        while (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == '\n' || c == '\r') break;
        }
    }

    if (tty) {
        tam_tty_alt_screen(tty, true);
        tam_tty_raw(tty, true);
        tam_tty_paste(tty, true);
        if (mouse_enabled) tam_tty_mouse(tty, true);
        tam_tty_cursor_visible(tty, true);
    }
    return exit_code;
}
