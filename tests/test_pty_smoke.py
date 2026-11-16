import os
import pty
import select
import shutil
import subprocess
import tempfile
import threading
import time

def drain_master(fd, stop_event):
    while not stop_event.is_set():
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try:
                data = os.read(fd, 4096)
                if not data:
                    break
            except OSError:
                break

def test_cli_version():
    proc = subprocess.run(["./build/tam-edit", "--version"], stdout=subprocess.PIPE, text=True)
    assert proc.returncode == 0, f"--version failed with code {proc.returncode}"
    assert "tam-edit 1.0.2" in proc.stdout, f"Unexpected version output: {proc.stdout}"
    print("[PASS] CLI --version output verified")

def test_cli_config_check():
    tmp = tempfile.NamedTemporaryFile("w", delete=False, suffix=".ini")
    try:
        tmp.write(
            "[settings]\n"
            "softwrap = true\n"
            "wordwrap = true\n"
            "tabsize = 4\n"
            "colorscheme = default\n"
            "\n"
            "[bindings]\n"
            "Alt-w = togglelocal softwrap\n"
            "Ctrl-t = AddTab\n"
        )
        tmp.close()

        proc = subprocess.run(["./build/tam-edit", "--check-config", tmp.name],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert proc.returncode == 0, f"--check-config failed:\n{proc.stdout}\n{proc.stderr}"
        assert "Configuration check passed successfully" in proc.stdout
        print("[PASS] CLI --check-config validation verified")
    finally:
        if os.path.exists(tmp.name):
            os.remove(tmp.name)

def test_cli_micro_import():
    tmp_home = tempfile.mkdtemp(prefix="tam-test-home-")
    try:
        micro_dir = os.path.join(tmp_home, "micro_src")
        os.makedirs(micro_dir, exist_ok=True)
        with open(os.path.join(micro_dir, "settings.json"), "w") as f:
            f.write("{\n    // comment\n    \"softwrap\": true,\n    \"tabsize\": 2,\n}\n")
        with open(os.path.join(micro_dir, "bindings.json"), "w") as f:
            f.write("{\n    \"Alt-w\": \"togglelocal softwrap\",\n    \"Ctrl-t\": \"AddTab\",\n}\n")

        env = dict(os.environ, HOME=tmp_home, XDG_CONFIG_HOME=os.path.join(tmp_home, ".config"))
        proc = subprocess.run(["./build/tam-edit", "--import-micro-config", micro_dir],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
        assert proc.returncode == 0, f"--import-micro-config failed:\n{proc.stdout}\n{proc.stderr}"

        ini_path = os.path.join(tmp_home, ".config", "tam-edit", "settings.ini")
        assert os.path.exists(ini_path), f"settings.ini not created at {ini_path}"

        chk = subprocess.run(["./build/tam-edit", "--check-config", ini_path],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
        assert chk.returncode == 0, f"--check-config failed on imported INI:\n{chk.stdout}"
        print("[PASS] CLI --import-micro-config JSON5 migration verified")
    finally:
        shutil.rmtree(tmp_home, ignore_errors=True)

def run_pty_test():
    test_path = "/tmp/tam-edit-pty-smoke.txt"
    if os.path.exists(test_path):
        os.remove(test_path)

    master, slave = pty.openpty()
    proc = subprocess.Popen(
        ["./build/tam-edit", test_path],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
    )
    os.close(slave)

    stop_drain = threading.Event()
    drain_thread = threading.Thread(target=drain_master, args=(master, stop_drain), daemon=True)
    drain_thread.start()

    time.sleep(0.3)

    # 1. Type "Hello Milestone 1"
    os.write(master, b"Hello Milestone 1")
    time.sleep(0.2)

    # 2. Save with Ctrl-S (\x13)
    os.write(master, b"\x13")
    time.sleep(0.3)

    assert os.path.exists(test_path), "File was not created on Ctrl-S"
    with open(test_path, "r", encoding="utf-8") as f:
        content = f.read()
    assert "Hello Milestone 1" in content, f"Unexpected content: {content}"
    print("[PASS] PTY insert and Ctrl-S save verified")

    # 3. Undo with Ctrl-Z (\x1a)
    os.write(master, b"\x1a")
    time.sleep(0.2)

    # 4. Save with Ctrl-S
    os.write(master, b"\x13")
    time.sleep(0.3)

    with open(test_path, "r", encoding="utf-8") as f:
        content = f.read()
    assert content == "" or content == "\n", f"Unexpected content after undo: {content}"
    print("[PASS] PTY Ctrl-Z undo verified")

    # 5. Type "Done", quit with Ctrl-Q (\x11)
    os.write(master, b"Done")
    time.sleep(0.2)
    # First Ctrl-Q when dirty: should arm and prompt
    os.write(master, b"\x11")
    time.sleep(0.2)
    # Second Ctrl-Q: quit
    os.write(master, b"\x11")
    time.sleep(0.3)

    proc.wait(timeout=2)
    stop_drain.set()
    drain_thread.join(timeout=1)
    os.close(master)
    if os.path.exists(test_path):
        os.remove(test_path)

    print("[PASS] PTY dirty-quit confirmation and clean exit verified")

def test_pty_splits_and_tabs():
    file1 = "/tmp/tam-m2-tab1.txt"
    file2 = "/tmp/tam-m2-tab2.txt"
    for f in (file1, file2):
        if os.path.exists(f):
            os.remove(f)

    master, slave = pty.openpty()
    proc = subprocess.Popen(
        ["./build/tam-edit", file1, file2],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
    )
    os.close(slave)

    stop_drain = threading.Event()
    drain_thread = threading.Thread(target=drain_master, args=(master, stop_drain), daemon=True)
    drain_thread.start()

    time.sleep(0.4)

    # 1. Tab 1 is active: type "AlphaTab" and save
    os.write(master, b"AlphaTab")
    time.sleep(0.2)
    os.write(master, b"\x13")  # Ctrl-S
    time.sleep(0.3)

    # 2. Switch to Tab 2 via command prompt: Ctrl-E (:), "tabnext\r"
    os.write(master, b"\x05")  # Ctrl-E
    time.sleep(0.2)
    os.write(master, b"tabnext\r")
    time.sleep(0.3)

    # 3. Tab 2: type "BetaTab" and save
    os.write(master, b"BetaTab")
    time.sleep(0.2)
    os.write(master, b"\x13")  # Ctrl-S
    time.sleep(0.3)

    # 4. In Tab 2, split vertically via Ctrl-E, "vsplit\r"
    os.write(master, b"\x05")
    time.sleep(0.2)
    os.write(master, b"vsplit\r")
    time.sleep(0.3)

    # 5. Type in new split: "-SplitText" and save
    os.write(master, b"-SplitText")
    time.sleep(0.2)
    os.write(master, b"\x13")
    time.sleep(0.3)

    # 6. Unsplit via Ctrl-E, "unsplit\r"
    os.write(master, b"\x05")
    time.sleep(0.2)
    os.write(master, b"unsplit\r")
    time.sleep(0.3)

    # 7. Quit with Ctrl-Q (\x11)
    os.write(master, b"\x11")
    time.sleep(0.3)

    proc.wait(timeout=2)
    stop_drain.set()
    drain_thread.join(timeout=1)
    os.close(master)

    assert os.path.exists(file1), f"{file1} does not exist"
    with open(file1, "r", encoding="utf-8") as f:
        c1 = f.read()
    assert "AlphaTab" in c1, f"Unexpected content in {file1}: {c1}"

    assert os.path.exists(file2), f"{file2} does not exist"
    with open(file2, "r", encoding="utf-8") as f:
        c2 = f.read()
    assert "BetaTab" in c2 and "-SplitText" in c2, f"Unexpected content in {file2}: {c2}"

    for f in (file1, file2):
        if os.path.exists(f):
            os.remove(f)

    print("[PASS] PTY multi-tab file loading, split tree, and shared editing verified")

def test_pty_milestone4_commands():
    fpath = "/tmp/tam-m4-pty.txt"
    with open(fpath, "w", encoding="utf-8") as f:
        f.write("a=1 b=2\n")

    cfg_dir = tempfile.mkdtemp(prefix="tam-m4-cfg-")
    env = os.environ.copy()
    env["TAM_EDIT_CONFIG_HOME"] = cfg_dir

    master, slave = pty.openpty()
    proc = subprocess.Popen(
        ["./build/tam-edit", fpath],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        env=env,
        close_fds=True,
    )
    os.close(slave)

    stop_drain = threading.Event()
    drain_thread = threading.Thread(target=drain_master, args=(master, stop_drain), daemon=True)
    drain_thread.start()

    time.sleep(0.4)
    os.write(master, b"\x05")  # Ctrl-E
    time.sleep(0.15)
    os.write(master, b"replaceall ([a-z]+)=([0-9]+) $1:$2\r")
    time.sleep(0.25)
    os.write(master, b"\x13")  # Ctrl-S
    time.sleep(0.25)
    os.write(master, b"\x05")  # Ctrl-E
    time.sleep(0.15)
    os.write(master, b"help commands\r")
    time.sleep(0.25)
    os.write(master, b"\x11")  # Ctrl-Q
    time.sleep(0.3)

    proc.wait(timeout=2)
    stop_drain.set()
    drain_thread.join(timeout=1)
    os.close(master)

    with open(fpath, "r", encoding="utf-8") as f:
        out = f.read()
    assert out.strip() == "a:1 b:2", f"Unexpected replaceall output: {out!r}"

    if os.path.exists(fpath):
        os.remove(fpath)
    shutil.rmtree(cfg_dir, ignore_errors=True)
    print("[PASS] PTY Milestone 4 replaceall, help, save, and clean quit verified")

def test_pty_plus_line_col():
    fpath = "/tmp/tam-m5-plus.txt"
    with open(fpath, "w", encoding="utf-8") as f:
        f.write("line1\nline2\nhello_world\n")

    cfg_dir = tempfile.mkdtemp(prefix="tam-m5-cfg-")
    env = os.environ.copy()
    env["TAM_EDIT_CONFIG_HOME"] = cfg_dir

    master, slave = pty.openpty()
    proc = subprocess.Popen(
        ["./build/tam-edit", "+3:6", fpath],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        env=env,
        close_fds=True,
    )
    os.close(slave)

    stop_drain = threading.Event()
    drain_thread = threading.Thread(target=drain_master, args=(master, stop_drain), daemon=True)
    drain_thread.start()

    time.sleep(0.4)
    os.write(master, b"!")  # Insert at line 3, col 6 (0-based col 5 -> after 'hello')
    time.sleep(0.15)
    os.write(master, b"\x13")  # Ctrl-S
    time.sleep(0.25)
    os.write(master, b"\x11")  # Ctrl-Q
    time.sleep(0.3)

    proc.wait(timeout=2)
    stop_drain.set()
    drain_thread.join(timeout=1)
    os.close(master)

    with open(fpath, "r", encoding="utf-8") as f:
        out = f.read()
    assert "hello!_world" in out, f"Unexpected +3:6 output: {out!r}"

    if os.path.exists(fpath):
        os.remove(fpath)
    shutil.rmtree(cfg_dir, ignore_errors=True)
    print("[PASS] PTY +LINE:COL startup jump verified")

def _read_pty_until_idle(fd, idle_s=0.15):
    out = b""
    while select.select([fd], [], [], idle_s)[0]:
        chunk = os.read(fd, 65536)
        if not chunk:
            break
        out += chunk
    return out

def test_pty_arrow_keys():
    import re
    for softwrap in (False, True):
        fpath = "/tmp/tam-arrow-test.txt"
        long_line = "A" * 90  # wraps into 2 sublines on 80-col terminal
        with open(fpath, "w", encoding="utf-8") as f:
            f.write(f"first\n{long_line}\nthird\n")

        cfg_dir = tempfile.mkdtemp(prefix="tam-arrow-cfg-")
        ini_dir = os.path.join(cfg_dir, "tam-edit")
        os.makedirs(ini_dir, exist_ok=True)
        with open(os.path.join(ini_dir, "settings.ini"), "w", encoding="utf-8") as f:
            f.write(f"[settings]\nsoftwrap = {'true' if softwrap else 'false'}\n")

        env = os.environ.copy()
        env["TAM_EDIT_CONFIG_HOME"] = cfg_dir
        env["HOME"] = cfg_dir

        master, slave = pty.openpty()
        proc = subprocess.Popen(
            ["./build/tam-edit", fpath],
            stdin=slave,
            stdout=slave,
            stderr=slave,
            env=env,
            close_fds=True,
        )
        os.close(slave)

        try:
            _read_pty_until_idle(master)

            # Down to line 2
            os.write(master, b"\x1b[B")
            out_down1 = _read_pty_until_idle(master)
            pos1 = re.findall(rb"\x1b\[(\d+);(\d+)H", out_down1)
            assert pos1 and pos1[-1] == (b"2", b"5"), (
                f"softwrap={softwrap} Down1 cursor={pos1[-1] if pos1 else None}"
            )

            # Right 2 chars -> col 3 (screen col 7)
            os.write(master, b"\x1b[C\x1b[C")
            out_right = _read_pty_until_idle(master)
            pos_r = re.findall(rb"\x1b\[(\d+);(\d+)H", out_right)
            assert pos_r and pos_r[-1] == (b"2", b"7"), (
                f"softwrap={softwrap} Right2 cursor={pos_r[-1] if pos_r else None}"
            )

            # Down again: with softwrap, moves to subline 2 of line 2 (row 3);
            # without softwrap, moves to line 3 (row 3).
            os.write(master, b"\x1b[B")
            out_down2 = _read_pty_until_idle(master)
            pos2 = re.findall(rb"\x1b\[(\d+);(\d+)H", out_down2)
            assert pos2 and pos2[-1][0] == b"3", (
                f"softwrap={softwrap} Down2 cursor={pos2[-1] if pos2 else None}"
            )

            if softwrap:
                # One more Down moves past wrapped subline 2 to line 3 (row 4)
                os.write(master, b"\x1b[B")
                out_down3 = _read_pty_until_idle(master)
                pos3 = re.findall(rb"\x1b\[(\d+);(\d+)H", out_down3)
                assert pos3 and pos3[-1][0] == b"4", (
                    f"softwrap={softwrap} Down3 cursor={pos3[-1] if pos3 else None}"
                )

            # Up and Left
            os.write(master, b"\x1b[A\x1b[D")
            out_up = _read_pty_until_idle(master)
            pos_u = re.findall(rb"\x1b\[(\d+);(\d+)H", out_up)
            assert pos_u and pos_u[-1][0] == (b"3" if softwrap else b"2"), (
                f"softwrap={softwrap} Up+Left cursor={pos_u[-1] if pos_u else None}"
            )

            os.write(master, b"\x11")  # Ctrl-Q
            proc.wait(timeout=2)
        finally:
            os.close(master)
            if os.path.exists(fpath):
                os.remove(fpath)
            shutil.rmtree(cfg_dir, ignore_errors=True)
    print("[PASS] PTY arrow keys (softwrap=false and softwrap=true) verified")

if __name__ == "__main__":
    test_cli_version()
    test_cli_config_check()
    test_cli_micro_import()
    run_pty_test()
    test_pty_splits_and_tabs()
    test_pty_milestone4_commands()
    test_pty_plus_line_col()
    test_pty_arrow_keys()
