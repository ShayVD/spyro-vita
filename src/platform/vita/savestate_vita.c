/* Save states on the Vita (src/game/savestate.c, decisions D51): the
 * platform hooks, the short notice shown after a save or load, and
 * config.txt's state_test= for scripted Vita3K runs. The states go to
 * ux0:data/spyro/save/state<N>.bin, next to the memory card. */
#include "vita.h"
#include "../../game/savestate.h"
#include "menu_panel.h"
#include "psx_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t *soft_host_vram(void); /* linux32/gpu_soft.c, the gpu=0 renderer */

#define NOTICE_TICKS 60 /* about 2 s */
#define MAX_TESTS 8

void savestate_plat_set_vblank(int vblank) {
  int was = vita_vblank_count();
  vita_set_vblank(vblank);
  vita_log("savestate: vblank counter %d -> %d", was, vblank);
}

void savestate_plat_audio(int mode) {
  vita_mixer_drain(); /* nothing in flight; the XA FIFO is this thread's */
  if (mode == PSX_STATE_SAVE)
    vita_mixer_state(0);
}

void savestate_plat_audio_done(int mode) {
  if (mode != PSX_STATE_LOAD)
    return;
  vita_mixer_state(1);
  vita_audio_clear();
}

uint16_t *savestate_plat_vram(int mode) {
  (void)mode;
  return g_vita_gpu ? vita_gpu_vram_take() : soft_host_vram();
}

void savestate_plat_vram_done(int mode) {
  if (mode == PSX_STATE_LOAD && g_vita_gpu)
    vita_gpu_vram_loaded();
}

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
    if (!panel)
      panel = malloc((size_t)MENU_PANEL_W * h * 3);
    if (panel) {
      menu_panel_draw(panel, msg, NULL, 0, -1, "");
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
}
