/* The disc read straight from a raw image in the data folder (psx_cdimage.h,
 * decisions D60). Finding the image, its sector size and first LBA, the
 * ISO9660 walk and the XA test are tools/extract_disc.py's (D58), so the
 * file table is the manifest.json it would write. Reads go through two
 * windows of 32 raw sectors, one fread each, so a level load or the XA
 * stream costs one file-system call per 32 sectors. The reading, sniffing,
 * directory walk and SHA-1 are vita-port-kit's discimage (D108); what stays
 * here is the search of the data folder, the cue sheet, the messages and the
 * disc check. */
#include "psx_cdimage.h"
#include "psx_region.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_NAMES 256

#define XA_FORM2 0x1000
#define XA_INTERLEAVED 0x2000
#define XA_CDDA 0x4000

/* The executables and their SHA-1s are the region table's (psx_region.h,
 * D132; extract_disc.py's REGIONS). */
#define WAD_NAME "WAD.WAD"

static VpkDisc s_disc; /* the image, read through two 32-sector windows */
static struct {
  char path[512];
  char name[128]; /* for messages: the file name alone */
  long size;
} s_img;

static char s_marker[512];
static void (*s_progress)(int percent);

void psx_cd_set_verify(const char *marker_path, void (*progress)(int percent)) {
  snprintf(s_marker, sizeof s_marker, "%s", marker_path ? marker_path : "");
  s_progress = progress;
}

const char *psx_cdimage_path(void) { return vpk_disc_is_open(&s_disc) ? s_img.path : NULL; }

void psx_cdimage_layout(int *sector, int *base) {
  *sector = s_disc.l.sector;
  *base = s_disc.l.base;
}

/* ---- messages ---- */

