/* Per-vblank game-state trace (docs/porting-plan.md §7.3, layer 2): one TSV
 * row per vblank, the same fields the oracle's Lua script logs from the
 * emulator, so tools/oracle can report the first divergent frame and field. */
#include "psx_hooks.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdio.h>

#include "common.h"
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
