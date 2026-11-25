#!/usr/bin/env bash
# docker run for tools/tc, tools/pc/tc and tools/ps4/tc: one spyro container at a time on this machine, on the
# fastest Docker VM that can see the mounts. Takes `docker run`'s arguments.
#
# The VM: SPYRO_DOCKER_HOST if set; else colima's kh1fast profile (4 CPUs, 6 GB, Rosetta for amd64 images,
# virtiofs) when its socket exists and every -v source is under ~/Projects, the only folder it mounts; else
# DOCKER_HOST. The images must already be on that VM (--pull never: nothing is fetched); docs/host-build.md says
# how to build or copy them there.
#
# The queue: a lock file (SPYRO_DQ_LOCK, default ~/.cache/spyro-vita/docker.lock) held for as long as the
# container runs, so a second build waits instead of halving the first one's CPUs; while it waits it prints who
# holds the lock. SPYRO_DQ=0 skips the queue (a quick look inside, or a run that must overlap another), and so
# does TC_PORT (a --serve that a second container talks to).
set -euo pipefail

home_projects="$HOME/Projects/"
fast="$HOME/.colima/kh1fast/docker.sock"
if [ -n "${SPYRO_DOCKER_HOST:-}" ]; then
  export DOCKER_HOST="$SPYRO_DOCKER_HOST"
elif [ -S "$fast" ]; then
  ok=1 prev=
  for a in "$@"; do
    if [ "$prev" = "-v" ]; then
      case "${a%%:*}/" in "$home_projects"*) ;; *) ok=0 ;; esac
    fi
    prev=$a
  done
  [ "$ok" = 1 ] && export DOCKER_HOST="unix://$fast"
fi

if [ "${SPYRO_DQ:-1}" = 0 ] || [ -n "${TC_PORT:-}" ]; then
  exec docker run --pull never "$@"
fi

lock=${SPYRO_DQ_LOCK:-$HOME/.cache/spyro-vita/docker.lock}
mkdir -p "$(dirname "$lock")"
# First come, first served: each waiter leaves a ticket (its start time and PID) in LOCK.q/ and takes the lock only
# when its ticket is the oldest live one, so a session that starts builds back to back cannot keep jumping ahead of
# one that is waiting (flock alone wakes waiters in no order). Tickets of dead PIDs are dropped. The lock itself is
# flock in Python (macOS has no flock(1)); its descriptor survives the exec into docker, so it is released exactly
# when the docker client exits.
exec python3 -I -c '
import fcntl, os, sys, time
path, argv = sys.argv[1], sys.argv[2:]
fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
q = path + ".q"
os.makedirs(q, exist_ok=True)
me = "%020d-%d" % (time.time_ns(), os.getpid())
open(os.path.join(q, me), "w").close()
def live():
    out = []
    for n in sorted(os.listdir(q)):
        try:
            pid = int(n.split("-")[1])
            os.kill(pid, 0)
        except PermissionError:
            pass
        except (ValueError, IndexError, ProcessLookupError):
            try:
                os.remove(os.path.join(q, n))
            except OSError:
                pass
            continue
        out.append(n)
    return out
said = None
while True:
    ahead = live()
    if ahead and ahead[0] == me:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            pass
    n = ahead.index(me) if me in ahead else 0
    if said != n:
        held = os.pread(fd, 400, 0).decode("utf-8", "replace").strip()
        sys.stderr.write("spyro docker queue: %d waiting ahead; running: %s\n" % (n, held or "nothing yet"))
        said = n
    time.sleep(1)
if said is not None:
    sys.stderr.write("spyro docker queue: running\n")
os.remove(os.path.join(q, me))
os.ftruncate(fd, 0)
os.pwrite(fd, ("pid %d since %s in %s: %s\n" % (os.getpid(), time.strftime("%H:%M:%S"), os.getcwd(),
                                                  " ".join(argv[-6:])[:200])).encode(), 0)
os.set_inheritable(fd, True)
os.execvp(argv[0], argv)
' "$lock" docker run --pull never "$@"
