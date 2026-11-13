#!/usr/bin/env python3
"""Benchmark tam-edit (C11) against /usr/bin/micro on ROADMAP Section 8 targets."""

import fcntl
import os
import pty
import select
import shutil
import struct
import subprocess
import tempfile
import termios
import time


def set_winsize(fd, rows=24, cols=80):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def measure_pty_startup_and_rss(cmd, args, env=None):
    master, slave = pty.openpty()
    set_winsize(slave, 24, 80)
    t0 = time.perf_counter()
    proc = subprocess.Popen(
        [cmd] + args,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        env=env or os.environ.copy(),
        close_fds=True,
    )
    os.close(slave)

    first_paint_ms = None
    deadline = t0 + 5.0
    while time.perf_counter() < deadline:
        r, _, _ = select.select([master], [], [], 0.2)
        if r:
            chunk = os.read(master, 4096)
            if chunk and first_paint_ms is None:
                first_paint_ms = (time.perf_counter() - t0) * 1000.0
                break

    # Give it a moment to finish syntax scan / settle, then sample VmHWM
    time.sleep(0.15)
    vmhwm_kb = None
    try:
        with open(f"/proc/{proc.pid}/status", "r") as f:
            for line in f:
                if line.startswith("VmHWM:"):
                    vmhwm_kb = int(line.split()[1])
                    break
    except FileNotFoundError:
        pass

    # Measure keystroke-to-paint latency (send 'x' and wait for echo/redraw)
    t_key = time.perf_counter()
    os.write(master, b"x")
    key_ms = None
    while time.perf_counter() < t_key + 1.0:
        r, _, _ = select.select([master], [], [], 0.1)
        if r:
            chunk = os.read(master, 4096)
            if chunk:
                key_ms = (time.perf_counter() - t_key) * 1000.0
                break

    # Quit cleanly (Ctrl-Q twice if dirty)
    os.write(master, b"\x11\x11")
    try:
        proc.wait(timeout=2.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    os.close(master)
    return first_paint_ms, vmhwm_kb, key_ms


def main():
    bin_path = os.path.abspath("build/tam-edit")
    if not os.path.exists(bin_path):
        raise SystemExit("build/tam-edit not found; run make first")

    size_bytes = os.path.getsize(bin_path)
    print(f"Binary size (unstripped): {size_bytes / (1024 * 1024):.2f} MB (target <= 5.00 MB)")

    with tempfile.TemporaryDirectory() as tmp:
        c_10k = os.path.join(tmp, "big.c")
        with open(c_10k, "w") as f:
            for i in range(10000):
                f.write(f"int var_{i} = {i}; /* comment {i} */\n")

        empty_f = os.path.join(tmp, "empty.txt")
        with open(empty_f, "w") as f:
            f.write("hello\n")

        env = os.environ.copy()
        env["TERM"] = "xterm-256color"
        env["TAM_EDIT_CONFIG_HOME"] = os.path.join(tmp, "cfg")

        fp_80, _, key_80 = measure_pty_startup_and_rss(bin_path, [empty_f], env=env)
        fp_10k, rss_10k, key_10k = measure_pty_startup_and_rss(bin_path, [c_10k], env=env)

        print(f"tam-edit 80x24 startup:      {fp_80:.2f} ms (target <= 25.00 ms)")
        print(f"tam-edit 80x24 keystroke:    {key_80:.2f} ms (target <= 16.00 ms)")
        print(f"tam-edit 10k-line startup:   {fp_10k:.2f} ms")
        print(f"tam-edit 10k-line keystroke: {key_10k:.2f} ms (target <= 50.00 ms)")
        print(f"tam-edit 10k-line VmHWM:     {rss_10k / 1024.0:.2f} MB (target <= 15.00 MB)")

        micro_bin = shutil.which("micro")
        if micro_bin:
            m_fp, _, m_key = measure_pty_startup_and_rss(micro_bin, [empty_f], env=env)
            _, m_rss, m_key10k = measure_pty_startup_and_rss(micro_bin, [c_10k], env=env)
            fmt = lambda v, u: f"{v:.2f} {u}" if v is not None else "n/a"
            m_rss_mb = (m_rss / 1024.0) if m_rss is not None else None
            print(f"micro    80x24 startup:      {fmt(m_fp, 'ms')}, key={fmt(m_key, 'ms')}")
            print(f"micro    10k-line VmHWM:     {fmt(m_rss_mb, 'MB')}, key={fmt(m_key10k, 'ms')}")


if __name__ == "__main__":
    main()
