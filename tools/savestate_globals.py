#!/usr/bin/env python3
"""Save states (src/game/savestate.c, decisions D51) save the arena plus a
short list of the game's host globals. This checks that the list is complete:
every data or bss symbol the game's objects define must be in the arena (an
absolute symbol in the linked binary, decisions D23), or in the native
build in the data labels' section (s1_labels to s1_labels_end, saved whole;
decisions D106), saved by savestate.c, or known not to change after
start-up.

  savestate_globals.py --nm NM --bin SPYRO OBJ...
"""
import argparse
import subprocess
import sys

# A file's static is named "file.c:name".
# chunk_globals() in src/game/savestate.c, with port_cheats_state()
SAVED = {"g_mips_sp", "g_mips_v1",
         "cheats_port.c:s_pending", "cheats_port.c:s_level", "cheats_port.c:s_age",
         "cheats_port.c:s_title_done",  # the queued cheat (D50)
         "gteRegs",  # chunk_gpu(); ours since stage 8c (gte_regs.c, D116)
         # tick60_state() (the "T60 " chunk, D130): the mode, and of the rules'
         # state the half tick and Spyro's carry; the rest is set every update
         "g_tick60_mode", "g_t60",
         # chunk_region() (the "REGN" chunk, D134): PAL's language, screen
         # position, language slots and card-header state (region_game.c)
         "g_Language", "g_ScreenOffsetX", "g_ScreenOffsetY", "g_WadCutscenesPal",
         "g_LevelDragonsPal", "g_PalCardHeaderDirty", "g_PalTitleCardLoad", "g_PalLetterCount",
         "g_PalCutsceneCarry"}  # PAL 60's cutscene clock (D150)
# Written once at start-up or never: initialised tables and pointers.
CONSTANT = {
    "CheatTable",               # cheats.c: the cheat button sequences, read only
    "g_OverlaySpacePointer",    # overlay_pointers.c: &main_BSS_END, never written
    "g_DragonNames", "g_HomeworldNames", "g_HomeworldNamesLength", "g_LevelNames",
    "g_BalloonistNames", "g_BalloonistDialogueIndices", "g_BalloonistDialogue",  # strings.c
    "g_m2c_seed",               # oracle_replay.c (tests only)
    "cheats_port.c:s_log",      # the log function, set at start-up
    "camera_hand.c:s_tilt",     # set before the springs, cleared after (D47)
    "tick60_update.o.c:s_tilt", # the same, in the 60 Hz rules' copy (D130)
    # the 60 Hz rules' scratch, set at every update before use (t60_rules.c, D130)
    "s_t60_delta", "s_t60_list30", "s_t60_list60",
    "g_t60_stats", "g_t60_stats_on",  # tick60.c: --tick60-stats' counters, the log only
    "g_fast_render",            # mips2c_rt.c: set at start-up, before any game code (D72)
    "env_hand.c:s_proj",        # read from the GTE at the start of every EnvDraw, before
                                # use; a state is never taken inside a draw (D80, D124)
    "port.c:s_kuseg_sink",      # the native build's sink for stray stores, never read (D106)
    # a record's faces copied while the presenter draws beside the game's draw (D126):
    # written by ReplaceFaces, emptied by RestoreFaces in the same record, never across ticks
    "moby_hand.c:s_faces", "moby_hand.c:s_faces_on",
    # the region's data copied from the disc's executable once, at start-up, before any
    # game code; the game only reads it (region_data.c, D133)
    "region_data.c:s_loaded",
}
# The player's settings, which a load keeps, as it keeps the memory card.
SETTINGS = {
    "g_port_controls",          # port.c: free camera, invert, speed (D47, D142)
    "g_port_skip_logos",        # cheats_port.c (D50)
    "g_psyxRtpHook", "g_psyxRtpValueHook",  # gte_ops.c (D116): the vertex cache's hooks,
                                # set by the picture options (psx_vcache_enable, D39, D89)
}
CONSTANT_PREFIXES = ("Terminator_",)  # ovl_table.c: fixed arena addresses (D20)
# Port files whose state isn't the game's: the presenter (D30), the oracle
# replay and trace (tests), save states themselves, the draw distance
# (lod.c, D49: picture only; per draw, or reset on load by port_lod_reset;
# g_lod is the player's setting, kept), the renderers' per-call check
# (hand_check.c, host tests, D80, D127), and the native draw lists (native_draw.c,
# D82: the switch, and records that live from a renderer call to its list's
# walk, which a state taken at the loop top never holds; its check, tests),
# and the walk check (walk_check.c, D117: host tests, the reference walk's
# copy of the walk's state, synced before every packet), and the presenter's
# builds and context (NAME_hand_pres.c, lod_pres.c, render_ctx*.c, D123:
# set before each presenter call from the game's, never read by the game;
# the check, host tests), and the renderers' working memory (render_rc.c,
# D124: a renderer call's scratch, which nothing reads after the call), and the presenter's draw (src/game/pres/, D125: its
# snapshot and context taken from the game's at every frame it draws, never
# read by the game; its record serial, which only its own side table uses,
# emptied each frame; the check and the audit, host tests).
NOT_GAME = ("/present.c.", "/oracle_replay.c.", "/trace.c.", "/savestate.c.", "/lod.c.",
            "/hand_check.c.", "/native_draw.c.", "/native_draw_check.c.", "/walk_check.c.",
            "_pres.c.", "/render_ctx.c.", "/render_ctx_check.c.", "/render_rc.c.", "/pres/pres_snap.c.",
            "/pres/pres_draw.c.", "/pres/pres_check.c.")
