/* perfcount: two windows, a flush, bad names and ids, a full table. VPK_PERFCOUNT_MUTANT=1 (totals not reset
 * after a line) must fail. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vpk_perfcount.h"

static char logbuf[4096];
static void cap(const char *fmt, ...) {
    va_list ap;
    size_t n = strlen(logbuf);
    va_start(ap, fmt);
    vsnprintf(logbuf + n, sizeof logbuf - n, fmt, ap);
    va_end(ap);
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf("perfcount_test: FAIL %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

int main(void) {
    /* a window with no registered counters: nothing after the frame count (runs first, before any register) */
    VpkPerfcountConfig e = {2, "pc", cap};
    vpk_pc_init(&e);
    vpk_pc_frame(1);
    vpk_pc_frame(2);
    CHECK(strcmp(logbuf, "pc: w 1 2 2\n") == 0);
    logbuf[0] = 0;
    VpkPerfcountConfig c = {3, "pc", cap};
    vpk_pc_init(&c);
    int draws = vpk_pc_register("draws");
    int ups = vpk_pc_register("tex.uploads");
    CHECK(draws == 0 && ups == 1);
    CHECK(vpk_pc_register("draws") == 0);
    CHECK(vpk_pc_register("bad name") == -1);
    CHECK(vpk_pc_register("") == -1);
    CHECK(vpk_pc_register("abcdefghijklmnopqrstuvwx") == -1); /* 24 characters: refused */
    CHECK(vpk_pc_register("a_name_that_is_much_too_long") == -1);
    vpk_pc_add(-1, 5);
    vpk_pc_add(99, 5);
    for (unsigned f = 1; f <= 3; f++) { vpk_pc_add(draws, 2); vpk_pc_add(ups, 1); vpk_pc_frame(f); }
    CHECK(strcmp(logbuf, "pc: w 1 3 3 draws=6 tex.uploads=3\n") == 0);
    logbuf[0] = 0;
    for (unsigned f = 4; f <= 5; f++) { vpk_pc_add(draws, 2); vpk_pc_frame(f); }
    CHECK(logbuf[0] == 0);
    vpk_pc_flush();
    CHECK(strcmp(logbuf, "pc: w 4 5 2 draws=4 tex.uploads=0\n") == 0);
    logbuf[0] = 0;
    vpk_pc_flush();
    CHECK(logbuf[0] == 0);
    char name[16];
    int last = 0;
    for (int i = 2; i < VPK_PC_MAX - 1; i++) { snprintf(name, sizeof name, "c%d", i); last = vpk_pc_register(name); }
    CHECK(last == VPK_PC_MAX - 2);
    last = vpk_pc_register("abcdefghijklmnopqrstuvw"); /* exactly 23 characters: accepted (the last slot) */
    CHECK(last == VPK_PC_MAX - 1);
    CHECK(vpk_pc_register("one_more") == -1);
    if (fails) { printf("perfcount_test: %d failed\n", fails); return 1; }
    printf("perfcount_test: ok\n");
    return 0;
}