static int fail(PsxCdError *err, int code, const char *title, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static int fail(PsxCdError *err, int code, const char *title, const char *fmt, ...) {
  va_list ap;
  err->code = code;
  snprintf(err->title, sizeof err->title, "%s", title);
  va_start(ap, fmt);
  vsnprintf(err->text, sizeof err->text, fmt, ap);
  va_end(ap);
  return -1;
}

/* ---- finding the image ---- */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int name_cmp(const void *a, const void *b) {
  const char *x = *(const char *const *)a, *y = *(const char *const *)b;
  for (; *x && lower(*x) == lower(*y); x++, y++)
    ;
  return lower(*x) - lower(*y);
}

static int has_ext(const char *name, const char *ext) {
  size_t n = strlen(name), e = strlen(ext);
  return n > e && name_cmp(&(const char *){name + n - e}, &ext) == 0;
}

enum { K_RAW, K_ISO, K_CHD, K_PBP, K_ECM, K_UNKNOWN, K_UNREADABLE };

/* What the file is; for K_RAW also its sector size and first LBA (the first
 * sector with a sync pattern whose next sector has one too, as D58). */
static int sniff(const char *path, VpkDiscLayout *l) {
  switch (vpk_disc_sniff(path, l)) {
  case VPK_DISC_RAW:
    return K_RAW;
  case VPK_DISC_ISO:
    return K_ISO;
  case VPK_DISC_CHD:
    return K_CHD;
  case VPK_DISC_PBP:
    return K_PBP;
  case VPK_DISC_ECM:
    return K_ECM;
  case VPK_DISC_UNREADABLE:
    return K_UNREADABLE;
  }
  return K_UNKNOWN;
}

/* A word at the start of p, ignoring case. */
static int starts(const char *p, const char *word) {
  for (; *word; p++, word++)
    if (lower(*p) != lower(*word))
      return 0;
  return 1;
}

/* The image a .cue's first FILE line names, next to it; its first TRACK must
 * be 1, MODE2/2352 (or 2448), as extract_disc.py wants. A name that differs
 * only in case is taken too (the Vita's memory card ignores case, Linux
 * doesn't), and a folder in it is dropped. */
static int resolve_cue(const char *dir, const char *cue, char **names, int nnames, char *out,
                       size_t cap, PsxCdError *err) {
  char path[512], text[4096], want[256] = "";
  const char *p, *q, *base;
  int n, i, track_ok = -1;
  FILE *f;
  snprintf(path, sizeof path, "%s/%s", dir, cue);
  if (!(f = fopen(path, "rb")))
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE", "%s can't be opened.", cue);
  n = (int)fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[n < 0 ? 0 : n] = 0;
  for (p = text; *p; p = *q ? q + 1 : q) {
    q = p + strcspn(p, "\r\n");
    while (*p == ' ' || *p == '\t')
      p++;
    if (!want[0] && starts(p, "FILE") && (p[4] == ' ' || p[4] == '\t')) {
      const char *s = p + 4, *e;
      while (*s == ' ' || *s == '\t')
        s++;
      if (*s == '"')
        e = memchr(++s, '"', (size_t)(q - s));
      else
        for (e = s; e < q && *e != ' ' && *e != '\t'; e++)
          ;
      if (e && e > s && (size_t)(e - s) < sizeof want) {
        memcpy(want, s, (size_t)(e - s));
        want[e - s] = 0;
      }
    } else if (track_ok < 0 && starts(p, "TRACK")) {
      char *e;
      long t = strtol(p + 5, &e, 10);
      while (*e == ' ' || *e == '\t')
        e++;
      track_ok = t == 1 && (starts(e, "MODE2/2352") || starts(e, "MODE2/2448"));
    }
  }
  if (!want[0])
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE CUE FILE",
                "%s has no FILE line this port can read. Make the .cue again, or copy the .bin "
                "alone.",
                cue);
  if (track_ok != 1)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE CUE FILE",
                "%s: track 1 isn't MODE2/2352, as a PlayStation data track should be.", cue);
  for (p = base = want; *p; p++)
    if (*p == '/' || *p == '\\')
      base = p + 1;
  for (i = 0; i < nnames; i++)
    if (name_cmp(&base, &names[i]) == 0) {
      snprintf(out, cap, "%s/%s", dir, names[i]);
      return 0;
    }
  return fail(err, PSX_CD_ERR_BAD_IMAGE, "THE .BIN IS MISSING",
              "%s names %s, which isn't in %s/. Copy both files.", cue, base, dir);
}

static int open_image(const char *path, const char *name, const VpkDiscLayout *l,
                      PsxCdError *err) {
  int r;
  s_disc.now = psx_cd_now;
  s_disc.io = psx_cd_io_add;
  r = vpk_disc_open(&s_disc, path, l, 1);
  if (r == -1)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE", "%s can't be opened.", name);
  if (r != 0)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE",
                "%s can't be read (or is over 2 GB).", name);
  memset(&s_img, 0, sizeof s_img);
  snprintf(s_img.path, sizeof s_img.path, "%s", path);
  snprintf(s_img.name, sizeof s_img.name, "%s", name);
  s_img.size = (long)s_disc.size;
  return 0;
}

static void say(const char *what) { fprintf(stderr, "libcd: %s\n", what); }

static int not_found(PsxCdError *err, const char *dir) {
  return fail(err, PSX_CD_ERR_NOT_FOUND, "NO DISC IMAGE FOUND",
              "Copy your Spyro the Dragon (USA) disc image to %s/: the .bin and .cue of a raw "
              "dump (a CloneCD .img works too). See the README.",
              dir);
}

/* The first usable image in dir: .cue files first, then .bin/.img (and any
 * other candidate whose data is a raw image), each in name order ignoring
 * case. With none, the first candidate's reason. */
