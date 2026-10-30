/* Level tour for the headless linux32 host (--tour LIST, --tour-stay N): the
 * same sequence as the oracle's capture.lua ORACLE_TOUR, so host and
 * emulator visit the same levels the same way (docs/whole-game-host.md).
 * Once the --pad script is done and Spyro is playing (gamestate 0) for N
 * vblanks (default 300; at most 60 in the flight levels, x5, where Spyro
 * flies into the water and drowns if left alone), take the next entry of
 * LIST (comma separated):
 *   NN       warp to level NN with the game's level-warp cheat: Pause, Down,
 *            Down, Cross opens the inventory, where CheatProcessLevelWarp
 *            (cheats.c) runs while g_LevelCheatActive; it takes the world
 *            digit, then the level digit (CIRCLE 0, CROSS 1, SQUARE 2,
 *            TRIANGLE 3, RIGHT 4, DOWN 5, LEFT 6).
 *   credits  the credits cheat (cheats.c CheatTable[2]) typed in the
 *            inventory: InitCreditsSequence(0), then back to this level.
 *   levels   the LEVELS OPEN cheat (CheatTable[3]): every level visited,
 *            every homeworld unlocked.
 *   lives    the 99 LIVES cheat (CheatTable[4]).
 *   end2     what Gnasty Gnorc's moby does when he is beaten
 *            (moby_update.c, state 9): cutscene 2, then the credits.
 *   end3     what Gnasty's Loot's moby 217 does with 14000 gems: cutscene 3,
 *            then the full credits (InitCreditsSequence(1)).
 * In the flight results (gamestate 7, the timer ran out) Cross is pressed so
 * the tour can go on. Leaving a level writes tour-K-LEVEL.png (and with
 * --moby-dump tour-K-LEVEL.tsv) in the --shot-dir, K the entry's index; the
 * oracle writes tour-K-LEVEL.raw at the same point. */
#include "host.h"
#include "psx_arena.h"
#include "psx_hooks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* psx.map */
#define A_GAMESTATE 0x800757D8u
#define A_LEVEL_ID 0x8007596Cu
#define A_LEVEL_CHEAT_ACTIVE 0x80075880u
#define A_CHEAT_HOMEWORLD 0x800758ECu
#define A_CUTSCENE_IDX 0x8007566Cu
#define A_CUTSCENE_LAYOUT 0x80075680u
#define A_CREDITS_SEQUENCE 0x800757ACu
#define A_STATE_SWITCH 0x8007579Cu
#define A_SCREEN_BORDER 0x8007570Cu
#define A_SPYRO_HIDDEN 0x80075814u
#define A_GEM_TOTAL 0x80075860u

#define TOUR_MAX 64
#define SEQ_MAX 64
#define TOUR_CREDITS -1
#define TOUR_END2 -2
#define TOUR_END3 -3
#define TOUR_LEVELS -4
#define TOUR_LIVES -5

enum { EV_PAD, EV_CHEAT, EV_END2, EV_END3 };
typedef struct {
  int t, kind;
  uint16_t mask;
} TourEv;

static int s_tour[TOUR_MAX], s_n, s_i, s_stay = 300;
static int s_t0 = -1, s_at = -1, s_len;
static TourEv s_seq[SEQ_MAX];
static int s_nseq;
static int s_mask = -1;

static uint32_t *ram(uint32_t a) { return (uint32_t *)PSX_ARENA(a); }

int host_tour_set(const char *list) {
  char buf[512], *tok;
  snprintf(buf, sizeof buf, "%s", list);
  for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    if (s_n == TOUR_MAX)
      return -1;
    if (strcmp(tok, "credits") == 0)
      s_tour[s_n++] = TOUR_CREDITS;
    else if (strcmp(tok, "end2") == 0)
      s_tour[s_n++] = TOUR_END2;
    else if (strcmp(tok, "end3") == 0)
      s_tour[s_n++] = TOUR_END3;
    else if (strcmp(tok, "levels") == 0)
      s_tour[s_n++] = TOUR_LEVELS;
    else if (strcmp(tok, "lives") == 0)
      s_tour[s_n++] = TOUR_LIVES;
    else if (atoi(tok) >= 10 && atoi(tok) <= 64)
      s_tour[s_n++] = atoi(tok);
    else
      return -1;
  }
  return 0;
}

void host_tour_set_stay(int vblanks) { s_stay = vblanks; }

static void ev(int t, int kind, const char *pad) {
  uint16_t mask = 0;
  if (s_nseq == SEQ_MAX)
    return;
  if (pad)
    host_pad_parse(pad, &mask);
  s_seq[s_nseq].t = t;
  s_seq[s_nseq].kind = kind;
  s_seq[s_nseq++].mask = mask;
}

/* Pause, Down, Down, Cross: the inventory. */
static void open_inventory(void) {
  ev(0, EV_PAD, "start");
  ev(6, EV_PAD, "none");
  ev(60, EV_PAD, "down");
  ev(66, EV_PAD, "none");
  ev(100, EV_PAD, "down");
  ev(106, EV_PAD, "none");
  ev(140, EV_PAD, "cross");
  ev(146, EV_PAD, "none");
}