DATA_TYPES = set("BbCDdGgSsVv")


def nm(tool, path):
    """(type, name, address or None) for each symbol."""
    out = subprocess.run([tool, "-S", path], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and len(parts[-2]) == 1:
            yield parts[-2], parts[-1], int(parts[0], 16) if len(parts) >= 3 and len(parts[0]) == 8 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--bin", required=True)
    ap.add_argument("objs", nargs="+")
    a = ap.parse_args()
    syms = list(nm(a.nm, a.bin))
    linked = {name: t for t, name, _ in syms}
    where = {name: adr for _, name, adr in syms}
    # the native build's labels' section (tools/data2s.py --image, D106)
    lo, hi = where.get("s1_labels"), where.get("s1_labels_end")

    def in_labels(name):
        return lo is not None and hi is not None and where.get(name) is not None \
            and lo <= where[name] < hi
    bad, n = [], 0
    for obj in a.objs:
        if any(x in obj for x in NOT_GAME):
            continue
        for t, name, _ in nm(a.nm, obj):
            if t not in DATA_TYPES or name.startswith(".L"):
                continue
            n += 1
            src = obj.rsplit("/", 1)[-1].split(".c.", 1)[0] + ".c"
            known = SAVED | CONSTANT | SETTINGS
            if name in known or f"{src}:{name}" in known or name.startswith(CONSTANT_PREFIXES):
                continue
            if t.isupper() and linked.get(name) == "A":
                continue  # a data label in the arena
            if t.isupper() and in_labels(name):
                continue  # a data label in the native build's labels' section (D106)
            if name.endswith("__p") and linked.get(name[:-3]) == "A":
                continue  # a moved arena's pointer to that label, set at start-up (D25, D97)
            bad.append(f"{obj}: {t} {name}")
    if bad:
        print("game globals outside the arena that save states don't save (add them to "
              "chunk_globals in src/game/savestate.c, or to this script's CONSTANT list):")
        print("\n".join(bad))
        return 1
    print(f"savestate_globals: {n} data symbols in {len(a.objs)} objects, all covered")
    return 0


if __name__ == "__main__":
    sys.exit(main())