static int find_image(const char *dir, PsxCdError *err) {
  static const char *const exts[] = {".cue", ".bin", ".img", ".iso", ".chd", ".pbp", ".ecm"};
  char *names[MAX_NAMES], path[512], msg[700];
  int nnames = 0, i, pass, found = -1;
  VpkDiscLayout lay = {0, 0, 0};
  PsxCdError first = {0};
  DIR *d = opendir(dir);
  struct dirent *e;
  if (!d)
    return not_found(err, dir);
  while ((e = readdir(d)) && nnames < MAX_NAMES)
    for (i = 0; i < (int)(sizeof exts / sizeof *exts); i++)
      if (has_ext(e->d_name, exts[i])) {
        names[nnames++] = strdup(e->d_name);
        break;
      }
  closedir(d);
  qsort(names, (size_t)nnames, sizeof *names, name_cmp);

  for (pass = 0; pass < 2 && found < 0; pass++)
    for (i = 0; i < nnames && found < 0; i++) {
      const char *name = names[i];
      PsxCdError why = {0};
      int kind;
      if ((pass == 0) != has_ext(name, ".cue"))
        continue;
      if (pass == 0) {
        if (resolve_cue(dir, name, names, nnames, path, sizeof path, &why) == 0) {
          kind = sniff(path, &lay);
          if (kind == K_RAW) {
            const char *bin = strrchr(path, '/') + 1;
            snprintf(msg, sizeof msg, "disc image %s (from %s), %d-byte sectors, first LBA %d",
                     path, name, lay.sector, lay.base);
            say(msg);
            if (open_image(path, bin, &lay, err) != 0)
              goto out;
            found = i;
            continue;
          }
          fail(&why, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE",
               "%s names %s, which isn't a raw CD image. Dump the disc as .bin + .cue "
               "(MODE2/2352).",
               name, strrchr(path, '/') + 1);
        }
      } else {
        snprintf(path, sizeof path, "%s/%s", dir, name);
        kind = sniff(path, &lay);
        switch (kind) {
        case K_RAW:
          snprintf(msg, sizeof msg, "disc image %s, %d-byte sectors, first LBA %d", path, lay.sector,
                   lay.base);
          say(msg);
          if (open_image(path, name, &lay, err) != 0)
            goto out;
          found = i;
          continue;
        case K_ISO:
          fail(&why, PSX_CD_ERR_ISO, "AN ISO HAS NO MUSIC",
               "%s is a 2048-byte .iso, which has lost the game's music. Dump the disc again as "
               ".bin + .cue (raw, MODE2/2352) and copy both to %s/.",
               name, dir);
          break;
        case K_CHD:
          fail(&why, PSX_CD_ERR_CONVERT, "CONVERT THE CHD FIRST",
               "%s is a CHD. Convert it on a PC with MAME's chdman:\n"
               "chdman extractcd -i game.chd -o game.cue\n"
               "then copy the .bin and .cue to %s/.",
               name, dir);
          break;
        case K_PBP:
          fail(&why, PSX_CD_ERR_CONVERT, "A PBP CAN'T BE USED",
               "%s is a PSP EBOOT.PBP, which has lost the game's music. Dump your disc as "
               ".bin + .cue (MODE2/2352) and copy both to %s/.",
               name, dir);
          break;
        case K_ECM:
          fail(&why, PSX_CD_ERR_CONVERT, "UNPACK THE ECM FIRST",
               "%s is an ECM file. Unpack it on a PC with unecm (it gives the .bin back), then "
               "copy the .bin and .cue to %s/.",
               name, dir);
          break;
        case K_UNREADABLE:
          fail(&why, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE", "%s can't be opened.", name);
          break;
        default:
          fail(&why, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE",
               "%s isn't a raw CD image. Dump the disc as .bin + .cue (MODE2/2352).", name);
          break;
        }
      }
      snprintf(msg, sizeof msg, "%s: %s", name, why.title);
      say(msg);
      if (!first.code)
        first = why;
    }
  if (found < 0 && first.code)
    *err = first;
  else if (found < 0)
    not_found(err, dir);
out:
  for (i = 0; i < nnames; i++)
    free(names[i]);
  return found >= 0 ? 0 : -1;
}

/* ---- reading ---- */

int psx_cdimage_read(int lba, int off, void *buf, int n) {
  return vpk_disc_read_raw(&s_disc, lba, off, buf, n);
}

void psx_cdimage_close(void) {
  vpk_disc_close(&s_disc);
  memset(&s_img, 0, sizeof s_img);
}

static int read_form1(int lba, int count, unsigned char *out) {
  return vpk_disc_read(&s_disc, lba, count, out);
}

/* ---- the ISO9660 directory, as extract_disc.py walks it ---- */

typedef struct {
  PsxCdImageFile *v;
  int n, cap, bad_lba;
} Table;

static int add(Table *t, const PsxCdImageFile *f) {
  if (t->n == t->cap) {
    PsxCdImageFile *v = realloc(t->v, sizeof *v * (size_t)(t->cap = t->cap ? t->cap * 2 : 32));
    if (!v)
      return -1;
    t->v = v;
  }
  t->v[t->n++] = *f;
  return 0;
}

/* One file of the kit's walk (vpk_disc_walk: depth first, directory order). */
static int add_file(void *user, const VpkDiscEntry *e) {
  Table *t = user;
  PsxCdImageFile f;
  if (e->xa_attr & XA_CDDA)
    return 0; /* none on this disc; the extractor refuses them */
  memset(&f, 0, sizeof f);
  snprintf(f.path, sizeof f.path, "%s", e->path);
  f.lba = e->lba;
  f.size = e->size;
  f.xa = (e->xa_attr & (XA_FORM2 | XA_INTERLEAVED)) != 0;
  if (!f.xa && f.size > 0) { /* the first sector's submode says Form 2 */
    unsigned char sub[8];
    if (psx_cdimage_read(f.lba, 16, sub, 8) == 8 && (sub[2] & 0x20))
      f.xa = 1;
  }
  return add(t, &f);
}

static int by_lba(const void *a, const void *b) {
  const PsxCdImageFile *x = a, *y = b;
  return (x->lba > y->lba) - (x->lba < y->lba);
}

/* ---- which release is it? (D132) ---- */

static const PsxCdImageFile *lookup(const Table *t, const char *path) {
  for (int i = 0; i < t->n; i++)
    if (name_cmp(&(const char *){t->v[i].path}, &path) == 0)
      return &t->v[i];
  return NULL;
}

static int hash_file(const PsxCdImageFile *f, char hex[41], int progress) {
  static unsigned char sec[2048];
  PsxSha1 s;
  int nsect = (f->size + 2047) / 2048, last = -1;
  psx_sha1_init(&s);
  for (int i = 0; i < nsect; i++) {
    int n = f->size - i * 2048 < 2048 ? f->size - i * 2048 : 2048;
    if (psx_cdimage_read(f->lba + i, 24, sec, 2048) != 2048)
      return -1;
    psx_sha1_update(&s, sec, (unsigned)n);
    if (progress && s_progress && i * 100 / nsect != last)
      s_progress(last = i * 100 / nsect);
  }
  psx_sha1_hex(&s, hex);
  return 0;
}

/* SYSTEM.CNF's BOOT file ("SCES_014.38"), or "" without one. */
static void boot_exe(const Table *t, char *exe, size_t cap) {
  const PsxCdImageFile *cnf = lookup(t, "SYSTEM.CNF");
  unsigned char buf[2048];
  exe[0] = 0;
  if (!cnf || cnf->size <= 0 || read_form1(cnf->lba, 1, buf) != 0)
    return;
  psx_region_boot_exe((const char *)buf, cnf->size < 2048 ? (size_t)cnf->size : 2048, exe, cap);
}

static int verified_before(const char *marker, char *key, size_t cap) {
  struct stat st;
  char line[700] = "";
  FILE *f;
  key[0] = 0;
  if (stat(s_img.path, &st) != 0)
    return 0;
  snprintf(key, cap, "%ld %lld %s", s_img.size, (long long)st.st_mtime, s_img.path);
  if (!(f = fopen(marker, "rb")))
    return 0;
  if (!fgets(line, sizeof line, f))
    line[0] = 0;
  fclose(f);
  line[strcspn(line, "\r\n")] = 0;
  return strcmp(line, key) == 0;
}

int psx_cd_check_release(const PsxRegion *r, const char *exe_sha1, PsxCdError *err) {
  if (strcmp(exe_sha1, r->exe_sha1) != 0) {
    if (r->id == PSX_REGION_US)
      return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG VERSION OF THE GAME",
                  "%s on this disc isn't the one Spyro Vita needs (SHA-1 %.8s, needs %.8s): "
                  "another release of the game (Greatest Hits, a demo) or a bad dump.",
                  r->exe, exe_sha1, r->exe_sha1);
    return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG VERSION OF THE GAME",
                "%s on this disc isn't the one of the %s release Spyro Vita knows (SHA-1 %.8s, "
                "it knows %.8s): another edition of the game (a reprint, a demo) or a bad dump. "
                "Spyro Vita needs Spyro the Dragon (USA), SCUS-94228.",
                r->exe, r->serial, exe_sha1, r->exe_sha1);
  }
  g_Region = r->id;
  g_RegionTiming = r->id == PSX_REGION_PAL && g_RegionPal50; /* PAL 60 by default: D138, D150 */
  if (!r->supported && g_RegionDev) {
    say("--region-dev: opening a release that isn't supported yet");
    return 0;
  }
  if (!r->supported)
    return fail(err, PSX_CD_ERR_NOT_SUPPORTED, "NOT SUPPORTED YET",
                "This is Spyro the Dragon (%s), %s. Spyro Vita recognises it but can't run it "
                "yet: the European and Japanese releases are being worked on. For now Spyro Vita "
                "needs Spyro the Dragon (USA), SCUS-94228.",
                r->area, r->serial);
  return 0;
}

