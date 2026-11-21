#!/usr/bin/env bash
# The owner's yes: 2026-11-21 ("sure run them"): #198 bisect, AP13 with one deferral switched off each.
cd /Users/shayvandam/Projects/spyro-vita
export VITA_IP=192.168.1.4
R=$PWD/runtime/autopilot/vpk T=$PWD/tools/vita/hw-routes/o-toasty.pad F=tools/vita/fps-runs N=autopilot-63d80f4.vpk
toasty() { REALTIME=1 VPK=$R/$N PAD=$T DEVICE_DEFAULTS=1 tools/vita/hw_run.sh logs/hw-$1 1500 @$F/ap2-toasty60-wobble.txt @$F/$2 @tools/vita/hw-routes/o-toasty.cfg > logs/hw-$1.out 2>&1; echo "$(date +%H:%M:%S) $1 rc=$?"; }
echo "$(date +%H:%M:%S) start"
toasty ap15a ap15-pt1.txt
toasty ap15b ap15-lazy0.txt
toasty ap15c ap15-ll0.txt
tools/vita/hw_run.sh --put-eboot $R/$N logs/hw-put-63d80f4b > logs/hw-put-63d80f4b.out 2>&1; echo "$(date +%H:%M:%S) put-eboot rc=$?"; tail -1 logs/hw-put-63d80f4b.out
echo "$(date +%H:%M:%S) done"
