#!/usr/bin/env python3
"""vita-port-kit's hw_mock.py (third_party/vita-port-kit/vita/tools/hw_mock.py: its usage
there) with this game's folders (tools/vita/hw.game: ux0:data/spyro, SPYV00001)."""
import os
import runpy
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.environ.setdefault("HW_GAME", os.path.join(ROOT, "tools", "vita", "hw.game"))
KIT = os.path.join(ROOT, "third_party", "vita-port-kit", "vita", "tools", "hw_mock.py")
sys.argv[0] = KIT
runpy.run_path(KIT, run_name="__main__")
