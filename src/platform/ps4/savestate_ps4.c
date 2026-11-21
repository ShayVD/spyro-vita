/* Save states on the PS4 (src/game/savestate.c, decisions D51, D194): the
 * menu's SAVE rows (slot, save, load) and the short notice shown after a
 * save or load, as the Vita's savestate_vita.c, and the platform hooks
 * (linux32's savestate_host.c's: the SPU mixes on the game thread here too;
 * and the GPU renderer told when VRAM was replaced). The states go to
 * /data/spyro/save/state<N>.bin (the disc's region's folder, D132), next to
 * the memory card. */
#include "ps4.h"
#include "host.h"
#include "../../game/savestate.h"
#include "psx_region.h"
#include "menu_opts.h"
#include "menu_panel.h"
#include "psx_spu.h"
#include "psx_state.h"

#include <stdio.h>
#include <stdlib.h>

#define NOTICE_TICKS 60 /* about 2 s */

void host_set_vblank(int vblank); /* vblank.c */

/* ---- the hooks src/game/savestate.c calls ---- */

void savestate_plat_set_vblank(int vblank) {
  int was = ps4_vblank_count();
  host_set_vblank(vblank);
  host_log("savestate: vblank counter %d -> %d", was, vblank);
}

void savestate_plat_audio(int mode) { (void)mode; }
void savestate_plat_audio_done(int mode) {
  if (mode == PSX_STATE_LOAD)
    psx_spu_pcm_reset(); /* SPU RAM replaced: the native mix decodes its PCM again */
}

/* the CPU's VRAM (gpu_soft.c's), which both renderers draw from */
uint16_t *savestate_plat_vram(int mode) {
  (void)mode;
  return host_vram();
}

void savestate_plat_vram_done(int mode) {
  if (mode == PSX_STATE_LOAD)
    ps4_gpu_vram_loaded(); /* the GPU's copy and its targets made again */
}

/* ---- the menu's SAVE section (menu_opts.h, D45) ---- */

static int s_slot = 1;           /* config.txt state_slot= */
static char s_contents[32] = ""; /* what the slot holds, for LOAD STATE's value */
static const char *const k_contents[] = {s_contents};

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

/* At every main loop top, after any save or load: the notice. */
static void tick(void) {
  static uint8_t *panel;
  static int serial, notice;
  int s;
  const char *msg = savestate_message(&s);
  if (s != serial) {
    int h = menu_panel_height(0);
    serial = s;
    host_log("savestate: %s (vblank %d)", msg, ps4_vblank_count());
    host_log_flush();
    refresh();
    if (!panel)
      panel = malloc((size_t)MENU_PANEL_W * h * 3);
    if (panel) {
      menu_panel_draw(panel, msg, NULL, 0, -1, 0, 0, "");
      ps4_video_overlay(panel, MENU_PANEL_W, h);
      notice = NOTICE_TICKS;
    }
  } else if (notice > 0 && --notice == 0) {
    ps4_video_overlay(NULL, 0, 0);
  }
}

void ps4_savestate_init(void) {
  char dir[256];
  savestate_set_dir(psx_region_path(dir, sizeof dir, PS4_DATA_DIR "/save", NULL, 1));
  g_savestate_tick_hook = tick;
  if (s_slot < 1 || s_slot > SAVESTATE_SLOTS)
    s_slot = 1;
  refresh();
  host_log("savestate: slot %d in %s (%s)", s_slot, dir, s_contents);
}
