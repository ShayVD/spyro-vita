# tools/docker: one Docker queue per machine

Several ports share one Mac and its colima VM. Two containers that each expect the whole VM halve each other's
CPUs, and a 4-CPU VM building three games at once takes hours for what takes minutes alone. These two scripts give
the machine one queue that every port joins.

| Script | What it does |
|---|---|
| `run.sh ARGS...` | `docker run ARGS...`: picks the VM, adds the CPU and memory limits and `--pull never`, waits its turn, runs |
| `queue.py CMD...` | waits its turn, then runs any command holding the queue (a script that starts containers itself) |

From spyro-vita `tools/docker/run.sh` @ 663ceeb: PR #253 (one container at a time, on the 4-CPU Rosetta VM) and
#255 (first come, first served). Here the lock is not named after a game, and spyro's values are settings.

## The queue

- **One lock per machine**: `~/.cache/encore/docker.lock` (`ENCORE_DOCKER_LOCK`), held with `flock` for as long as
  the command runs. The descriptor survives the exec, so the kernel releases it exactly when the command exits,
  however it exits.
- **First come, first served**: each waiter leaves a ticket (its start time and PID) in `docker.lock.q/` and
  takes the lock only when its ticket is the oldest live one. `flock` alone wakes waiters in no order, so a session
  that starts builds back to back kept jumping ahead of one that had waited longer (spyro-vita #255).
- **Stale holders and tickets**: a dead holder's lock is released by the kernel with its process. A ticket whose PID
  is gone, or whose PID now names a newer process (the ticket records the process's start time), is removed by the
  next waiter that looks. A waiter stopped with SIGINT, SIGTERM or SIGHUP takes its tickets with it.
- **While waiting** it prints how many tickets are ahead and what is running:
  `encore docker queue: 1 waiting ahead; running: pid 47083 since 09:38:59 in /Users/me/Projects/spyro-vita: ...`.
- **No waiting**: `ENCORE_DQ=0` runs straight away (a quick look inside a container, or a container that must run
  beside another, such as a `--serve` a second container talks to). A call made inside a command that already holds
  the queue runs straight away too (`ENCORE_DQ_HELD`), so a script wrapped in `queue.py` can start several
  containers without waiting for itself.
- `ENCORE_DQ_LABEL` names the job in the lock file and in what waiters print (default: the command's last
  arguments).

Every copy of these scripts on a machine shares one protocol: the lock path, the `.q` directory beside it, the
ticket name (`%020d-%d`, nanoseconds then PID) and its contents (the start time `ps -o lstart=` prints, or nothing).
Games vendor different Encore commits, so a change to any of these must stay readable by the copies already out.

## The VM and the limits (`run.sh`)

Settings come from the environment, else from `~/.config/encore/docker.env` (`ENCORE_DOCKER_CONFIG`): `KEY=VALUE`
lines, one machine's choices for every port. A leading `~` is the home directory; nothing else is expanded.

| Setting | Default | Meaning |
|---|---|---|
| `ENCORE_DOCKER_HOST` | | the Docker endpoint, always |
| `ENCORE_COLIMA_PROFILE` | | run on this colima profile (`~/.colima/PROFILE/docker.sock`) when it is running and every bind-mount source is inside a folder it mounts; else on `DOCKER_HOST` as it was, with a line saying why |
| `ENCORE_COLIMA_MOUNTS` | the profile's `colima.yaml` `mounts:` (colima's default, the home directory, when it lists none) | those folders, `:`-separated |
| `ENCORE_DOCKER_CPUS` | the whole VM | `--cpus` for every container, unless the caller passes its own |
| `ENCORE_DOCKER_MEMORY` | the whole VM | `--memory` likewise |
| `ENCORE_DOCKER_PULL` | `never` | `--pull`: images must already be on the VM (empty: docker's default) |

Named volumes are not bind mounts and do not count against the profile's folders. The VM's own size is colima's:
`colima start -p PROFILE --cpu 4 --memory 6 --vm-type vz --vz-rosetta --mount-type virtiofs --mount ~/Projects:w`
is what spyro-vita's `kh1fast` is (amd64 images under Rosetta). With one container at a time, leaving
`ENCORE_DOCKER_CPUS` unset gives each container the whole VM; set it only to keep CPUs back for something else.

This Mac's settings file, as spyro-vita used it:

```
ENCORE_COLIMA_PROFILE=kh1fast
```

## Using it from a game

Vendor it like any Encore path ([`docs/consuming.md`](../../docs/consuming.md)):

```bash
tools/vendor.sh ../my-game tools/docker
```

and call `third_party/encore/tools/docker/run.sh` (or `third_party/vita-port-kit/...` in a game vendored before
the rename) where the game called `docker run`. A script that runs its own `docker run` with its own traps (a time
limit that stops the container) wraps itself once instead:

```bash
q=$root/third_party/encore/tools/docker/queue.py
[ -n "${ENCORE_DQ_HELD:-}" ] || ENCORE_DQ_LABEL="my-game: $*" exec python3 -I "$q" "$0" "$@"
```

CI runners need neither the VM choice nor the queue: a game's workflow keeps its bare `docker run`.

## Switching the ports over

Until every port is on this queue, it also queues for and holds the older locks in `ENCORE_DOCKER_ALSO_LOCK`
(`:`-separated; default spyro-vita's `~/.cache/spyro-vita/docker.lock`; set it empty for none), with tickets in
their `.q` directories in the same format. A spyro checkout still on its own copy of the old queue (a worktree
mid-run, an old branch) and a port on this one therefore never run beside each other, and keep their arrival order
between them. Callers of the old queue from before #255 use bare `flock` and can still jump ahead of waiters, as
they could before.

- **spyro-vita**: vendor `tools/docker`; its `tools/docker/run.sh` becomes a shim that maps its old switches and
  execs the kit's: `SPYRO_DQ=0` or a `TC_PORT` sets `ENCORE_DQ=0`, `SPYRO_DOCKER_HOST` sets `ENCORE_DOCKER_HOST`,
  `ENCORE_COLIMA_PROFILE` defaults to `kh1fast` until the machine's settings file says so. `SPYRO_DQ_LOCK`
  goes (a private lock is `ENCORE_DOCKER_LOCK`). `docs/host-build.md` points here.
- **kh1-vita**: `tools/host/docker/run.sh` wraps itself in `queue.py` as above (it runs `docker run` in the
  background under its own traps, and keeps `KH1_DOCKER_HOST`); `tools/host/docker/build.sh` likewise.
- **atv2-vita**: `tools/decomp/harness/{build,test,run}.sh` and `tools/gsreplay/docker/run.sh` call `run.sh`
  instead of `docker run` (or wrap themselves in `queue.py` where they build an image first).
- **Afterwards**, once no spyro checkout has the old `run.sh` (every worktree merged or gone), the default of
  `ENCORE_DOCKER_ALSO_LOCK` becomes empty.

## Test

`tools/test/docker_queue_test.sh` runs both scripts with private locks and a fake `docker`: three queued jobs run
one at a time in arrival order; a killed holder, a dead waiter's ticket, a ticket whose PID now names another
process and a malformed ticket are cleared; an older-queue ticket and an older-queue `flock` holder are waited for,
and a running job holds the older lock too; a nested call and `ENCORE_DQ=0` do not wait; `run.sh` picks the profile
only when every bind mount is inside its folders, reads the settings file (the environment wins) and adds the
limits. It starts no container.
