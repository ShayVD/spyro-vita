/* PsyQ libmcrd over a directory per card (docs/porting-plan.md §6 item 4):
 * <save root>/card0 is port A, card1 port B; each memory-card file is a host
 * file of blocks * 8 KB. Port A always has a formatted card (its directory
 * is created on first use); port B has one only if card1/ exists. Async
 * calls (Exist, Accept, ReadFile, WriteFile) complete at the next
 * MemCardSync, which reports them once and then returns -1 (idle).
 * Replaces PsyCross's LIBMCRD.C, which has no files behind it. */
#include "psx_hooks.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define McFuncExist 1
#define McFuncAccept 2
#define McFuncReadFile 3
#define McFuncWriteFile 4
#define McErrNone 0
#define McErrCardNotExist 1
#define McErrFileNotExist 5
#define McErrAlreadyExist 6
#define McErrBlockFull 7

#define CARD_BLOCKS 15
#define BLOCK_SIZE 8192

typedef void (*MemCB)(int, int);

static char s_root[512] = "save";
static int s_pending_cmd, s_pending_rslt, s_have_pending;

void psx_mcrd_set_root(const char *dir) { snprintf(s_root, sizeof s_root, "%s", dir); }

static int card_index(int chan) { return (chan >> 4) & 1; }

static void card_dir(int chan, char *out, size_t n) {
  snprintf(out, n, "%s/card%d", s_root, card_index(chan));
}

static int card_present(int chan) {
  char d[600];
  struct stat st;
  if (card_index(chan) == 0)
    return 1;
  card_dir(chan, d, sizeof d);
  return stat(d, &st) == 0 && S_ISDIR(st.st_mode);
}

static int make_card_dir(int chan) {
  char d[600];
  mkdir(s_root, 0777);
  card_dir(chan, d, sizeof d);
  return mkdir(d, 0777) == 0 || errno == EEXIST ? 0 : -1;
}

static void file_path(int chan, const char *name, char *out, size_t n) {
  char d[600];
  card_dir(chan, d, sizeof d);
  snprintf(out, n, "%s/%s", d, name);
}

static void post(int cmd, int rslt) {
  s_pending_cmd = cmd;
  s_pending_rslt = rslt;
  s_have_pending = 1;
}

void MemCardInit(int val) { (void)val; }
void MemCardEnd(void) {}
void MemCardStart(void) {}
void MemCardStop(void) {}
MemCB MemCardCallback(MemCB func) { (void)func; return 0; }

int MemCardExist(int chan) {
  post(McFuncExist, card_present(chan) ? McErrNone : McErrCardNotExist);
  return 1;
}

int MemCardAccept(int chan) {
  post(McFuncAccept, card_present(chan) ? McErrNone : McErrCardNotExist);
  return 1;
}

int MemCardSync(int mode, int *cmds, int *rslt) {
  (void)mode; /* everything is already done; Sync and Async behave alike */
  if (!s_have_pending)
    return -1;
  if (cmds)
    *cmds = s_pending_cmd;
  if (rslt)
    *rslt = s_pending_rslt;
  s_have_pending = 0;
  return 1;
}

int MemCardReadFile(int chan, char *file, unsigned int *adrs, int ofs, int bytes) {
  char p[700];
  FILE *f;
  int r = McErrFileNotExist;
  if (!card_present(chan)) {
    post(McFuncReadFile, McErrCardNotExist);
    return 1;
  }
  file_path(chan, file, p, sizeof p);
  f = fopen(p, "rb");
  if (f) {
    if (fseek(f, ofs, SEEK_SET) == 0 && fread(adrs, 1, (size_t)bytes, f) == (size_t)bytes)
      r = McErrNone;
    fclose(f);
  }
  post(McFuncReadFile, r);
  return 1;
}

int MemCardWriteFile(int chan, char *file, unsigned int *adrs, int ofs, int bytes) {
  char p[700];
  FILE *f;
  int r = McErrFileNotExist;
  if (!card_present(chan)) {
    post(McFuncWriteFile, McErrCardNotExist);
    return 1;
  }
  file_path(chan, file, p, sizeof p);
  f = fopen(p, "r+b");
  if (f) {
    if (fseek(f, ofs, SEEK_SET) == 0 && fwrite(adrs, 1, (size_t)bytes, f) == (size_t)bytes &&
        fflush(f) == 0)
      r = McErrNone;
    fclose(f);
  }
  post(McFuncWriteFile, r);
  return 1;
}

int MemCardCreateFile(int chan, char *file, int blocks) {
  char p[700];
  static const char zero[BLOCK_SIZE];
  FILE *f;
  int i;
  if (!card_present(chan))
    return McErrCardNotExist;
  if (blocks < 1 || blocks > CARD_BLOCKS)
    return McErrBlockFull;
  if (make_card_dir(chan) != 0)
    return McErrBlockFull;
  file_path(chan, file, p, sizeof p);
  if ((f = fopen(p, "rb")) != NULL) {
    fclose(f);
    return McErrAlreadyExist;
  }
  if (!(f = fopen(p, "wb")))
    return McErrBlockFull;
  for (i = 0; i < blocks; i++)
    fwrite(zero, 1, sizeof zero, f);
  fclose(f);
  return McErrNone;
}

int MemCardDeleteFile(int chan, char *file) {
  char p[700];
  file_path(chan, file, p, sizeof p);
  return remove(p) == 0 ? McErrNone : McErrFileNotExist;
}

int MemCardFormat(int chan) {
  return make_card_dir(chan) == 0 ? McErrNone : McErrCardNotExist;
}

int MemCardUnformat(int chan) { (void)chan; return McErrNone; }
