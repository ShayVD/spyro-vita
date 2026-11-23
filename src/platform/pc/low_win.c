/* The low image on Windows (docs/pc-build.md, D240). The executable has a
 * fixed base of 0x400000 (no ASLR) and is not large-address-aware
 * (src/platform/pc/CMakeLists.txt), so Windows keeps the whole process below
 * 2 GB: the image with the arena (.bss), every heap, thread stack and DLL.
 * The game's addresses fit its 32-bit pointer fields (P32) and its (int)
 * casts, and a host pointer cut to 32 bits by mistake is still the same
 * address: no trap is needed, and none is possible (nothing is above 4 GB).
 * The price is a 2 GB address space for everything, drivers included, which
 * the game (2 MB of PS1 RAM, a 1 MB VRAM, a few MB of frames) never nears. */
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <windows.h>

extern const void *psx_low_arena_end(void);
extern IMAGE_DOS_HEADER __ImageBase;

static char s_report[256];

int pc_low_reserved_pages(void) { return 0; }

/* Every file binary: the common code opens the disc image, saves and logs
 * with open() and fopen() as on POSIX, where there is no text mode; on
 * Windows the CRT's default would turn CR LF into LF in the disc's bytes. */
__attribute__((constructor)) static void binary_files(void) { _set_fmode(_O_BINARY); }

/* ELF's linker symbols, which savestate.c stamps a save state's layout
 * with (the build's own identity): a PE has none, so marks in the image */
char etext[1], _edata[1], _end[1];

/* POSIX's pread (cdnative.c's worker reads the disc by offset): a
 * positional ReadFile, which leaves the descriptor's own offset alone and
 * may run on several threads at once, as pread may */
ssize_t pread(int fd, void *buf, size_t n, off_t off);
ssize_t pread(int fd, void *buf, size_t n, off_t off) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  OVERLAPPED ov = {0};
  DWORD got = 0;
  if (h == INVALID_HANDLE_VALUE)
    return -1;
  ov.Offset = (DWORD)((unsigned long long)off & 0xFFFFFFFFu);
  ov.OffsetHigh = (DWORD)((unsigned long long)off >> 32);
  if (!ReadFile(h, buf, n > 0x7FFFFFFF ? 0x7FFFFFFF : (DWORD)n, &got, &ov))
    return GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
  return (ssize_t)got;
}

const char *pc_low_report(void) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  /* Windows: 0x7FFEFFFF for a program that isn't large-address-aware (Wine
   * doesn't apply that limit to 64-bit programs) */
  snprintf(s_report, sizeof s_report, "image at %p, arena end %p, addresses up to %p",
           (void *)&__ImageBase, psx_low_arena_end(), si.lpMaximumApplicationAddress);
  return s_report;
}
#endif
