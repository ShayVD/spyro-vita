/* Per-vblank game-state trace (docs/porting-plan.md §7.3, layer 2): one TSV
 * row per vblank, the same fields the oracle's Lua script logs from the
 * emulator, so tools/oracle can report the first divergent frame and field. */
#include "psx_hooks.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdio.h>

#include "common.h"
#include "camera.h"
#include "cutscene.h"
#include "cyclorama.h"
#include "dragon.h"
#include "environment.h"
#include "loaders.h"
#include "moby.h"
#include "spyro.h"
#include "variables.h"

extern unsigned int psyq_rand_seed(void); /* rand_psyq.c */

static FILE *s_trace;

int spyro_trace_open(const char *path) {
  s_trace = fopen(path, "w");
  if (!s_trace)
    return -1;
  fprintf(s_trace, "vblank\tgamestate\tlevel\ttick\tx\ty\tz\tstate\thealth\t"
                   "gems\tdragons\tlives\trand\n");
  return 0;
}

static void moby_trace_vblank(int vblank);
static void cutscene_trace_vblank(int vblank);

void spyro_trace_vblank(int vblank) {
  moby_trace_vblank(vblank);
  cutscene_trace_vblank(vblank);
  if (!s_trace)
    return;
  fprintf(s_trace, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%08x\n",
          vblank, g_Gamestate, g_LevelId, g_GameTick, g_Spyro.m_Position.x,
          g_Spyro.m_Position.y, g_Spyro.m_Position.z, g_Spyro.m_State,
          g_Spyro.m_health, g_GemTotal, g_DragonTotal, g_SpyroLifeCount,
          psyq_rand_seed());
  if ((vblank & 63) == 0)
    fflush(s_trace);
}

/* --moby-trace (60 Hz logic's statistics, D130, tools/tick60_stats.py):
 * in play, every live moby within 0x8000 of Spyro, each vblank: its slot,
 * class, position, rotation, state, substate and animation. */
static FILE *s_moby_trace;

int spyro_moby_trace_open(const char *path) {
  s_moby_trace = fopen(path, "w");
  if (!s_moby_trace)
    return -1;
  fprintf(s_moby_trace, "vblank\tlevel\tslot\tclass\tx\ty\tz\trx\try\trz\tstate\tsub\tanim\tframe\n");
  return 0;
}

static void moby_trace_vblank(int vblank) {
  int i;
  if (!s_moby_trace || g_Gamestate != GS_Playing || g_LoadStage >= 0 || !g_LevelMobys)
    return;
  for (i = 0; g_LevelMobys[i].m_State != 0xff && i < 4096; i++) {
    const Moby *m = &g_LevelMobys[i];
    int dx = m->m_Position.x - g_Spyro.m_Position.x, dy = m->m_Position.y - g_Spyro.m_Position.y;
    if (m->m_State >= 0x80 || dx < -0x8000 || dx > 0x8000 || dy < -0x8000 || dy > 0x8000)
      continue;
    fprintf(s_moby_trace, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", vblank, g_LevelId, i,
            m->m_Class, m->m_Position.x, m->m_Position.y, m->m_Position.z, m->m_Rotation.x,
            m->m_Rotation.y, m->m_Rotation.z, m->m_State, m->m_Substate,
            m->m_AnimationState.m_Animation, m->m_AnimationState.m_Frame);
  }
}

/* --cutscene-trace (D151): in the intro and endings (gamestate 14) and a
 * dragon's talk (gamestate 8, its state 4), each vblank, the cutscene's tick, the camera and each cutscene moby's position,
 * frame and frame progress: how evenly PAL's 25 Hz data plays at 30 Hz. */
static FILE *s_cut_trace;

int spyro_cutscene_trace_open(const char *path) {
  s_cut_trace = fopen(path, "w");
  if (!s_cut_trace)
    return -1;
  fprintf(s_cut_trace, "vblank\tcuttick\tslot\tx\ty\tz\trx\try\trz\tframe\tprogress\n");
  return 0;
}

