/* Widescreen and 60 fps on the Vita (docs/presentation.md, decisions D30):
 * config.txt keys into src/game/present.c's switches, the platform clock for
 * interp_budget, and a log line with every stats report. The pictures are
 * shown by video.c (gpu=0) and gpu_gl.c (gpu=1, which also captures them on
 * the GPU). */
#include "vita.h"
#include "../../game/present.h"
#include "host.h" /* linux32's, for png.c's shots */

#include <psp2/kernel/processmgr.h>

static long long vita_clock_us(void) { return (long long)sceKernelGetProcessTimeWide(); }

/* Also from the menu, between ticks or inside the game's VSync wait:
 * present.c takes new switches at its next tick. */
void vita_present_apply(void) {
  g_present.widescreen = g_vita_opts.widescreen != 0;
  g_present.interp = g_vita_opts.interp != 0;
  g_present.interp_mobys = g_vita_opts.interp == 1 || g_vita_opts.interp == 2;
  g_present.interp_anim = g_vita_opts.interp == 1;
  g_present.interp_budget_us = g_vita_opts.interp_budget * 1000;
}

void vita_present_init(void) {
  vita_present_apply();
  g_present.stats = g_vita_opts.stats ? 1 : 0;
  g_present_clock_us = vita_clock_us;
  if (g_vita_gpu)
    g_present_capture = vita_gpu_present_capture();
  else
    g_host_opts.out960 = 2; /* shots: the presentation frame on screen, 512x240 */
  vita_log("present: widescreen %d, interp %d (budget %d ms), %s capture; "
           "hold SELECT and press START for the options menu",
           g_vita_opts.widescreen, g_vita_opts.interp, g_vita_opts.interp_budget,
           g_present_capture ? "GPU" : "CPU");
}

/* One line per stats report, totals since the last one. */
void vita_present_report(int vblank) {
  static PresentStats last;
  const PresentStats *s = &g_present_stats;
  long long ticks = s->ticks - last.ticks;
  if (!port_present_active() || ticks <= 0)
    return;
  vita_log("present %d: %lld ticks; per tick us: logic draw %lld, passes %lld, snapshot %lld, "
           "blend %lld; %lld in-between frames, %lld skipped for the budget, %lld otherwise",
           vblank, ticks, (s->us_logic - last.us_logic - (s->us_vsync - last.us_vsync)) / ticks,
           (s->us_pass - last.us_pass) / ticks, (s->us_snapshot - last.us_snapshot) / ticks,
           (s->us_blend - last.us_blend) / ticks, s->interp_frames - last.interp_frames,
           s->budget_skips - last.budget_skips, s->skipped_interp - last.skipped_interp);
  last = *s;
}
