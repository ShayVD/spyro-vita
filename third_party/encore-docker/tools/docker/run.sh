#!/usr/bin/env bash
# `docker run ARGS...` on this machine's Docker VM, through its one queue (queue.py): one container at a time, first
# come, first served, whichever port started it. A drop-in for `docker run` in a game's tools/tc and the like.
#   tools/docker/run.sh --rm -v "$PWD:$PWD" -w "$PWD" my-image make
# From spyro-vita tools/docker/run.sh @ 663ceeb (PRs #253, #255), with spyro's values made settings. README.md has
# the settings, how a game uses this and how spyro-vita, kh1-vita and atv2-vita switch over.
#
# Settings come from the environment, else from ~/.config/encore/docker.env (ENCORE_DOCKER_CONFIG; KEY=VALUE lines,
# one machine's choices for every port; a leading ~ is the home directory; nothing else is expanded):
#   ENCORE_DOCKER_HOST     the Docker endpoint, always (overrides the profile below)
#   ENCORE_COLIMA_PROFILE  a colima profile (its socket ~/.colima/PROFILE/docker.sock) to run on when the socket
#                          exists and every bind-mount source is inside a folder the profile mounts; else DOCKER_HOST
#   ENCORE_COLIMA_MOUNTS   those folders, ':'-separated (default: read from the profile's colima.yaml; colima's
#                          default, the home directory, when it lists none)
#   ENCORE_DOCKER_CPUS     --cpus for every container, unless the caller passes its own (default: none, the whole VM)
#   ENCORE_DOCKER_MEMORY   --memory likewise
#   ENCORE_DOCKER_PULL     --pull (default never: images must already be on that VM; empty for docker's default)
# and queue.py's: ENCORE_DOCKER_LOCK, ENCORE_DOCKER_ALSO_LOCK, ENCORE_DQ=0, ENCORE_DQ_LABEL, ENCORE_DQ_POLL.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)

conf=${ENCORE_DOCKER_CONFIG:-$HOME/.config/encore/docker.env}
if [ -f "$conf" ]; then
  while IFS= read -r line; do
    case $line in ENCORE_*=*) ;; *) continue ;; esac
    k=${line%%=*} v=${line#*=}
    case $k in *[!A-Z0-9_]*) continue ;; esac
    eval "set_already=\${$k+x}"
    [ -n "$set_already" ] && continue
    case $v in "~") v=$HOME ;; "~/"*) v=$HOME/${v#\~/} ;; esac
    export "$k=$v"
  done < "$conf"
fi

# A bind-mount source as an absolute path, or nothing for a named volume.
source_of() {
  local s=$1
  case $s in
    /*) printf '%s' "$s" ;;
    "~"|"~/"*) printf '%s' "$HOME${s#\~}" ;;
    .|./*|..|../*) (cd "$(dirname "$s")" 2> /dev/null && printf '%s/%s' "$(pwd)" "$(basename "$s")") || printf '%s' "$PWD/$s" ;;
  esac
}

# The folders colima profile $1 mounts, one a line.
profile_mounts() {
  if [ -n "${ENCORE_COLIMA_MOUNTS:-}" ]; then
    tr ':' '\n' <<< "$ENCORE_COLIMA_MOUNTS"
    return
  fi
  local yaml="$HOME/.colima/$1/colima.yaml" found=
  if [ -f "$yaml" ]; then
    found=$(awk '/^mounts:/ { m = 1; next } m && /^[^ #-]/ { m = 0 } m && /location:/ { sub(/.*location:[ ]*/, ""); gsub(/["'\'']/, ""); print }' "$yaml")
  fi
  [ -n "$found" ] || found=$HOME
  while IFS= read -r m; do
    case $m in "~") m=$HOME ;; "~/"*) m=$HOME/${m#\~/} ;; esac
    printf '%s\n' "${m%/}"
  done <<< "$found"
}

# 0 if every bind-mount source in the docker run arguments is inside one of the folders on stdin.
mounts_inside() {
  local folders srcs=() prev='' a s src ok f
  folders=$(cat)
  for a in "$@"; do
    s=
    case $prev in -v|--volume) s=${a%%:*} ;; --mount) s=$a ;; esac
    case $a in --volume=*) s=${a#--volume=}; s=${s%%:*} ;; -v?*) s=${a#-v}; s=${s%%:*} ;; --mount=*) s=${a#--mount=} ;; esac
    case $s in *source=*|*src=*) s=,$s; s=${s#*,source=}; s=${s#*,src=}; s=${s%%,*} ;; esac
    [ -n "$s" ] && srcs+=("$s")
    prev=$a
  done
  for s in ${srcs[@]+"${srcs[@]}"}; do
    src=$(source_of "$s")
    [ -n "$src" ] || continue
    ok=0
    while IFS= read -r f; do
      [ -n "$f" ] || continue
      case "$src/" in "$f"/*) ok=1; break ;; esac
    done <<< "$folders"
    [ "$ok" = 1 ] || { echo "encore docker: $src is outside ${ENCORE_COLIMA_PROFILE}'s mounts" >&2; return 1; }
  done
}

if [ -n "${ENCORE_DOCKER_HOST:-}" ]; then
  export DOCKER_HOST=$ENCORE_DOCKER_HOST
elif [ -n "${ENCORE_COLIMA_PROFILE:-}" ]; then
  sock=$HOME/.colima/$ENCORE_COLIMA_PROFILE/docker.sock
  current=${DOCKER_HOST:-the default endpoint}   # (no apostrophe inside "${..:-..}": bash 3.2 reads it as a quote)
  if [ ! -S "$sock" ]; then
    echo "encore docker: colima profile $ENCORE_COLIMA_PROFILE is not running; using $current" >&2
  elif profile_mounts "$ENCORE_COLIMA_PROFILE" | mounts_inside "$@"; then
    export DOCKER_HOST=unix://$sock
  else
    echo "encore docker: using $current instead" >&2
  fi
fi

extra=()
has() { local a o=$1; shift; for a in "$@"; do case $a in "$o"|"$o"=*) return 0 ;; esac; done; return 1; }
pull=${ENCORE_DOCKER_PULL-never}
[ -n "$pull" ] && ! has --pull "$@" && extra+=(--pull "$pull")
[ -n "${ENCORE_DOCKER_CPUS:-}" ] && ! has --cpus "$@" && extra+=(--cpus "$ENCORE_DOCKER_CPUS")
[ -n "${ENCORE_DOCKER_MEMORY:-}" ] && ! has --memory "$@" && ! has -m "$@" && extra+=(--memory "$ENCORE_DOCKER_MEMORY")

exec python3 -I "$here/queue.py" docker run ${extra[@]+"${extra[@]}"} "$@"
