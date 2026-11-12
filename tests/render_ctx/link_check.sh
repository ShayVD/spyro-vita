#!/bin/sh
# Usage: link_check.sh NM OBJECT... (stage 7d, D123; tests/render_ctx/CMakeLists.txt)
# The presenter's objects (NAME_hand_pres.c, lod_pres.c)
# may need from outside only the presenter's own state and functions
# (g_pres_*, pres_*) and what they only read: the level's and the game's
# data the draw reads, read-only tables, the C library's copies.
nm=$1; shift
objs=
# the objects come as one CMake list (;-separated) or as arguments
for o in $(echo "$@" | tr ';' ' '); do
  case $o in *_pres.c.o|*_pres.c.obj) objs="$objs $o";; esac
done
[ -n "$objs" ] || { echo "render_ctx_link: no presenter objects among the arguments"; exit 1; }
n=$(echo $objs | wc -w)
[ "$n" -ge 8 ] || { echo "render_ctx_link: only $n presenter objects"; exit 1; }
# read only (each checked: no store through the name in the presenter's code;
# --render-ctx-check traps any store into the arena). g_Region (RV(): the
# lit mobys' screen centre, D147) is set once, when the disc is opened at
# start-up (psx_cd_open), before any draw or presenter thread.
allow='^(g_pres_|pres_)
^(memcpy|memset|snprintf|mips2c_div|__divdi3|__udivdi3)$
^g_psx_arena_base$
^port_ws_(hud_ofx|hud_r11|r1112)$
^port_poly_start$
^(g_Camera|g_Cyclorama|g_DeltaTime|g_Environment|g_EnvironmentAnimations|g_Models|g_SkipLowPolyWorld|g_Spyro)$
^(g_Buffers|g_CurDB|g_DB|g_LevelId|g_Gamestate)$
^g_Region$
^D_8006(CBF8|CC78|CF48|CF58|CF98|CFC8|D058|D0D8|D0E8|D0F8|D138|D1C8|D378|D388|D3C8|D5C8|D5E4|D614|D808|D908|D94C|DAA8|E1A8|E268|E3D8|E44C)$
^D_800(74B84|756F0|76248|76278|770C8|7AA10)$'
bad=$("$nm" -u $objs | awk 'NF == 2 && $1 == "U" {print $2} NF == 1 && $1 !~ /:$/ {print $1}' | sort -u | grep -v -E "$(echo "$allow" | tr '\n' '|' | sed 's/|$//')")
if [ -n "$bad" ]; then
  echo "render_ctx_link: presenter objects need names that aren't the presenter's or read-only:"
  echo "$bad" | sed 's/^/  /'
  exit 1
fi
echo "render_ctx_link: $n presenter objects, every outside name the presenter's own or read-only"
