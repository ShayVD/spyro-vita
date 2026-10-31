/* Deterministic virtual time for the headless linux32 host
 * (docs/porting-plan.md §5): VSync(0) is exactly one vblank, nothing runs on
 * a timer. Also PsyX's scene hooks, which here only handle isbg clears. */
#include "cheats_port.h"
#include "host.h"
#include "lod.h"
#include "psx_arena.h"
#include "psx_hooks.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "psx/libetc.h"
#include "PsyX/PsyX_render.h"
#include "PsyX/PsyX_public.h"
#include "gpu/PsyX_GPU.h"

HostOpts g_host_opts = {.shot_dir = "."};

extern void (*vsync_callback)(void); /* LIBETC.C */

int g_vmode = MODE_NTSC;
static int s_vblank;

int host_vblank_count(void) { return s_vblank; }
void host_set_vblank(int vblank) { s_vblank = vblank; }

int PsyX_Sys_GetVBlankCount(void) { return s_vblank; }

int PsyX_Sys_SetVMode(int mode) {
  int old = g_vmode;
  g_vmode = mode;
  return old;
}

static int shot_due(int v) {
  int i;
  if (g_host_opts.shot_every > 0 && v % g_host_opts.shot_every == 0)
    return 1;
  for (i = 0; i < g_host_opts.nshots; i++)
    if (g_host_opts.shots[i] == v)
      return 1;
  return 0;
}

void host_vblank(void) {
  int v = ++s_vblank;

  for (int i = 0; i < g_host_opts.npokes; i++)
    if (g_host_opts.pokes[i].vblank == v) {
      *(uint32_t *)PSX_ARENA(g_host_opts.pokes[i].addr) = g_host_opts.pokes[i].value;
      host_log("poke %08x = %08x", g_host_opts.pokes[i].addr,
               g_host_opts.pokes[i].value);
    }
  for (int i = 0; i < g_host_opts.ncheats; i++)
    if (g_host_opts.cheats[i].vblank == v &&
        !port_cheat_request(g_host_opts.cheats[i].cheat, g_host_opts.cheats[i].level))
      host_log("cheat %d refused at vblank %d (not available now)", g_host_opts.cheats[i].cheat, v);
  host_pad_vblank(v);
  psx_cd_vblank();
  psx_spu_vblank();
  if (vsync_callback)
    vsync_callback();
  if (g_host_opts.out960)
    host_present_vblank();

  if (shot_due(v)) {
    char path[1024];
    snprintf(path, sizeof path, "%s/shot-%d.png", g_host_opts.shot_dir, v);
    if (host_shot_display(path) == 0)
      host_log("shot %s", path);
    if (g_host_opts.vram_shot) {
      snprintf(path, sizeof path, "%s/vram-%d.png", g_host_opts.shot_dir, v);
      host_shot_vram(path);
    }
    if (g_host_opts.moby_dump) {
      snprintf(path, sizeof path, "%s/mobys-%d.tsv", g_host_opts.shot_dir, v);
      spyro_moby_dump(path);
    }
    for (int i = 0; i < g_host_opts.npeeks; i++) {
      char line[600];
      int len = snprintf(line, sizeof line, "peek %08x:", g_host_opts.peeks[i].addr);
      for (int k = 0; k < g_host_opts.peeks[i].n && len < (int)sizeof line - 10; k++)
        len += snprintf(line + len, sizeof line - len, " %08x",
                        *(uint32_t *)PSX_ARENA(g_host_opts.peeks[i].addr + 4 * k));
      host_log("%s", line);
    }
  }
  if (g_host_opts.lod_stats && v % g_host_opts.lod_stats == 0) {
    char line[400];
    port_lod_report(line, sizeof line);
    if (line[0])
      host_log("lod %d: %s", v, line);
  }
  spyro_trace_vblank(v);
  host_serve_vblank(v);
  host_step_vblank(v);
  if (g_host_opts.frames > 0 && v >= g_host_opts.frames) {
    host_log("--frames %d reached, exiting", g_host_opts.frames);
    host_audio_close();
    spyro_trace_close();
    host_log_flush();
    fflush(stdout);
    exit(0);
  }
}

void PsyX_WaitForTimestep(int count) {
  while (count-- > 0)
    host_vblank();
}

void PsyX_UpdateInput(void) {}

/* DrawOTag/DrawPrim call this first. Draw anything DrawPrim left pending so
 * GPU order holds, then do the isbg clear once per PutDrawEnv (the real
 * PutDrawEnv clears when it is called; LIBGPU's copy of the env is ours to
 * mark as done). Returning 0 stops DrawOTag from dropping pending splits. */
char PsyX_BeginScene(void) {
  if (g_splitIndex > 0)
    DrawAllSplits();
  if (activeDrawEnv.isbg) {
    const RECT16 *c = &activeDrawEnv.clip;
    GR_ClearVRAM(c->x, c->y, c->w, c->h, activeDrawEnv.r0, activeDrawEnv.g0,
                 activeDrawEnv.b0);
    activeDrawEnv.isbg = 0;
  }
  return 0;
}

void PsyX_EndScene(void) {}
