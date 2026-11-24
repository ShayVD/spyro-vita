/* enc_config: comments, lines without '=', trimming, '=' in values, CRLF, a missing file.
 *   cc -std=c99 -Wall -I../include ../src/enc_config.c config_test.c -o config_test && ./config_test */
#include <stdio.h>
#include <string.h>
#include "encore/config.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char got[16][2][64];
static int n;
static void take(char *k, char *v, void *user) {
    (void)user;
    if (n < 16) { snprintf(got[n][0], 64, "%s", k); snprintf(got[n][1], 64, "%s", v); }
    n++;
}

int main(void) {
    const char *path = "config_test.txt";
    FILE *f = fopen(path, "w");
    fputs("# a comment=1\nwide=1\nno equals here\nvolume=40  \r\niso=ux0:data/x/a=b.iso\nempty=\n frames=5\n", f);
    fclose(f);
    CHECK(enc_config_read(path, take, 0) == 1);
    CHECK(n == 5);
    CHECK(!strcmp(got[0][0], "wide") && !strcmp(got[0][1], "1"));
    CHECK(!strcmp(got[1][0], "volume") && !strcmp(got[1][1], "40"));
    CHECK(!strcmp(got[2][0], "iso") && !strcmp(got[2][1], "ux0:data/x/a=b.iso"));
    CHECK(!strcmp(got[3][0], "empty") && !strcmp(got[3][1], ""));
    CHECK(!strcmp(got[4][0], " frames") && !strcmp(got[4][1], "5")); /* keys are not trimmed */
    remove(path);
    n = 0;
    CHECK(enc_config_read(path, take, 0) == 0 && n == 0);
    printf(fails ? "config_test: %d failed\n" : "config_test: ok\n", fails);
    return fails != 0;
}
