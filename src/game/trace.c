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
#include "psx_arena.h"
#include "psx_mem.h"
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
static void anim_trace_vblank(int vblank);

void spyro_trace_vblank(int vblank) {
  moby_trace_vblank(vblank);
  cutscene_trace_vblank(vblank);
  anim_trace_vblank(vblank);
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
  fprintf(s_moby_trace, "vblank\tlevel\tslot\tclass\tx\ty\tz\trx\try\trz\tstate\tsub\tanim\tframe\tprog\tper\n");
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
    fprintf(s_moby_trace, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", vblank, g_LevelId, i,
            m->m_Class, m->m_Position.x, m->m_Position.y, m->m_Position.z, m->m_Rotation.x,
            m->m_Rotation.y, m->m_Rotation.z, m->m_State, m->m_Substate,
            m->m_AnimationState.m_Animation, m->m_AnimationState.m_Frame,
            m->m_AnimationState.m_FrameProgress, m->m_AnimationState.m_PerFrameProgress);
  }
}

/* --anim-trace (D155): in play, Spyro's body animation each vblank (its
 * animation and frame, the ones it blends into, and the 16ths between
 * them): how fast PAL's shorter animations play at US timing. */
static FILE *s_anim_trace;

int spyro_anim_trace_open(const char *path) {
  s_anim_trace = fopen(path, "w");
  if (!s_anim_trace)
    return -1;
  fprintf(s_anim_trace, "vblank\tlevel\tstate\tanim\tnext\tframe\tnframe\tprog\tspeed\n");
  return 0;
}

static void anim_trace_vblank(int vblank) {
  if (!s_anim_trace || g_Gamestate != GS_Playing || g_LoadStage >= 0)
    return;
  fprintf(s_anim_trace, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", vblank, g_LevelId, g_Spyro.m_State,
          g_Spyro.m_bodyAnimation, g_Spyro.m_nextBodyAnimation, g_Spyro.m_bodyAnimationFrame,
          g_Spyro.m_nextBodyAnimationFrame, g_Spyro.m_bodyFrameProgress, g_Spyro.m_bodyAnimationSpeed);
}

/* --cutscene-trace (D151): in the intro and endings (gamestate 14) and a
 * dragon's talk (gamestate 8, its state 4), each vblank, the cutscene's tick, the camera and each cutscene moby's position,
 * frame and frame progress: how evenly PAL's 25 Hz data plays at 30 Hz. */
static FILE *s_cut_trace, *s_cut_keys, *s_cut_mid;
static int s_cut_vblank;

int spyro_cutscene_trace_open(const char *path) {
  char keys[1024];
  s_cut_trace = fopen(path, "w");
  if (!s_cut_trace)
    return -1;
  snprintf(keys, sizeof keys, "%s.keys", path);
  s_cut_keys = fopen(keys, "w");
  snprintf(keys, sizeof keys, "%s.mid", path);
  s_cut_mid = fopen(keys, "w");
  if (s_cut_mid)
    fprintf(s_cut_mid, "vblank\tslot\tx\ty\tz\tframe\tprogress\n");
  fprintf(s_cut_trace, "vblank\tcuttick\tslot\tx\ty\tz\trx\try\trz\tframe\tprogress\n");
  return 0;
}

/* FILE.mid (D156): the in-between frame's cutscene mobys as the presenter
 * is about to draw them (present.c's blend), with the vblank of the last
 * row traced: is each one's origin its path's point at its pose's time? */
void spyro_cutscene_trace_mid(const Moby *mobys, int n) {
  int i;
  if (!s_cut_mid || g_Gamestate != GS_Cutscene || !g_CutsceneLayout || g_LoadStage < 10)
    return;
  for (i = 0; i < g_CutsceneLayout->m_MobyCount && i < 16 && i < n; i++)
    fprintf(s_cut_mid, "%d\t%d\t%d\t%d\t%d\t%d\t%d\n", s_cut_vblank, i, mobys[i].m_Position.x,
            mobys[i].m_Position.y, mobys[i].m_Position.z,
            mobys[i].m_AnimationState.m_Animation * 64 + mobys[i].m_AnimationState.m_Frame,
            mobys[i].m_AnimationState.m_FrameProgress);
}

