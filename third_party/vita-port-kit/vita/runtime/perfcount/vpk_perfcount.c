/* vpk_perfcount: see vpk_perfcount.h. */
#include "vpk_perfcount.h"

#include <stdio.h>
#include <string.h>

static VpkPerfcountConfig cfg = {300, "vpk-perfcount", 0};
static char names[VPK_PC_MAX][VPK_PC_NAME_MAX + 1];
static int count;
static unsigned totals[VPK_PC_MAX];
static unsigned first, last, frames;

void vpk_pc_init(const VpkPerfcountConfig *config) {
    if (!config) return;
    cfg.window = config->window ? config->window : 300;
    cfg.prefix = config->prefix ? config->prefix : "vpk-perfcount";
    cfg.log = config->log;
}

static int good_name(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > VPK_PC_NAME_MAX) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' ||
              *s == '.'))
            return 0;
    return 1;
}

int vpk_pc_register(const char *name) {
    if (!name || !good_name(name)) return -1;
    for (int i = 0; i < count; i++)
        if (strcmp(names[i], name) == 0) return i;
    if (count == VPK_PC_MAX) return -1;
    strcpy(names[count], name);
    return count++;
}

void vpk_pc_add(int id, unsigned n) {
    if (id < 0 || id >= count) return;
    __atomic_fetch_add(&totals[id], n, __ATOMIC_RELAXED);
}

static void emit(void) {
    char line[VPK_PC_MAX * (VPK_PC_NAME_MAX + 13) + 64];
    int n = snprintf(line, sizeof line, "%s: w %u %u %u", cfg.prefix, first, last, frames);
    /* every counter is reset even when the text no longer fits: the line may be cut, the totals must not leak */
    for (int i = 0; i < count; i++) {
#if defined(VPK_PERFCOUNT_MUTANT) && VPK_PERFCOUNT_MUTANT == 1
        unsigned v = __atomic_load_n(&totals[i], __ATOMIC_RELAXED);
#else
        unsigned v = __atomic_exchange_n(&totals[i], 0u, __ATOMIC_RELAXED);
#endif
        if (n > 0 && (size_t)n < sizeof line) n += snprintf(line + n, sizeof line - (size_t)n, " %s=%u", names[i], v);
    }
    if (cfg.log) cfg.log("%s\n", line);
    else printf("%s\n", line);
    frames = 0;
}

void vpk_pc_frame(unsigned frame) {
    if (frames == 0) first = frame;
    last = frame;
    if (++frames >= cfg.window) emit();
}

void vpk_pc_flush(void) {
    if (frames) emit();
}
