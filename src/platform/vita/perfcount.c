/* Work counters (issue #160, D131): vita-port-kit's vita/runtime/perfcount
 * (third_party/vita-port-kit, vendored). One "vpk-perfcount:" line in
 * spyro.log every 300 vblanks, each counter the window's sum, which the
 * kit's vita/tools/perf_report.py reads and compares between two runs.
 * Vita3K's timing is not evidence, but the work a frame does is the same
 * there and on the Vita (phase 8 of the port autopilot).
 *
 * Every counter is a running total kept where the work happens with a plain
 * add per event (or one per walk), and pulled once a vblank on the game
 * thread into the kit's counters (vpk_pc_add, ~28 atomic adds a vblank):
 * gpu_gl.c's GL calls and the game's draws there (vita_pc.h, g_vpc_gl), the
 * OT walk's packets (gpu_walk.c, one add a walk), the vertex cache's projections and lookups (vcache.c)
 * and their cost (vcache.c's g_vcache_cost and g_vcache_pres_cost, D134: each thread's divisions,
 * probes and table lines, records and lookups added; issue #172) and the presenter's frames (present.c's g_present_stats). The shared code
 * needs nothing of the kit (the host links none), and no hot path has an
 * atomic (D131).
 * Keep the names stable: runs compare by name. */
#include "vita.h"
#include "vita_pc.h"
#include "../../game/present.h"
#include "../common/psx_vcache.h"

int g_vpc[VPC_COUNT] = {[0 ... VPC_COUNT - 1] = -1}; /* -1: not registered, ignored */
unsigned g_vpc_gl[VPC_COUNT];

static const char *const k_names[VPC_COUNT] = {
    [VPC_GL_DRAWS] = "gl.draws",       [VPC_GL_TRIS] = "gl.tris",
    [VPC_GR_DRAWS] = "gr.draws",       [VPC_GR_TRIS] = "gr.tris",
    [VPC_GL_CLEARS] = "gl.clears",     [VPC_TEX_UPLOADS] = "tex.uploads",
    [VPC_TEX_BYTES] = "tex.bytes",     [VPC_VBO_UPLOADS] = "vbo.uploads",
    [VPC_VBO_BYTES] = "vbo.bytes",     [VPC_GL_READS] = "gl.reads",
    [VPC_GL_STATE] = "gl.state",       [VPC_GL_PROGRAMS] = "gl.programs",
    [VPC_GL_TEX_BINDS] = "gl.tex_binds", [VPC_GL_FBO_BINDS] = "gl.fbo_binds",
    [VPC_GL_UNIFORMS] = "gl.uniforms", [VPC_GL_ATTRIBS] = "gl.attribs",
    [VPC_OT_PACKETS] = "ot.packets",   [VPC_VC_PROJ] = "vc.proj",
    [VPC_VC_POLYS] = "vc.polys",       [VPC_VC_VERTS] = "vc.verts",
    [VPC_VC_MISSES] = "vc.misses",     [VPC_PRES_FRAMES] = "pres.frames",
    [VPC_VC_DIV] = "vc.div",           [VPC_VC_PROBE] = "vc.probe",
    [VPC_VC_LINES] = "vc.lines",       [VPC_VCP_DIV] = "vcp.div",
    [VPC_VCP_PROBE] = "vcp.probe",     [VPC_VCP_LINES] = "vcp.lines",
};
_Static_assert(VPC_COUNT <= VPK_PC_MAX, "more counters than the kit's table holds");

/* the shared code's running totals (never reset; wrap-safe differences) */
extern unsigned g_psx_walk_packets[2]; /* gpu_walk.c: OT packets walked, the game's and its own walks' */
extern unsigned g_vcache_work[4];   /* vcache.c: projections, polygons, vertices, misses */

static unsigned s_gl0[VPC_COUNT], s_walk0, s_vc0[4], s_cost0[2][3];

/* one thread's cost counters (D134), records and lookups added: divisions,
 * probes, lines. The presenter's are another thread's plain adds, read as a
 * word each (as gpu_gl.c's); the game thread's are this thread's own. */
static void pull_cost(const unsigned *c, unsigned *last, int first) {
  static const int rec[3] = {VCC_REC_DIV, VCC_REC_PROBES, VCC_REC_LINES};
  static const int look[3] = {VCC_LOOK_DIV, VCC_LOOK_PROBES, VCC_LOOK_LINES};
  int i;
  for (i = 0; i < 3; i++) {
    unsigned v = __atomic_load_n(&c[rec[i]], __ATOMIC_RELAXED) +
                 __atomic_load_n(&c[look[i]], __ATOMIC_RELAXED);
    vpk_pc_add(g_vpc[first + i], v - last[i]);
    last[i] = v;
  }
}
static long long s_pres0;

void vita_pc_init(void) {
  VpkPerfcountConfig c = {300, "vpk-perfcount", vita_log};
  int i;
  vpk_pc_init(&c);
  for (i = 0; i < VPC_COUNT; i++)
    g_vpc[i] = vpk_pc_register(k_names[i]);
  vita_log("perfcount: a vpk-perfcount line every %u vblanks, %d counters (D131)", c.window,
           VPC_COUNT);
}

/* the pulled counters' increase since the last vblank */
static void pull(void) {
  unsigned w = __atomic_load_n(&g_psx_walk_packets[0], __ATOMIC_RELAXED) +
               __atomic_load_n(&g_psx_walk_packets[1], __ATOMIC_RELAXED);
  long long p = g_present_stats.interp_frames;
  int i;
  for (i = 0; i < VPC_OT_PACKETS; i++) { /* gpu_gl.c's: a word read, whatever thread adds */
    unsigned v = __atomic_load_n(&g_vpc_gl[i], __ATOMIC_RELAXED);
    vpk_pc_add(g_vpc[i], v - s_gl0[i]);
    s_gl0[i] = v;
  }
  vpk_pc_add(g_vpc[VPC_OT_PACKETS], w - s_walk0);
  s_walk0 = w;
  for (i = 0; i < 4; i++) {
    unsigned v = g_vcache_work[i];
    vpk_pc_add(g_vpc[VPC_VC_PROJ + i], v - s_vc0[i]);
    s_vc0[i] = v;
  }
  vpk_pc_add(g_vpc[VPC_PRES_FRAMES], (unsigned)(p - s_pres0));
  s_pres0 = p;
  pull_cost(g_vcache_cost, s_cost0[0], VPC_VC_DIV);
  pull_cost(g_vcache_pres_cost, s_cost0[1], VPC_VCP_DIV);
}

void vita_pc_vblank(int vblank) {
  pull();
  vpk_pc_frame((unsigned)vblank);
}

void vita_pc_flush(void) {
  pull();
  vpk_pc_flush();
}
