/* What src/platform/linux32/gpu_soft.c and png.c (the software rasterizer
 * and the display-rect reader, shared with the Vita for now, decisions D26)
 * expect from their host: its options, all off here. */
#include "host.h"

HostOpts g_host_opts;