static void cutscene_trace_vblank(int vblank) {
  int i;
  s_cut_vblank = vblank;
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
  /* the intro and endings, and the title's flyby (gamestate 13, D155).
   * Gamestate 13 is also the load after a cutscene, where the layout's
   * buffer is being written over: only a layout that reads as one */
  if (!s_cut_trace || (g_Gamestate != GS_Cutscene && g_Gamestate != GS_TitleScreen) ||
      !g_CutsceneLayout || (g_Gamestate == GS_Cutscene && g_LoadStage < 10))
    return;
  if (g_Gamestate == GS_TitleScreen &&
      ((unsigned)g_CutsceneLayout->m_MobyCount > 16 || (unsigned)g_CutsceneLayout->m_Duration - 1 > 4094 ||
       (unsigned)g_CutsceneLayout->m_CurrentTick > 2u * (unsigned)g_CutsceneLayout->m_Duration))
    return;
  if (s_cut_keys && g_Gamestate == GS_Cutscene) {
    /* FILE.keys, once (D156): the first cutscene's models as the disc has
     * them, the first four vertex words (z, x, y packed) of every frame of
     * every animation: how a release's keyframes move from one to the next */
    fprintf(s_cut_keys, "slot\tanim\tframe\tspeed\tv0\tv1\tv2\tv3\n");
    for (i = 0; i < g_CutsceneLayout->m_MobyCount && i < 16; i++) {
      const Model *model = g_Models[1 + i];
      int k, f;
      if ((int)(uintptr_t)model >= 0)
        continue;
      for (k = 0; k < model->m_NumAnimations; k++) {
        const AnimationHeader *anim = model->m_Animations[k];
        for (f = 0; f < anim->m_NumFrames; f++) {
          const u_int *v = (const u_int *)MEM_ANIM21(anim->m_Frames[f].m.m_Data);
          fprintf(s_cut_keys, "%d\t%d\t%d\t%d\t%08x\t%08x\t%08x\t%08x\n", i, k, f,
                  anim->m_ProgressPerTick, v[0], v[1], v[2], v[3]);
        }
      }
    }
    fclose(s_cut_keys);
    s_cut_keys = NULL;
  }
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
  if (s_anim_trace)
    fclose(s_anim_trace);
  s_anim_trace = NULL;
  if (s_cut_mid)
    fclose(s_cut_mid);
  s_cut_mid = NULL;
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
  /* each model's animations: frames and progress a tick (64 a frame) */
  for (i = 1; i < MODEL_COUNT; i++) {
    const Model *model = g_Models[i];
    int k;
    if ((int)(uintptr_t)model >= 0 || model->m_NumAnimations <= 0)
      continue; /* none, or a SimpleModel (its word has bit 31 cleared) */
    fprintf(f, "# model %d anims %d:", i, model->m_NumAnimations);
    for (k = 0; k < model->m_NumAnimations && k < 64; k++)
      if (model->m_Animations[k])
        fprintf(f, " %d/%d", model->m_Animations[k]->m_NumFrames, model->m_Animations[k]->m_ProgressPerTick);
    fprintf(f, "\n");
  }
  for (i = 0; i < g_PortalCount && i < 6 && g_Portals[i]; i++)
    fprintf(f, "# portal %d level %d center %d %d %d point0 %d %d %d path_moby %d\n", i,
            g_Portals[i]->m_LevelId, g_Portals[i]->m_Center.x, g_Portals[i]->m_Center.y,
            g_Portals[i]->m_Center.z, g_Portals[i]->m_Points[0].x, g_Portals[i]->m_Points[0].y,
            g_Portals[i]->m_Points[0].z, g_Portals[i]->m_PathMoby);
  fclose(f);
}
