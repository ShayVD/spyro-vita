/* The presenter's LOD (stage 7d, decisions D123): lod.c compiled again on
 * the presenter's names (render_ctx_names.h), so the renderers' hooks
 * (pres_lod_*) keep their own state: the limiter, the pass-2 bits, the
 * statistics, the switches. The level's moby array is read through
 * g_pres_level_mobys, which the presenter points at its copy of it.
 * --render-ctx-check sets all of it to the game's before each call
 * (pres_lod_state_set) and compares after. */
#include "render_ctx_names.h"
#define g_lod g_pres_lod
#define g_LevelMobys g_pres_level_mobys
#define port_lod_draw_begin pres_lod_draw_begin
#define port_lod_draw_end pres_lod_draw_end
#define port_lod_reset pres_lod_reset
#define port_lod_report pres_lod_report
#define port_lod_state_size pres_lod_state_size
#define port_lod_state_get pres_lod_state_get
#define port_lod_state_set pres_lod_state_set
#include "lod.c"

Moby *g_pres_level_mobys;
