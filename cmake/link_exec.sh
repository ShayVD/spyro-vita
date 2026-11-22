#!/bin/sh
# Link launcher of the linux32 host build (RULE_LAUNCH_LINK in CMakeLists.txt;
# issue #163): sh cmake/link_exec.sh OUTPUT -- LINK-COMMAND...
#
# GNU ld makes its output executable only after it has written and closed it:
# stat(path), then chmod(path, mode | 0111 & ~umask), and no chmod at all if
# that stat fails (binutils bfd/opncls.c, _maybe_make_executable). tools/tc's
# /work is the Mac's folder through colima's reverse-sshfs mount, and there
# that last step is now and then lost: in logs/run163-race 1 link of 60 kept
# mode 0644 while other processes stat'ed the same path (the build has many
# links and lima's inotify agent looking at the tree). qemu-i386, the tests'
# emulator, refuses a file without an execute bit, so a normal test fails and
# a WILL_FAIL one "passed" without running.
#
# This runs the link, then checks that an executable has its bits, sets them
# again if not (as ld meant to, with the umask), and fails the link if they
# still don't hold, so a build never ends with a binary that can't run.
out=$1
shift 2
"$@" || exit $?
case $out in
  *.a | *.so | *.so.*) exit 0 ;;
esac
try=0
while [ ! -x "$out" ]; do
  try=$((try + 1))
  if [ $try -gt 5 ]; then
    echo "link_exec.sh: $out still has no execute bit after the link" >&2
    exit 1
  fi
  echo "link_exec.sh: $out lost its execute bit after the link (try $try), setting it" >&2
  chmod +x "$out" 2>/dev/null || sleep 1
done
