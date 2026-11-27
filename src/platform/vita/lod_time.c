/* dd_budget=auto (#212, the default): the longer draw distance's 60 Hz
 * budget sized from the game thread's measured time, not a fixed guess.
 *
 * Until #212 the budget was LOD_BUDGET_60, 48 KB of primitives a logic draw
 * (#180), on every scene and every build. D202, D204 and D206 made each
 * draw cheaper, but a byte budget can't see that: it backed off at 48 KB
 * however fast the frame had become (hardware session 6, D209: Stone Hill
 * at 2x held at 1x, 271-300 budget back-offs a window, while the game thread
 * was busy 71.5% on average). Here the budget follows the time the frame
 * actually has left. lod.c still does all the limiting: this file only
 * picks the number (lod_set_budget, which clamps it between LOD_BUDGET_60
 * and the span the draw has).
 *
 * The time: once a logic frame, at the start of the next shown draw (lod.c
 * calls frame() through lod_set_budget_hook, on the game thread), the game
 * thread's work for the frame just done: present.c's update time plus its
 * logic draw time (which leaves out the VSync waits, and holds the walk's
 * join: GamestateDraw's DrawSync(0) comes before its VSync) plus any
 * presenter frame drawn on the game thread. Those are g_present_stats'
 * us_update, us_logic and us_pass, the "present" lines' numbers, timed only
 * with stats=1 (the default); with stats=0 the budget stays the fixed 48 KB.
 * At the draw's start the new tick's update is in already and the last
 * draw's logic time too, so a frame's update is kept for one call. Only
 * frames whose draw kept to the budget count (60 Hz logic in play, the
 * option on: lod_last_draw). (Not a wrap of GamestateUpdate, as prof_draw.c
 * has: with 60 Hz logic the update is GamestateUpdate_60, tick60.c.)
 *
 * The target: TARGET_US of the 16.68 ms NTSC frame. The 2.2 ms left are for
 * what the measure doesn't see but the frame still holds: VSync's vblank work
 * on this thread (the pad, the CD and mixer hand-offs, posting the picture),
 * the game loop between the draw and the next update, and the vblank's
 * jitter and the scheduler's (the walk and the presenter threads share the
 * cores).
 *
 * The rule: a frame over the target cuts the budget at once, to the draw's
 * bytes (or the budget, if smaller) times target / used, so the limiter
 * backs off at its next draw. A frame under the target by ROOM_US or more is
 * room; after ROOM_FRAMES of those in a row the budget grows STEP bytes a
 * frame while the room lasts, and only while it is within reach of the draw
 * (under 5/4 of its bytes and 8 KB: the limiter grows the distance only under
 * 7/8 of the budget, so it needs 8/7; never far ahead of what is drawn).
 * Frames between hold it. */
#include "vita.h"
#include "../../game/lod.h"
#include "../../game/present.h"

#define TARGET_US 14500  /* of the 16,683 us a 59.94 Hz frame has */
#define ROOM_US 1000     /* a frame with room: at least this far under the target */
#define ROOM_FRAMES 8    /* frames with room in a row before the budget grows */
#define STEP 0x800       /* 2 KB a frame */
#define REACH_KB 0x2000  /* and 8 KB over 5/4 of the draw at most */

static int s_auto = 1; /* dd_budget=auto */
static int s_room;     /* frames with room in a row */

static void frame(void);

/* main.c's config.txt: dd_budget=auto (1) or a fixed budget (0) */
void vita_lod_time_set_auto(int on) { s_auto = on != 0; }

/* main.c, after vita_present_init (g_present.stats is set): the hook, and
 * the start-up line */
void vita_lod_time_init(void) {
  if (s_auto && g_present.stats)
    lod_set_budget_hook(frame);
  if (s_auto && g_present.stats)
    vita_log("picture: draw distance with 60 Hz logic in play sizes its budget from the game "
             "thread's time per logic frame: under %d.%d ms of the 16.7 ms frame, from %d KB up "
             "to the draw's span (dd_budget=auto, #212)",
             TARGET_US / 1000, TARGET_US % 1000 / 100, LOD_BUDGET_60 / 1024);
  else if (s_auto)
    vita_log("picture: draw distance with 60 Hz logic in play keeps each draw under %d KB of "
             "primitives: stats=0 leaves the frame untimed (dd_budget=auto needs stats=1, #212)",
             g_lod.budget / 1024);
  else if (g_lod.budget)
    vita_log("picture: draw distance with 60 Hz logic in play keeps each draw under %d KB of "
             "primitives (dd_budget=%d, fixed; D175, #212)",
             g_lod.budget / 1024, g_lod.budget == LOD_BUDGET_60 ? 1 : g_lod.budget / 1024);
  else
    vita_log("picture: draw distance with no 60 Hz budget, only the buffer's limits "
             "(dd_budget=0, D175)");
  /* dd_free (#271): this file's budget sets where it starts (a draw over the
   * budget); lod.c's limit() compares that draw with the last 1x draw */
  if (g_lod.budget)
    vita_log("picture: draw distance over the 60 Hz budget %s (dd_free=%d, #271)",
             g_lod.grow_free ? "still grows while each draw writes no more KB and vertex-cache "
                               "records than the last 1x draw"
                             : "holds the extra distance back, as before #271",
             g_lod.grow_free);
}

/* lod.c, at the start of every shown draw with the option on: the frame
 * just done */
static void frame(void) {
  static long long last_update, last_draw, last_passes, last_ticks;
  static int primed, frame_update;
  const PresentStats *s = &g_present_stats;
  const long long du = s->us_update - last_update, dl = s->us_logic + s->us_pass - last_draw,
                  dp = s->logic_passes - last_passes, dt = s->ticks - last_ticks;
  const long long dw = dl + frame_update; /* the last tick's update and its draw */
  int bytes, budget, b, used;
  last_update = s->us_update;
  last_draw = s->us_logic + s->us_pass;
  last_passes = s->logic_passes;
  last_ticks = s->ticks;
  frame_update = du < 0 || du > 1000000 ? 0 : (int)du; /* this tick's, for the next call */
  if (!primed || dp != 1 || dt != 1 || dl < 0 || !lod_last_draw(&bytes, &budget)) {
    /* one draw and one new tick since the last call, and that draw kept
     * to the 60 Hz budget; else no measure (a gamestate change, a load,
     * 30 Hz) */
    primed = 1;
    s_room = 0;
    return;
  }
  used = dw > 1000000 ? 1000000 : (int)dw;
  b = budget;
  if (used > TARGET_US) {
    b = bytes < budget ? bytes : budget;
    b = (int)((long long)b * TARGET_US / used);
    s_room = 0;
  } else if (used <= TARGET_US - ROOM_US) {
    if (++s_room >= ROOM_FRAMES && budget < bytes + bytes / 4 + REACH_KB)
      b = budget + STEP;
  } else {
    s_room = 0;
  }
  lod_set_budget(b, used, TARGET_US);
}
