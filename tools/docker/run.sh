#!/usr/bin/env bash
# docker run for tools/tc, tools/pc/tc and tools/ps4/tc, through Encore's one Docker queue per machine (D254): one
# container at a time across every port, first come, first served, on the fastest Docker VM that can see the mounts.
# Takes `docker run`'s arguments. The queue and the VM choice are Encore's tools/docker, vendored in
# third_party/encore-docker (its README.md has the settings); this shim only maps spyro's old switches:
#   SPYRO_DQ=0 or TC_PORT     ENCORE_DQ=0: no waiting (a quick look inside; a --serve a second container talks to)
#   SPYRO_DOCKER_HOST         ENCORE_DOCKER_HOST: this Docker endpoint, always
#   ENCORE_COLIMA_PROFILE     kh1fast unless set here or in ~/.config/encore/docker.env (a Mac without that file)
# SPYRO_DQ_LOCK is gone: a private lock is ENCORE_DOCKER_LOCK. The images must already be on the VM (--pull never);
# docs/host-build.md says how to build or copy them there.
set -euo pipefail
kit="$(cd "$(dirname "$0")/../.." && pwd)/third_party/encore-docker/tools/docker"

if [ "${SPYRO_DQ:-1}" = 0 ] || [ -n "${TC_PORT:-}" ]; then
  export ENCORE_DQ=0
fi
if [ -n "${SPYRO_DOCKER_HOST:-}" ]; then
  export ENCORE_DOCKER_HOST=$SPYRO_DOCKER_HOST
fi
# The environment wins over the settings file, so set the fallback only when the file does not name a profile.
conf=${ENCORE_DOCKER_CONFIG:-$HOME/.config/encore/docker.env}
if [ -z "${ENCORE_COLIMA_PROFILE+x}" ] && ! grep -qs '^ENCORE_COLIMA_PROFILE=' "$conf"; then
  export ENCORE_COLIMA_PROFILE=kh1fast
fi

exec "$kit/run.sh" "$@"
