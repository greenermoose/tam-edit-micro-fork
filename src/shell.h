#ifndef TAM_EDIT_SHELL_H
#define TAM_EDIT_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#include "tam_tty.h"

/*
 * Split a command string into argv using POSIX shell-like quoting rules.
 * Caller releases with shell_split_free().
 */
bool shell_split(const char *cmd, char ***out_argv, int *out_argc);
void shell_split_free(char **argv, int argc);

/* Return true if cmd contains shell metacharacters requiring sh -c. */
bool shell_needs_sh(const char *cmd);

/*
 * Run cmd synchronously via tam_exec_opts with input piped to stdin.
 * Captures stdout cleanly and enforces timeout_ms (default 30000 if <= 0).
 */
bool shell_textfilter(const char *cmd,
                      const char *input, size_t input_len,
                      int timeout_ms,
                      char **out_text, size_t *out_len,
                      char *err_buf, size_t err_size);

typedef struct {
    bool            active;
    pid_t           pid;
    int             pipe_fd;
    struct timespec deadline;
    char           *output;
    size_t          out_len;
    size_t          out_cap;
    size_t          max_output;
    bool            timed_out;
    bool            truncated;
    int             exit_code;
    char            cmd_desc[256];
} shell_bg_t;

/*
 * Start a non-blocking background process for `run`.
 * Returns true if spawned; caller monitors bg->pipe_fd via poll().
 */
bool shell_bg_start(shell_bg_t *bg, const char *cmd, int timeout_ms,
                    char *err_buf, size_t err_size);

/*
 * Drain available bytes from bg->pipe_fd without blocking.
 * Returns true while still running, false once EOF or error closes the pipe
 * and the child process has been reaped.
 */
bool shell_bg_poll_read(shell_bg_t *bg);

/*
 * Kill background process group if wall-clock deadline has elapsed.
 * Returns true if timeout triggered and process was killed.
 */
bool shell_bg_check_timeout(shell_bg_t *bg, int *out_timeout_ms);

/* Cancel an active background process (sends SIGKILL to its process group). */
void shell_bg_cancel(shell_bg_t *bg);

void shell_bg_free(shell_bg_t *bg);

/*
 * Suspend tam_tty raw/alt-screen mode, execute interactive shell command
 * (or user's $SHELL if cmd is NULL/empty), optionally wait for Enter,
 * and restore tam_tty alt-screen + raw mode.
 */
int shell_run_interactive(tam_tty_t *tty, const char *cmd,
                          bool wait_for_enter, bool mouse_enabled);

#endif
