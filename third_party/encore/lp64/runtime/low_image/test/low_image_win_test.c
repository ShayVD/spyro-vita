/* lp64_low_image_win on 64-bit Windows, linked with the flags in lp64_low_image_win.c: the image at 0x400000, and
 * the probe's heap block, fresh mapping and stack all below 2 GB, a thread's too. Run on Windows itself (CI's Windows
 * runner): Wine doesn't apply the 2 GB limit, so it proves nothing there.
 *   low_image_win_test.exe      (targets/pc/test/desktop_runtime_tests.sh builds it) */
#include "lp64_low_image.h"

#include <stdint.h>
#include <stdio.h>
#include <windows.h>

static unsigned char s_arena[1 << 20];

static DWORD WINAPI on_thread(void *arg) {
  *(int *)arg = lp64_win_low_probe(NULL, 0);
  return 0;
}

int main(void) {
  char line[256];
  int ok = lp64_win_low_probe(line, sizeof line), th = 0;
  HANDLE h = CreateThread(NULL, 1 << 20, on_thread, &th, 0, NULL);
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
  printf("low_image_win_test: %s; a thread's: %s\n", line, th ? "yes" : "no");
  ok = ok && th && (uintptr_t)GetModuleHandleA(NULL) == 0x400000u && lp64_is_low(s_arena, sizeof s_arena);
  printf("low_image_win_test: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
