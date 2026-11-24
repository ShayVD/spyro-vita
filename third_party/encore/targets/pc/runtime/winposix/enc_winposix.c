/* enc_winposix: what a POSIX-written port's own code (a disc reader, a save writer) needs on Windows (mingw) that
 * mingw's CRT lacks or does differently. Link it into the Windows build only; it compiles to nothing elsewhere.
 *  - every file binary: a constructor sets _fmode to _O_BINARY, so open() and fopen() without "b" never turn CR LF
 *    into LF in a disc image's bytes (POSIX has no text mode, and ports open files as on POSIX);
 *  - pread: a positional ReadFile, the descriptor's offset put back after it (Windows moves it even for a positional
 *    read; caught by this module's test under Wine).
 * mkdir takes no mode on Windows: `#define mkdir(path, mode) _mkdir(path)` after <direct.h> at the call sites
 * (a macro here would break mingw's own declaration). From spyro-vita src/platform/pc/low_win.c (MIT; its D241). */
#if defined(_WIN32)
#include "enc_winposix.h"

#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <windows.h>

__attribute__((constructor)) static void enc_winposix_binary(void) { _set_fmode(_O_BINARY); }

ssize_t pread(int fd, void *buf, size_t n, off_t off) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  OVERLAPPED ov;
  DWORD got = 0;
  long long pos;
  BOOL ok;
  if (h == INVALID_HANDLE_VALUE) return -1;
  /* a positional ReadFile on a handle opened without FILE_FLAG_OVERLAPPED (every CRT descriptor's) still moves the
   * file pointer, which POSIX's pread must not: put it back. Not atomic against a read() on the same descriptor
   * from another thread at the same moment; preads alone may overlap freely. */
  pos = _lseeki64(fd, 0, SEEK_CUR);
  ZeroMemory(&ov, sizeof ov);
  ov.Offset = (DWORD)((unsigned long long)off & 0xFFFFFFFFu);
  ov.OffsetHigh = (DWORD)((unsigned long long)off >> 32);
  ok = ReadFile(h, buf, n > 0x7FFFFFFF ? 0x7FFFFFFF : (DWORD)n, &got, &ov);
  if (pos >= 0) _lseeki64(fd, pos, SEEK_SET);
  if (!ok) return GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
  return (ssize_t)got;
}
#else
typedef int enc_winposix_empty; /* ISO C wants something in a unit */
#endif
