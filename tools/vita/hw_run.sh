#!/usr/bin/env bash
# vita-port-kit's hw_run.sh (third_party/vita-port-kit/vita/tools/hw_run.sh: its usage and variables there) with this
# game's settings (tools/vita/hw.game). Decision D77; docs/vita-testing.md "Hardware runs from the Mac".
root=$(cd "$(dirname "$0")/../.." && pwd)
HW_GAME=$root/tools/vita/hw.game exec "$root/third_party/vita-port-kit/vita/tools/hw_run.sh" "$@"
