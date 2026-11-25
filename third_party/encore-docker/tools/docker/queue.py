#!/usr/bin/env python3
"""Run a command in this machine's Docker queue: one at a time, first come, first served, across every port.

    tools/docker/queue.py CMD [ARG...]        wait for this machine's turn, then exec CMD holding the lock
    tools/docker/run.sh ARGS...               the usual way in: `docker run ARGS` through this queue (README.md)

From spyro-vita tools/docker/run.sh @ 663ceeb (PRs #253, #255), made game-neutral. README.md has the settings, how
a game uses it and how spyro-vita, kh1-vita and atv2-vita switch over.

The lock is a file (ENCORE_DOCKER_LOCK, default ~/.cache/encore/docker.lock) held with flock(2) for as long as CMD
runs: the descriptor survives the exec, so the kernel releases it exactly when CMD (and any child still holding the
descriptor) exits, however it exits. flock alone wakes its waiters in no order, so each waiter leaves a ticket (its
start time and PID) in LOCK.q/ and takes the lock only when its ticket is the oldest live one.

Stale tickets: a ticket whose PID is gone, or whose PID now names a newer process (the ticket records the process's
start time, as `ps -o lstart=` prints it), is removed by the next waiter that looks. A dead holder needs nothing:
its lock went with it.

Older locks (ENCORE_DOCKER_ALSO_LOCK, a ':'-separated list; default spyro-vita's ~/.cache/spyro-vita/docker.lock,
empty for none) are queued for and held too, in the same ticket format, so a game still on its own copy of the old
queue and a game on this one never run beside each other, and keep their first-come order between them.

ENCORE_DQ=0 runs CMD straight away (a quick look inside a container, or one that must run beside another). Inside a
command that already holds the queue (ENCORE_DQ_HELD names its lock) a nested call runs straight away too, so a
script that holds the queue can start several containers without waiting for itself. ENCORE_DQ_LABEL names the job
in the lock file and in what waiters print (default: the command's last arguments). ENCORE_DQ_POLL is the waiters'
poll interval in seconds (default 1).
"""
import fcntl
import os
import signal
import subprocess
import sys
import time

TAG = "encore docker queue"


def lock_paths():
    home = os.path.expanduser("~")
    primary = os.environ.get("ENCORE_DOCKER_LOCK") or os.path.join(home, ".cache", "encore", "docker.lock")
    also = os.environ.get("ENCORE_DOCKER_ALSO_LOCK")
    if also is None:  # unset: the queue this one replaces (spyro-vita PRs #253, #255); set it empty for none
        also = os.path.join(home, ".cache", "spyro-vita", "docker.lock")
    out = []
    for p in [primary] + [a for a in also.split(":") if a]:
        p = os.path.abspath(os.path.expanduser(p))
        if p not in out:
            out.append(p)
    return out


def start_times(pids):
    """{pid: start time} as ps prints it; {} when ps cannot say (then a ticket is checked by its PID alone)."""
    if not pids:
        return {}
    try:
        r = subprocess.run(["ps", "-o", "pid=,lstart=", "-p", ",".join(str(p) for p in sorted(pids))],
                           capture_output=True, text=True, env=dict(os.environ, LC_ALL="C"), timeout=10)
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0 and r.stderr.strip():  # a ps without lstart (busybox); no match at all exits 1 silently
        return None
    out = {}
    for line in r.stdout.splitlines():
        f = line.split(None, 1)
        if len(f) == 2 and f[0].isdigit():
            out[int(f[0])] = " ".join(f[1].split())
    return out


def alive(pid):
    try:
        os.kill(pid, 0)
    except PermissionError:
        return True
    except (ProcessLookupError, OverflowError):
        return False
    return True


