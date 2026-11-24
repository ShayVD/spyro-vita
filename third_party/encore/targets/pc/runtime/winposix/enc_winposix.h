/* enc_winposix (enc_winposix.c): POSIX calls mingw's CRT lacks, for a port's own code on Windows. */
#ifndef ENC_WINPOSIX_H
#define ENC_WINPOSIX_H
#if defined(_WIN32)
#include <sys/types.h>
/* POSIX pread: n bytes at off, the descriptor's offset unchanged; bytes read, 0 at the end, -1 on an error */
ssize_t pread(int fd, void *buf, size_t n, off_t off);
#endif
#endif
