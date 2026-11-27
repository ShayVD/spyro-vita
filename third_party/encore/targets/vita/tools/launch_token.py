#!/usr/bin/env python3
"""Write a launch-token request (targets/vita/runtime/launchgate) from the current launch's log: the last
`PREFIX: armed ...; token PID NONCE` line's token, as the request file's one line.

  targets/vita/tools/launch_token.py RUN/game.log --output present-start.req [--prefix vpk-presenttrace]

Upload the file to the path the game polls (e.g. ux0:data/<game>/present-start.req) only for the launch this log is
from; success is the game's `start accepted` line in the log, not the FTP client's size check (the game may consume
the file before the check), so never resend automatically.
From kh1-vita scripts/present_trace_request.py @ 75afd241. Python 3, stdlib.
"""
import argparse
import re
from pathlib import Path


def token(log, prefix="vpk-presenttrace"):
    tokens = re.findall(re.escape(prefix) + r": armed [^\n]*?token ([0-9a-f]{8}) (\d+)", log)
    return " ".join(tokens[-1]) if tokens else None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("log", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--prefix", default="vpk-presenttrace")
    a = p.parse_args()
    t = token(a.log.read_text(errors="replace"), a.prefix)
    if not t:
        p.error("no `%s: armed ... token` line: enable the capture and fetch the current launch's log" % a.prefix)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(t + "\n")
    print(f"request saved to {a.output}; use it only with the launch this log is from")


if __name__ == "__main__":
    main()
