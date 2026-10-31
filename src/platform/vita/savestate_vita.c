/* Save states on the Vita (src/game/savestate.c, decisions D51): the
 * platform hooks, the menu's SAVE rows (slot, save, load), the short notice
 * shown after a save or load, and config.txt's state_test= for scripted
 * Vita3K runs. The states go to ux0:data/spyro/save/state<N>.bin, next to
 * the memory card. */
#include "vita.h"
#include "../../game/savestate.h"
#include "menu_opts.h"
#include "menu_panel.h"
#include "psx_state.h"

#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t *soft_host_vram(void); /* linux32/gpu_soft.c, the gpu=0 renderer */

#define NOTICE_TICKS 60 /* about 2 s */
#define MAX_TESTS 8

void savestate_plat_set_vblank(int vblank) {
  int was = vita_vblank_count();
  vita_set_vblank(vblank);
  vita_pad_rewind(vblank);
  vita_log("savestate: vblank counter %d -> %d", was, vblank);
}

static SceUInt64 s_t0; /* when the save or load began, for the log */

void savestate_plat_audio(int mode) {
  s_t0 = sceKernelGetProcessTimeWide();
  vita_mixer_drain(); /* nothing in flight; the XA FIFO is this thread's */
  if (mode == PSX_STATE_SAVE)
    vita_mixer_state(0);
}

void savestate_plat_audio_done(int mode) {
  if (mode == PSX_STATE_LOAD) {
    vita_mixer_state(1);
    vita_audio_clear();
  }
  /* a save's chunks are written by now; only the close and rename are left */
  vita_log("savestate: %s took %u ms", mode == PSX_STATE_LOAD ? "load" : "save",
           (unsigned)((sceKernelGetProcessTimeWide() - s_t0) / 1000));
}

uint16_t *savestate_plat_vram(int mode) {
  (void)mode;
  return g_vita_gpu ? vita_gpu_vram_take() : soft_host_vram();
}

void savestate_plat_vram_done(int mode) {
  if (mode == PSX_STATE_LOAD && g_vita_gpu)
    vita_gpu_vram_loaded();
}

/* ---- the menu's SAVE section (menu_opts.h, D45) ---- */

static int s_slot = 1;            /* config.txt state_slot= */
static char s_contents[32] = "";  /* what the slot holds, for LOAD STATE's value */
static const char *const k_contents[] = {s_contents};

/* The slot's header again (after a slot change, a save or a load). */
static void refresh(void) {
  SaveStateInfo info;
  const char *msg;
  if (savestate_info(s_slot, &info, &msg) == 0)
    snprintf(s_contents, sizeof s_contents, "%s", savestate_label(&info));
  else
    snprintf(s_contents, sizeof s_contents, "%s", msg);
}

/* The save or load happens at the next main loop top, so the menu closes. */
static int save_now(void) {
  savestate_request(0, s_slot);
  return 1;
}

static int load_now(void) {
  savestate_request(1, s_slot);
  return 1;
}

static const MenuOption k_rows[] = {
    {.section = MENU_SAVE, .label = "STATE SLOT", .key = "state_slot", .value = &s_slot,
     .min = 1, .max = SAVESTATE_SLOTS, .on_change = refresh},
    {.section = MENU_SAVE, .label = "SAVE STATE", .action = save_now},
    {.section = MENU_SAVE, .label = "LOAD STATE", .names = k_contents, .count = 1,
     .action = load_now},
};
MENU_OPTION_LIST(g_menu_save, k_rows);

/* state_test=V:save:N,V:load:N,...: ask at vblank V (tests without a pad). */
static struct {
  int vblank, load, slot, done;
} s_test[MAX_TESTS];
static int s_ntests;

void vita_savestate_config(const char *v) {
  char buf[512], *tok;
  snprintf(buf, sizeof buf, "%s", v);
  for (tok = strtok(buf, ","); tok && s_ntests < MAX_TESTS; tok = strtok(NULL, ",")) {
    char what[8];
    int vb, slot;
    if (sscanf(tok, "%d:%7[a-z]:%d", &vb, what, &slot) != 3 ||
        (strcmp(what, "save") && strcmp(what, "load"))) {
      vita_log("config: bad state_test entry %s", tok);
      continue;
    }
    s_test[s_ntests].vblank = vb;
    s_test[s_ntests].load = what[0] == 'l';
    s_test[s_ntests++].slot = slot;
  }
}

/* At every main loop top, after any save or load. */
static void tick(void) {
  static uint8_t *panel;
  static int serial, notice;
  int s, i, v = vita_vblank_count();
  const char *msg = savestate_message(&s);
  if (s != serial) {
    int h = menu_panel_height(0);
    serial = s;
    vita_log("savestate: %s (vblank %d)", msg, v);
    refresh();
    if (!panel)
      panel = malloc((size_t)MENU_PANEL_W * h * 3);
    if (panel) {
      menu_panel_draw(panel, msg, NULL, 0, -1, 0, 0, "");
      vita_video_overlay(panel, MENU_PANEL_W, h);
      notice = NOTICE_TICKS;
    }
  } else if (notice > 0 && --notice == 0) {
    vita_video_overlay(NULL, 0, 0);
  }
  for (i = 0; i < s_ntests; i++)
    if (!s_test[i].done && v >= s_test[i].vblank) {
      s_test[i].done = 1;
      vita_log("savestate: state_test %s slot %d at vblank %d", s_test[i].load ? "load" : "save",
               s_test[i].slot, v);
      savestate_request(s_test[i].load, s_test[i].slot);
      break;
    }
}

void vita_savestate_init(void) {
  savestate_set_dir(VITA_DATA_DIR "/save");
  g_savestate_tick_hook = tick;
  if (s_slot < 1 || s_slot > SAVESTATE_SLOTS)
    s_slot = 1;
  refresh();
}