static void cutscene_trace_vblank(int vblank) {
  int i;
  if (s_cut_trace && g_Gamestate == GS_Dragon && g_DragonCutscene.m_State == 4) {
    /* a dragon's talk: slot -1 the camera, 0 and 1 its two mobys */
    Moby *const m2[2] = {g_DragonCutscene.m_CutsceneSpyro, g_DragonCutscene.m_CutsceneDragon};
    fprintf(s_cut_trace, "%d\t%d\t-1\t%d\t%d\t%d\t%d\t%d\t%d\t0\t0\n", vblank,
            g_DragonCutscene.m_CutsceneTicks, g_Camera.m_Position.x, g_Camera.m_Position.y,
            g_Camera.m_Position.z, g_Camera.m_Rotation.x, g_Camera.m_Rotation.y,
            g_Camera.m_Rotation.z);
    for (i = 0; i < 2; i++)
      if (m2[i])
        fprintf(s_cut_trace, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", vblank,
                g_DragonCutscene.m_CutsceneTicks, i, m2[i]->m_Position.x, m2[i]->m_Position.y,
                m2[i]->m_Position.z, m2[i]->m_Rotation.x, m2[i]->m_Rotation.y,
                m2[i]->m_Rotation.z, m2[i]->m_AnimationState.m_Frame,
                m2[i]->m_AnimationState.m_FrameProgress);
    return;
  }
  if (!s_cut_trace || g_Gamestate != GS_Cutscene || !g_CutsceneLayout || g_LoadStage < 10)
    return;
  fprintf(s_cut_trace, "%d\t%d\t-1\t%d\t%d\t%d\t%d\t%d\t%d\t0\t0\n", vblank,
          g_CutsceneLayout->m_CurrentTick, g_Camera.m_Position.x, g_Camera.m_Position.y,
          g_Camera.m_Position.z, g_Camera.m_Rotation.x, g_Camera.m_Rotation.y, g_Camera.m_Rotation.z);
  for (i = 0; i < g_CutsceneLayout->m_MobyCount && i < 16; i++) {
    const Moby *m = &g_LevelMobys[i];
    fprintf(s_cut_trace, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", vblank,
            g_CutsceneLayout->m_CurrentTick, i, m->m_Position.x, m->m_Position.y,
            m->m_Position.z, m->m_Rotation.x, m->m_Rotation.y, m->m_Rotation.z,
            m->m_AnimationState.m_Animation * 64 + m->m_AnimationState.m_Frame,
            m->m_AnimationState.m_FrameProgress);
  }
  if ((vblank & 63) == 0)
    fflush(s_cut_trace);
}

void spyro_trace_close(void) {
  if (s_cut_trace)
    fclose(s_cut_trace);
  s_cut_trace = NULL;
  if (s_trace)
    fclose(s_trace);
  s_trace = NULL;
  if (s_moby_trace)
    fclose(s_moby_trace);
  s_moby_trace = NULL;
}

/* The level's mobys (class, position, state) as TSV, for finding things to
 * walk to in scripted runs (--moby-dump). Only in the level's own gamestates
 * and not while it loads (g_LoadStage >= 0): the title, cutscenes and the
 * credits (13-15) reuse the buffers, and a loading level's tables are
 * half-written. */
void spyro_moby_dump(const char *path) {
  FILE *f;
  int i;
  if (g_Gamestate >= GS_TitleScreen || g_LoadStage >= 0 || !g_LevelMobys ||
      !(f = fopen(path, "w")))
    return;
  fprintf(f, "index\tclass\tx\ty\tz\tstate\tspyro_x\tspyro_y\tspyro_z\tgems\trot_z\n");
  for (i = 0; g_LevelMobys[i].m_State != 0xff && i < 4096; i++)
    fprintf(f, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", i, g_LevelMobys[i].m_Class,
            g_LevelMobys[i].m_Position.x, g_LevelMobys[i].m_Position.y, g_LevelMobys[i].m_Position.z,
            g_LevelMobys[i].m_State, g_Spyro.m_Position.x, g_Spyro.m_Position.y, g_Spyro.m_Position.z,
            g_GemTotal, g_LevelMobys[i].m_Rotation.z);
  for (i = 0; i < g_Environment.m_SurfaceCount && g_Environment.m_SurfaceData; i++)
    fprintf(f, "# surface %d type %d\n", i, g_Environment.m_SurfaceData[i]->m_Type);
  for (i = 0; i < g_PortalCount && i < 6 && g_Portals[i]; i++)
    fprintf(f, "# portal %d level %d center %d %d %d point0 %d %d %d path_moby %d\n", i,
            g_Portals[i]->m_LevelId, g_Portals[i]->m_Center.x, g_Portals[i]->m_Center.y,
            g_Portals[i]->m_Center.z, g_Portals[i]->m_Points[0].x, g_Portals[i]->m_Points[0].y,
            g_Portals[i]->m_Points[0].z, g_Portals[i]->m_PathMoby);
  fclose(f);
}