def live(q, me, my_stamp):
    """The live tickets in queue directory q, oldest first; stale ones are removed and ours is put back if missing."""
    try:
        names = os.listdir(q)
    except FileNotFoundError:
        os.makedirs(q, exist_ok=True)
        names = []
    if me not in names:  # someone removed it (or the directory): back in our place, which is our start time
        write_ticket(q, me, my_stamp)
        names.append(me)
    tickets = {}
    for n in names:
        try:
            tickets[n] = int(n.split("-")[1])
        except (ValueError, IndexError):
            drop(q, n)
    stamps = start_times({p for n, p in tickets.items() if n != me and alive(p)})
    out = []
    for n, pid in tickets.items():
        ok = n == me or alive(pid)
        if ok and n != me and stamps is not None:
            try:
                with open(os.path.join(q, n)) as f:
                    recorded = " ".join(f.read().split())
            except OSError:
                recorded = ""
            # An empty ticket is an older queue's (spyro-vita's): its PID is all there is to check.
            ok = pid in stamps and (not recorded or recorded == stamps[pid])
        if ok:
            out.append(n)
        else:
            drop(q, n)
    return sorted(out)


def drop(q, n):
    try:
        os.remove(os.path.join(q, n))
    except OSError:
        pass


def write_ticket(q, name, stamp):
    os.makedirs(q, exist_ok=True)
    with open(os.path.join(q, name), "w") as f:
        f.write(stamp + "\n" if stamp else "")


def holder(fd, path):
    try:
        held = os.pread(fd, 400, 0).decode("utf-8", "replace").strip()
    except OSError:
        held = ""
    if not held:
        return "nothing yet"
    try:
        pid = int(held.split()[1])
    except (ValueError, IndexError):
        pid = 0
    if pid and not alive(pid):
        return "nothing (last: %s)" % held
    return held if path is None else "%s [%s]" % (held, path)


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.stdout.write(__doc__)
        return 0 if argv else 2
    paths = lock_paths()
    nested = os.environ.get("ENCORE_DQ_HELD")
    if os.environ.get("ENCORE_DQ", "1") == "0" or (nested and os.path.abspath(nested) == paths[0]):
        os.execvp(argv[0], argv)
    poll = float(os.environ.get("ENCORE_DQ_POLL", "1"))
    label = os.environ.get("ENCORE_DQ_LABEL") or " ".join(argv[-6:])[:200]

    fds = []
    for p in paths:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        fds.append(os.open(p, os.O_RDWR | os.O_CREAT, 0o644))
    queues = [p + ".q" for p in paths]
    me = "%020d-%d" % (time.time_ns(), os.getpid())
    my_stamp = (start_times({os.getpid()}) or {}).get(os.getpid(), "")
    for q in queues:
        write_ticket(q, me, my_stamp)
    held = [False] * len(paths)
    said = None
    for sig in (signal.SIGTERM, signal.SIGHUP):  # leave no ticket behind (exec resets these to the default)
        signal.signal(sig, lambda n, _f: sys.exit(128 + n))
    try:
        while True:
            lines = [live(q, me, my_stamp) for q in queues]
            blocked = None
            if all(t[0] == me for t in lines):
                for i, fd in enumerate(fds):
                    if not held[i]:
                        try:
                            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                            held[i] = True
                        except BlockingIOError:
                            blocked = i
                            break
                if all(held):
                    break
            ahead = max(t.index(me) for t in lines)
            state = (ahead, blocked)
            if said != state:
                i = 0 if blocked is None else blocked
                sys.stderr.write("%s: %d waiting ahead; running: %s\n"
                                 % (TAG, ahead, holder(fds[i], None if i == 0 else paths[i])))
                said = state
            time.sleep(poll)
    except BaseException:
        for q in queues:
            drop(q, me)
        raise
    if said is not None:
        sys.stderr.write("%s: running\n" % TAG)
    for q in queues:
        drop(q, me)
    info = "pid %d since %s in %s: %s\n" % (os.getpid(), time.strftime("%H:%M:%S"), os.getcwd(), label)
    for fd in fds:
        os.ftruncate(fd, 0)
        os.pwrite(fd, info.encode(), 0)
        os.set_inheritable(fd, True)
    os.environ["ENCORE_DQ_HELD"] = paths[0]
    os.execvp(argv[0], argv)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)
