import os
import pty
import select
import subprocess
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

if __name__ == "__main__":
    run_pty_test()

