#!/usr/bin/env python3
"""demWatchdog.py — run a command in its own process group and stop it when
its output stalls; pgf dem run's stall watchdog (#340).

  demWatchdog.py STALL GRACE COMMAND [ARGS ...]

The command's stdout and stderr are merged and copied to stdout as they
arrive. When nothing arrives for STALL seconds, the group gets TERM, then KILL
after GRACE seconds, a message with the last output line goes to stderr, and
the exit status is 124 (TIMEOUT for pgf test and the regression lib). INT,
TERM and HUP sent to the watchdog go to the group, with the same KILL after
GRACE; the exit status is then 128 + the signal. Otherwise it is the command's
(128 + N when signal N ended it). Only the command's group is ever signalled.
A terminal stdin is not passed on: a background group reading it would stop.
"""

import os
import select
import signal
import subprocess
import sys
import time

WHO = "pgf dem run"


def group_alive(pgid):
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def stop(proc, sig, grace):
    """Send sig to the run's group; KILL what is left of it after grace seconds."""
    try:
        os.killpg(proc.pid, sig)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + grace
    while time.monotonic() < deadline:
        proc.poll()  # reap the leader, or the group never empties
        if not group_alive(proc.pid):
            return
        time.sleep(0.1)
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def last_line(tail):
    lines = [l for l in tail.decode(errors="replace").splitlines() if l.strip()]
    return lines[-1] if lines else "(no output)"


def main(argv):
    if len(argv) < 4:
        print("usage: demWatchdog.py STALL GRACE COMMAND [ARGS ...]", file=sys.stderr)
        return 2
    stall, grace, cmd = float(argv[1]), float(argv[2]), argv[3:]
    stdin = subprocess.DEVNULL if os.isatty(0) else None
    r, w = os.pipe()
    try:
        proc = subprocess.Popen(cmd, stdin=stdin, stdout=w, stderr=w, preexec_fn=os.setpgrp)
    except OSError as e:
        print(f"{WHO}: cannot start {cmd[0]}: {e.strerror}", file=sys.stderr)
        return 127
    os.close(w)

    received = []

    def on_signal(signum, _frame):
        if not received:
            received.append(signum)
            stop(proc, signum, grace)

    for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(s, on_signal)

    tail, out_ok, stalled = b"", True, False
    while True:
        ready, _, _ = select.select([r], [], [], stall if stall > 0 else None)
        if not ready:
            stalled = True
            break
        data = os.read(r, 65536)
        if not data:
            break
        tail = (tail + data)[-4096:]
        while out_ok and data:
            try:
                data = data[os.write(1, data):]
            except BrokenPipeError:
                out_ok = False  # keep draining, so the run is not blocked on a full pipe
    if not stalled:
        try:
            proc.wait(timeout=stall if stall > 0 else None)
        except subprocess.TimeoutExpired:
            stalled = True  # the output closed, but the leader hangs on
    if stalled:
        stop(proc, signal.SIGTERM, grace)
        proc.wait()
        secs = f"{stall:g}"
        print(f"{WHO}: no output for {secs} s — run stalled, stopped", file=sys.stderr)
        print(f"{WHO}: last line: {last_line(tail)}", file=sys.stderr)
        return 124
    if received:
        return 128 + received[0]
    rc = proc.returncode
    return 128 - rc if rc < 0 else rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
