/* Closed-loop stepping for the headless linux32 host (--step DIR): the game
 * stops at a vblank, writes DIR/now.png, and waits for DIR/cmd, a line
 *   <buttons|none> <vblanks>     e.g. "up+cross 30"
 * holds those buttons for that many vblanks, then stops again. Every step is
 * appended to DIR/pad.txt in --pad syntax, so the session replays exactly as
 * a scripted run. Files, not FIFOs, because Docker Desktop's bind mounts
 * don't carry FIFOs. "quit" ends the run. tools/step.sh is the client. */
#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *s_dir;
static int s_until;
static uint16_t s_last = 0xFFFF;

void host_step_set_dir(const char *dir) { s_dir = dir; }

void host_step_vblank(int vblank) {
  char path[1024], cmd[1024], line[256], btn[200];
  int n;
  FILE *f;
  uint16_t mask;
  if (!s_dir || vblank < s_until)
    return;
  snprintf(path, sizeof path, "%s/now.png", s_dir);
  host_shot_display(path);
  snprintf(path, sizeof path, "%s/shot-%d.png", s_dir, vblank);
  host_shot_display(path);
  snprintf(path, sizeof path, "%s/ready", s_dir);
  if ((f = fopen(path, "w"))) {
    fprintf(f, "%d\n", vblank);
    fclose(f);
  }
  host_log_flush();
  snprintf(cmd, sizeof cmd, "%s/cmd", s_dir);
  for (;;) {
    struct timespec ts = {0, 20 * 1000000};
    f = fopen(cmd, "r");
    if (f) {
      int ok = fgets(line, sizeof line, f) != NULL;
      fclose(f);
      remove(cmd);
      if (!ok)
        continue;
      if (strncmp(line, "quit", 4) == 0) {
        host_log("step: quit at %d", vblank);
        host_log_flush();
        remove(path);
        exit(0);
      }
      if (sscanf(line, "%199s %d", btn, &n) == 2 && n > 0 &&
          host_pad_parse(btn, &mask) == 0)
        break;
      host_log("step: bad command '%s'", line);
      host_log_flush();
      continue;
    }
    nanosleep(&ts, NULL);
  }
  remove(path); /* not ready any more */
  host_pad_set_live(mask);
  if (mask != s_last) {
    snprintf(path, sizeof path, "%s/pad.txt", s_dir);
    if ((f = fopen(path, "a"))) {
      fprintf(f, "%d %s\n", vblank + 1, btn);
      fclose(f);
    }
    s_last = mask;
  }
  s_until = vblank + n;
}
