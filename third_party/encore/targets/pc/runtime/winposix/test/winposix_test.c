/* enc_winposix on Windows (run under Wine or on Windows): a file with CR LF bytes written and read back with open()
 * and fopen() without "b" keeps them; pread reads at offsets without moving the descriptor. The mutant
 * (ENC_WINPOSIX_MUTANT: the module not linked, text mode) must fail.
 *   winposix_test.exe     (targets/pc/test/desktop_runtime_tests.sh builds both) */
#include "enc_winposix.h"

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

#ifdef ENC_WINPOSIX_MUTANT
#include <unistd.h>
static ssize_t mut_pread(int fd, void *buf, size_t n, off_t off) { /* a seek and a read: moves the offset */
  lseek(fd, off, SEEK_SET);
  return read(fd, buf, (unsigned)n);
}
#define pread mut_pread
#endif

int main(void) {
  static const char data[] = "AB\r\nCD\r\nEF";
  char buf[32] = {0};
  const char *path = "winposix_test.bin";
  int fd, fails = 0;
  FILE *f = fopen(path, "w");
  fwrite(data, 1, sizeof data - 1, f);
  fclose(f);
  fd = open(path, O_RDONLY);
  if (read(fd, buf, 4) != 4 || memcmp(buf, "AB\r\n", 4)) fails++, printf("winposix_test: text mode changed the bytes\n");
  if (pread(fd, buf, 2, 4) != 2 || memcmp(buf, "CD", 2)) fails++, printf("winposix_test: pread at 4\n");
  if (read(fd, buf, 2) != 2 || memcmp(buf, "CD", 2)) fails++, printf("winposix_test: pread moved the offset\n");
  if (pread(fd, buf, 8, 8) != 2) fails++, printf("winposix_test: pread at the end\n");
  close(fd);
  remove(path);
  printf("winposix_test: %s\n", fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
