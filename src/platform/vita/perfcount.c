/* Work counters (issue #160, D131): vita-port-kit's vita/runtime/perfcount
 * (third_party/vita-port-kit, vendored). One "vpk-perfcount:" line in
 * spyro.log every 300 vblanks, each counter the window's sum, which the
 * kit's vita/tools/perf_report.py reads and compares between two runs.
 * Vita3K's timing is not evidence, but the work a frame does is the same
 * there and on the Vita (phase 8 of the port autopilot).
 *
 * Every counter is a running total kept where the work happens with a plain
 * add per event (or one per walk), and pulled once a vblank on the game
 * thread into the kit's counters (vpk_pc_add, ~22 atomic adds a vblank):
 * gpu_gl.c's GL calls and the game's draws there (vita_pc.h, g_vpc_gl), the
 * OT walk's packets (gpu_walk.c, one add a walk), the vertex cache's projections and lookups (vcache.c)
 * and the presenter's frames (present.c's g_present_stats). The shared code
 * needs nothing of the kit (the host links none), and no hot path has an
 * atomic (D131).
 * Keep the names stable: runs compare by name. */
#include "vita.h"
#include "vita_pc.h"
#include "../../game/present.h"

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
};

/* the shared code's running totals (never reset; wrap-safe differences) */
extern unsigned g_psx_walk_packets[2]; /* gpu_walk.c: OT packets walked, the game's and its own walks' */
extern unsigned g_vcache_work[4];   /* vcache.c: projections, polygons, vertices, misses */

static unsigned s_gl0[VPC_COUNT], s_walk0, s_vc0[4];
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
}

void vita_pc_vblank(int vblank) {
  pull();
  vpk_pc_frame((unsigned)vblank);
}

void vita_pc_flush(void) {
  pull();
  vpk_pc_flush();
}