/* The marker of the WAD.WAD check, in the region's save folder (D132): the
 * path set for the US disc, with pal/ or jp/ before its file name. */
static void marker_path(char *out, size_t cap) {
  char dir[512];
  const char *slash = strrchr(s_marker, '/');
  if (!slash) {
    psx_region_path(out, cap, ".", s_marker, 1);
    return;
  }
  snprintf(dir, sizeof dir, "%.*s", (int)(slash - s_marker), s_marker);
  psx_region_path(out, cap, dir, slash + 1, 1);
}

static int check_disc(const Table *t, PsxCdError *err) {
  const PsxCdImageFile *exe = NULL, *wad = lookup(t, WAD_NAME);
  const PsxRegion *r = NULL;
  char hex[41], boot[64], serial[32], key[700], marker[600], msg[800];
  const char *area;
  int i;
  boot_exe(t, boot, sizeof boot);
  if (boot[0])
    r = psx_region_by_exe(boot);
  /* no SYSTEM.CNF, or it boots something else: a known executable, as
   * before the region table */
  for (i = 0; !r && i < PSX_REGION_COUNT; i++)
    if (lookup(t, psx_region_info(i)->exe))
      r = psx_region_info(i);
  if (r)
    exe = lookup(t, r->exe);
  if (!exe) {
    psx_region_serial(boot, serial, sizeof serial, &area);
    if (!serial[0])
      return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG DISC",
                  "%s isn't a PlayStation game disc. Spyro Vita needs Spyro the Dragon (USA), "
                  "SCUS-94228.",
                  s_img.name);
    return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG DISC",
                "This is %s%s%s%s, not Spyro the Dragon (USA). Spyro Vita needs SCUS-94228.",
                serial, *area ? " (" : "", area, *area ? ")" : "");
  }
  if (hash_file(exe, hex, 0) != 0)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "THE IMAGE IS DAMAGED",
                "%s can't be read from %s. Copy the image again, or dump the disc again.",
                r->exe, s_img.name);
  if (r->id != PSX_REGION_US) {
    snprintf(msg, sizeof msg, "%s is Spyro the Dragon (%s), %s, SHA-1 %.8s", r->exe, r->area,
             r->serial, hex);
    say(msg);
  }
  if (psx_cd_check_release(r, hex, err) != 0)
    return -1;
  if (!s_marker[0])
    return 0;
  if (!wad)
    return fail(err, PSX_CD_ERR_WRONG_DISC, "WRONG DISC", "This disc has no %s.", WAD_NAME);
  marker_path(marker, sizeof marker);
  if (verified_before(marker, key, sizeof key)) {
    say("WAD.WAD checked before (same image, size and date)");
    return 0;
  }
  if (hash_file(wad, hex, 1) != 0)
    return fail(err, PSX_CD_ERR_BAD_IMAGE, "THE IMAGE IS DAMAGED",
                "%s can't be read from %s. Copy the image again, or dump the disc again.",
                WAD_NAME, s_img.name);
  if (strcmp(hex, r->wad_sha1) != 0)
    return fail(err, PSX_CD_ERR_WRONG_DISC, "BAD DUMP",
                "%s on this disc doesn't match a good dump of %s (SHA-1 %.8s, needs "
                "%.8s). Dump the disc again.",
                WAD_NAME, r->serial, hex, r->wad_sha1);
  if (key[0]) {
    FILE *f = fopen(marker, "wb");
    if (f) {
      fprintf(f, "%s\n", key);
      fclose(f);
    }
  }
  snprintf(msg, sizeof msg, "WAD.WAD matches a good dump; marker %s", marker);
  say(msg);
  return 0;
}

