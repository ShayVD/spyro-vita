#!/usr/bin/env bash
# Which of clang's four __ptr32 bugs (lp64/docs/clang-bugs.md) a given clang still has, and whether each workaround
# still compiles. For each reproduction: PRESENT (with how it shows: wrong code, a crash, a hang) or fixed.
#   lp64/tools/clang_bugs/check.sh [--expect present|fixed] [--timeout SECONDS] [CLANG]
# CLANG defaults to clang (any clang that can target x86_64-linux-gnu: Debian's clang-19, OpenOrbis's LLVM 18, a new
# release). Exit 0; 2 when a workaround fails to compile (the kit's way around a bug no longer works); 1 with --expect
# when a verdict differs (the kit's tests expect clang 19 to have all of them).
# From spyro-vita tools/ps4/clang_bugs/ @ c25a3ea (MIT; D174) and @ 5c6021c (bug 4, its PS4 build; D193): its
# reproductions, run as a check.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
expect="" limit=20
while [[ $# -gt 0 ]]; do
  case $1 in
    --expect) expect=$2; shift 2 ;;
    --timeout) limit=$2; shift 2 ;;
    *) break ;;
  esac
done
read -r -a clang <<< "${1:-clang}"
o=$(mktemp -d "${TMPDIR:-/tmp}/clang_bugs.XXXXXX")
trap 'rm -rf "$o"' EXIT
cc=("${clang[@]}" --target=x86_64-linux-gnu -fms-extensions -O1 -I"$here/../../runtime/p32" -w)
if ! version=$("${clang[@]}" --version 2> /dev/null | head -n 1) || [[ -z $version ]]; then
  echo "clang_bugs: can't run ${clang[*]}"; exit 2
fi
command -v timeout > /dev/null || { echo "clang_bugs: needs timeout (coreutils; gtimeout on macOS)"; exit 2; }
echo "clang: $version"
present=0 fixed=0 bad=0 wrong=0

# verdict NAME WHAT STATUS [DETAIL]
verdict() {
  printf '%-13s %-62s %s\n' "$1" "($2)" "$3${4:+: $4}"
  if [[ $3 == PRESENT* ]]; then present=$((present + 1)); [[ $expect == fixed ]] && wrong=$((wrong + 1))
  else fixed=$((fixed + 1)); [[ $expect == present ]] && wrong=$((wrong + 1)); fi
}

# compile NAME [FLAGS...]: exit status in $rc (124 when it ran past the limit), first error line in $err
compile() {
  local name=$1; shift
  timeout "$limit" "${cc[@]}" "$@" "$here/$name.c" -o "$o/$name.out" 2> "$o/$name.err"
  rc=$?
  err=$(grep -m 1 -E 'error|Cannot select|LLVM ERROR|Unsupported' "$o/$name.err" | cut -c 1-80)
}

# 1a: wrong code. An indirect jump or call through the 4-byte slot itself loads 8 bytes.
compile fnptr_call -S
if [[ $rc -ne 0 ]]; then
  verdict fnptr_call "a call through a __ptr32 function pointer" "PRESENT (crash)" "$err"
elif line=$(grep -m 1 -E '(jmp|call)q?[[:space:]]+\*g_fn(\(%rip\))?' "$o/fnptr_call.out"); then
  verdict fnptr_call "a call through a __ptr32 function pointer" "PRESENT (8-byte load)" "$(echo $line)"
else
  verdict fnptr_call "a call through a __ptr32 function pointer" "fixed"
fi
# 1b: the same where the call isn't a tail call: the backend crashes
compile fnptr_crash -c
if [[ $rc -ne 0 ]]; then
  verdict fnptr_crash "the same, not a tail call" "PRESENT (crash, exit $rc)" "$err"
elif "${clang[@]}" --target=x86_64-linux-gnu -fms-extensions -O1 -w -S "$here/fnptr_crash.c" -o - \
    | grep -q -E '(jmp|call)q?[[:space:]]+\*g_(fn|tab)'; then
  verdict fnptr_crash "the same, not a tail call" "PRESENT (8-byte load)"
else
  verdict fnptr_crash "the same, not a tail call" "fixed"
fi
# 2: ?: mixing a __ptr32 and a normal pointer
compile ternary_hang -c
if [[ $rc -eq 124 ]]; then
  verdict ternary_hang "?: mixing a __ptr32 and a normal pointer" "PRESENT (hang)" "still compiling after ${limit} s"
elif [[ $rc -ne 0 ]]; then
  verdict ternary_hang "?: mixing a __ptr32 and a normal pointer" "PRESENT (crash, exit $rc)" "$err"
else
  verdict ternary_hang "?: mixing a __ptr32 and a normal pointer" "fixed"
fi
# 3: a __ptr32 static initializer holding an address
compile static_init -c
if [[ $rc -ne 0 ]]; then
  verdict static_init "a __ptr32 static initializer with an address" "PRESENT (crash, exit $rc)" "$err"
else
  verdict static_init "a __ptr32 static initializer with an address" "fixed"
fi

# 4: the loop vectorizer under AVX on a copy into a __ptr32 array (P32_INIT's constructors)
compile vector_cast -c -O2 -march=btver2
if [[ $rc -ne 0 ]]; then
  verdict vector_cast "a vectorized copy into a __ptr32 array (AVX, -O2)" "PRESENT (crash, exit $rc)" "$err"
else
  verdict vector_cast "a vectorized copy into a __ptr32 array (AVX, -O2)" "fixed"
fi

# the workarounds must compile (and 1a's must load 4 bytes); bug 4's both ways: the pragma, and -fno-vectorize
wflags_vector_cast="-O2 -march=btver2"
for n in fnptr_call fnptr_crash ternary_hang static_init vector_cast vector_cast_fnv; do
  if [[ $n == vector_cast_fnv ]]; then
    compile vector_cast -S -O2 -march=btver2 -fno-vectorize; n=vector_cast
  else
    v=wflags_$n
    # shellcheck disable=SC2086
    compile "$n" -DWORKAROUND -S ${!v:-}
  fi
  if [[ $rc -ne 0 ]]; then
    echo "workaround $n: FAILED to compile${err:+: $err}"; bad=$((bad + 1))
  elif grep -q -E '(jmp|call)q?[[:space:]]+\*g_(fn|tab)' "$o/$n.out"; then
    echo "workaround $n: FAILED, still a call through the 4-byte slot"; bad=$((bad + 1))
  fi
done
[[ $bad -eq 0 ]] && echo "workarounds: all 6 compile"

echo "clang_bugs: $present present, $fixed fixed"
[[ $bad -eq 0 ]] || exit 2
[[ $wrong -eq 0 ]] || { echo "clang_bugs: $wrong verdicts differ from --expect $expect"; exit 1; }
exit 0