static const char *const k_digit[] = {"circle", "cross", "square", "triangle",
                                      "right",  "down",  "left"};

/* cheats.c CheatTable[2..4], typed in the inventory one press at a time. */
static const char *const k_cheat_credits[] = {"left", "right", "up", "down", "circle", "circle",
                                              "square", "r1", "r2", "l1", "l2", NULL};
static const char *const k_cheat_levels[] = {"square", "square", "circle", "square", "left",
                                             "right", "left", "right", "circle", "up",
                                             "right", "down", NULL};
static const char *const k_cheat_lives[] = {"square", "square", "square", "square", "square",
                                            "square", "circle", "up", "circle", "left",
                                            "circle", "right", "circle", NULL};

static void start(int entry) {
  s_nseq = 0;
  s_len = 400;
  if (entry == TOUR_CREDITS || entry == TOUR_LEVELS || entry == TOUR_LIVES) {
    const char *const *code = entry == TOUR_CREDITS  ? k_cheat_credits
                              : entry == TOUR_LEVELS ? k_cheat_levels
                                                     : k_cheat_lives;
    int k;
    open_inventory();
    for (k = 0; code[k]; k++) {
      ev(200 + 20 * k, EV_PAD, code[k]);
      ev(206 + 20 * k, EV_PAD, "none");
    }
    /* Triangle leaves the inventory for the pause menu (func_8002EB2C),
     * Start leaves that for the game; unless the credits took over */
    if (entry != TOUR_CREDITS) {
      ev(200 + 20 * k + 40, EV_PAD, "triangle");
      ev(206 + 20 * k + 40, EV_PAD, "none");
      ev(200 + 20 * k + 100, EV_PAD, "start");
      ev(206 + 20 * k + 100, EV_PAD, "none");
    }
    s_len = 200 + 20 * k + 160;
  } else if (entry == TOUR_END2 || entry == TOUR_END3) {
    ev(0, entry == TOUR_END2 ? EV_END2 : EV_END3, NULL);
    s_len = 0;
  } else {
    open_inventory();
    ev(200, EV_CHEAT, NULL);
    ev(220, EV_PAD, k_digit[entry / 10]);
    ev(226, EV_PAD, "none");
    ev(260, EV_PAD, k_digit[entry % 10]);
    ev(266, EV_PAD, "none");
  }
}

static void ending(int idx) {
  if (idx == 3)
    *ram(A_GEM_TOTAL) = 14000;
  else
    *ram(A_CREDITS_SEQUENCE) = 0;
  *ram(A_CUTSCENE_IDX) = idx;
  *ram(A_GAMESTATE) = 14; /* GS_Cutscene */
  *ram(A_STATE_SWITCH) = 1;
  *ram(A_SCREEN_BORDER) = 0;
  *ram(A_SPYRO_HIDDEN) = 0;
  *ram(*ram(A_CUTSCENE_LAYOUT)) = 0; /* m_CurrentTick, offset 0 */
}

int host_tour_done(void) { return s_i >= s_n && s_at < 0; }

/* The buttons the tour holds this vblank, or -1 to leave the pad alone. */
int host_tour_vblank(int vblank, int pad_done) {
  int gs, level;
  if (s_n == 0 || !pad_done)
    return -1;
  gs = (int)*ram(A_GAMESTATE);
  level = (int)*ram(A_LEVEL_ID);
  if (s_at >= 0) {
    int t = vblank - s_at;
    for (int k = 0; k < s_nseq; k++) {
      if (s_seq[k].t != t)
        continue;
      if (s_seq[k].kind == EV_PAD) {
        s_mask = s_seq[k].mask;
      } else if (s_seq[k].kind == EV_CHEAT) {
        *ram(A_LEVEL_CHEAT_ACTIVE) = 1;
        *ram(A_CHEAT_HOMEWORLD) = 0;
      } else {
        ending(s_seq[k].kind == EV_END2 ? 2 : 3);
        host_log("tour: ending %d set up at vblank %d",
                 s_seq[k].kind == EV_END2 ? 2 : 3, vblank);
      }
    }
    if (t >= s_len)
      s_at = -1;
    return s_mask;
  }
  if (gs == 7) { /* flight results: Cross, every second */
    s_t0 = -1;
    return vblank % 60 < 6 ? 0x4000 : 0;
  }
  if (s_i >= s_n)
    return -1;
  if (gs != 0) {
    s_t0 = -1;
    return -1;
  }
  if (s_t0 < 0)
    s_t0 = vblank;
  if (vblank - s_t0 < (level % 10 == 5 && s_stay > 60 ? 60 : s_stay))
    return -1;
  host_log("tour: leaving level %d for %d at vblank %d", level, s_tour[s_i],
           vblank);
  {
    char path[1024];
    snprintf(path, sizeof path, "%s/tour-%02d-%d.png", g_host_opts.shot_dir, s_i, level);
    host_shot_display(path);
    if (g_host_opts.moby_dump) {
      snprintf(path, sizeof path, "%s/tour-%02d-%d.tsv", g_host_opts.shot_dir, s_i, level);
      spyro_moby_dump(path);
    }
  }
  start(s_tour[s_i++]);
  s_t0 = -1;
  s_at = vblank + 1; /* [0] applies next vblank */
  return -1;
}