/* The image ends before sector need (an LBA): copied or dumped only in part. */
static int cut_short(PsxCdError *err, int need) {
  return fail(err, PSX_CD_ERR_BAD_IMAGE, "THE IMAGE IS CUT SHORT",
              "%s is cut short: %ld MB of the %ld MB or more the game needs. Copy it again, or "
              "dump the disc again.",
              s_img.name, s_img.size >> 20,
              (long)((long long)(need - s_disc.l.base) * s_disc.l.sector >> 20));
}

int psx_cdimage_open(const char *dir, PsxCdImageFile **files, int *nfiles, PsxCdError *err) {
  unsigned char pvd[2048];
  Table t = {0};
  int i, need = 0;
  psx_cdimage_close();
  if (find_image(dir, err) != 0)
    return -1;
  if (read_form1(16, 1, pvd) != 0 || memcmp(pvd + 1, "CD001", 5) != 0) {
    fail(err, PSX_CD_ERR_BAD_IMAGE, "CAN'T READ THE IMAGE",
         "%s has no CD file system where a PlayStation disc has one. Dump the disc again as "
         ".bin + .cue (MODE2/2352).",
         s_img.name);
    goto bad;
  }
  {
    int r = vpk_disc_walk(&s_disc, add_file, &t);
    t.bad_lba = s_disc.bad_lba;
    if (r != 0 || t.n == 0) {
      if (t.bad_lba - s_disc.l.base >= s_disc.nsect) {
        cut_short(err, t.bad_lba + 1);
        goto bad;
      }
      fail(err, PSX_CD_ERR_BAD_IMAGE, "THE IMAGE IS DAMAGED",
           "%s: the disc's file list can't be read (sector %d). Copy the image again, or dump "
           "the disc again.",
           s_img.name, t.bad_lba);
      goto bad;
    }
  }
  qsort(t.v, (size_t)t.n, sizeof *t.v, by_lba);
  for (i = 0; i < t.n; i++) {
    int end = t.v[i].lba + (t.v[i].size + 2047) / 2048;
    if (end > need)
      need = end;
  }
  if (need - s_disc.l.base > s_disc.nsect) {
    cut_short(err, need);
    goto bad;
  }
  if (check_disc(&t, err) != 0)
    goto bad;
  *files = t.v;
  *nfiles = t.n;
  return 0;
bad:
  free(t.v);
  psx_cdimage_close();
  return -1;
}
