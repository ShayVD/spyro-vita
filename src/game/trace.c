/* Per-vblank game-state trace (docs/porting-plan.md §7.3, layer 2): one TSV
 * row per vblank, the same fields the oracle's Lua script logs from the
 * emulator, so tools/oracle can report the first divergent frame and field. */
#include "psx_hooks.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdio.h>

#include "common.h"
#include "cyclorama.h"
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

void spyro_trace_vblank(int vblank) {
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

void spyro_trace_close(void) {
  if (s_trace)
    fclose(s_trace);
  s_trace = NULL;
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
