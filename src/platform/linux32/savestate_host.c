/* Save states on the linux32 host (src/game/savestate.c, decisions D51): the
 * platform hooks, and the test options. --state V:save:N or V:load:N asks
 * for a save or load of slot N at vblank V (it happens at the next main loop
 * top), the requests in the order given; --state-hash FILE writes each chunk's hash at every loop top, so two
 * runs can be compared tick by tick. A load sets the vblank counter back, and
 * the pad script (absolute lines) rewinds with it: the same buttons as the
 * first time from there on. */
#include "host.h"
#include "psx_state.h"
#include "savestate.h"

#include <stdio.h>
#include <string.h>

#define MAX_REQ 16

static struct {
  int vblank, load, slot, done;
} s_req[MAX_REQ];
static int s_nreq;
static FILE *s_hash;

void host_set_vblank(int vblank); /* vblank.c */
void host_pad_rewind(int vblank); /* pad.c */

void savestate_plat_set_vblank(int vblank) {
  int was = host_vblank_count();
  host_set_vblank(vblank);
  host_pad_rewind(vblank);
  host_log("savestate: vblank counter %d -> %d", was, vblank);
}

/* The host mixes on the game thread: g_psx_spu is complete. */
void savestate_plat_audio(int mode) { (void)mode; }
void savestate_plat_audio_done(int mode) { (void)mode; }

uint16_t *savestate_plat_vram(int mode) {
  (void)mode;
  return host_vram();
}

void savestate_plat_vram_done(int mode) { (void)mode; }

static void tick(void) {
  static int serial;
  int i, v = host_vblank_count(), s;
  const char *msg = savestate_message(&s);
  if (s != serial) {
    serial = s;
    host_log("savestate: %s (vblank %d)", msg, v);
  }
  if (s_hash) {
    char tags[16][5];
    uint32_t h[16];
    int n = savestate_hash(tags, h, 16);
    fprintf(s_hash, "%d", v);
    for (i = 0; i < n; i++)
      fprintf(s_hash, "\t%.4s=%08x", tags[i], (unsigned)h[i]);
    fputc('\n', s_hash);
    fflush(s_hash);
  }
  /* requests in the order given: the next one, once its vblank has come
   * (after a load the counter is back, so a later entry can name an earlier
   * vblank of the replayed stretch) */
  for (i = 0; i < s_nreq && s_req[i].done; i++)
    ;
  if (i < s_nreq && v >= s_req[i].vblank) {
    s_req[i].done = 1;
    host_log("savestate: %s slot %d asked at vblank %d", s_req[i].load ? "load" : "save",
             s_req[i].slot, v);
    savestate_request(s_req[i].load, s_req[i].slot);
  }
}

static void hook(void) {
  if (!g_savestate_tick_hook)
    g_savestate_tick_hook = tick;
}

int host_state_option(const char *val) {
  char what[8];
  int v, slot;
  if (s_nreq == MAX_REQ || sscanf(val, "%d:%7[a-z]:%d", &v, what, &slot) != 3 ||
      (strcmp(what, "save") && strcmp(what, "load")) || slot < 1 || slot > SAVESTATE_SLOTS)
    return -1;
  s_req[s_nreq].vblank = v;
  s_req[s_nreq].load = what[0] == 'l';
  s_req[s_nreq++].slot = slot;
  hook();
  return 0;
}

int host_state_hash_open(const char *path) {
  s_hash = fopen(path, "w");
  if (!s_hash)
    return -1;
  hook();
  return 0;
}
