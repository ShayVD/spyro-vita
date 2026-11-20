/* PsyQ libcd over the user's disc (docs/porting-plan.md §6): the files
 * extracted by tools/extract_disc.py, where manifest.json maps LBAs to files
 * (data files hold 2048 bytes per sector, XA files 2336: subheader + data),
 * or without a manifest.json a raw disc image in the same folder, read
 * directly (cdimage.c, decisions D60). Both give psx_cd_read_raw the same
 * bytes for every LBA in a file. CdRead completes on
 * a deterministic CD tick (psx_cd_vblank or a CdSync(1) poll), then fires the
 * read callback like the PS1 interrupt. ReadS streams move the head at the
 * real rate (75 or 150 sectors/s) per virtual vblank, and xa.c decodes the
 * sectors they pass. With cd_native (cdnative.c, D93) the model stays the
 * same and only the bytes come another way: a CdRead's range as file reads
 * on a worker, the stream's sectors from its read-ahead. */
#include "psx_cd.h"
#include "psx_cdimage.h"
#include "psx_cdnative.h"
#include "psx_hooks.h"
#include "psx_region.h"
#include "psx_state.h"
#include "psx_xa.h"

#include <libcd.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
  char path[128];
  int lba, nsect, size, xa;
  FILE *fp;
} CdFile;

static CdFile *s_files;
static int s_nfiles;
static char s_dir[256];
static int s_image; /* the files are in a disc image (cdimage.c), not extracted */
static PsxCdError s_err;
static PsxCdIo s_io;
static unsigned long long (*s_clock)(void);

static struct {
  unsigned char stat, last_com, mode;
  int setloc;             /* LBA of the last Setloc */
  CdlLOC lastpos;
  int head;               /* LBA under the head when not streaming */
  int head_q;             /* streaming: head in quarter sectors */
  int sync_status;
  unsigned char result[8];
  int rd_pending, rd_lba, rd_count, rd_ticks, rd_mode;
  unsigned char *rd_buf;
  CdlCB read_cb, sync_cb, ready_cb;
  int latency, log;
} cd = {.latency = 1, .sync_status = CdlComplete, .stat = CdlStatStandby};

/* D159's switch and its poll count. Settings of the run, not the drive's
 * state: outside cd, which psx_cd_state saves whole, so a save state keeps
 * v1.2.0's layout and a state load can't change the setting. */
static int s_async; /* a native read completes when the worker is done, not after latency ticks */
static int s_polls; /* PortCdLoadPoll's calls since the last vblank (or the read's start) that found it still with the worker */
enum { POLLS_A_VBLANK = 4 }; /* more than the loaders make in a frame; a loop that waits for the read passes it at once */
/* where the asynchronous reads were completed, for the "cd: asynchronous" line */
enum { ASYNC_VBLANK, ASYNC_SYNC_POLL, ASYNC_LOAD_POLL, ASYNC_LOOP_WAIT, ASYNC_SYNC_WAIT, ASYNC_WHERES };
static unsigned s_async_at[ASYNC_WHERES];

static PsxCdStream s_stream = {.mix = {0x80, 0, 0, 0x80}};

const PsxCdError *psx_cd_error(void) { return &s_err; }
const char *psx_cd_source(void) { return s_image ? psx_cdimage_path() : "files"; }
void psx_cd_set_clock(unsigned long long (*now_us)(void)) { s_clock = now_us; }
void psx_cd_io(PsxCdIo *out) {
  *out = s_io;
  if (g_psx_cd_native) { /* the worker's CdReads (cdnative.c, D93) */
    PsxCdnStats st;
    psx_cdn_stats(&st);
    out->reads += st.reads;
    out->kbytes += st.kbytes;
    out->us += st.us;
  }
  if (psx_cdimage_pbp()) { /* the copy's file reads (compressed bytes) are its decoder's, not the kit's (D158) */
    PsxPbpStats g, w;
    psx_cdimage_pbp_stats(&g);
    psx_cdn_pbp_stats(&w);
    out->reads += g.reads + w.reads;
    out->kbytes += (unsigned)((g.read_bytes + w.read_bytes) >> 10);
    out->us += g.read_us + w.read_us;
  }
}
unsigned long long psx_cd_now(void) { return s_clock ? s_clock() : 0; }

void psx_cd_io_add(unsigned bytes, unsigned long long t0) {
  s_io.reads++;
  s_io.kbytes += bytes / 1024; /* whole windows or sectors: rounding is small */
  if (s_clock)
    s_io.us += s_clock() - t0;
}

/* File access (stdio works on both hosts and on the Vita's newlib). */
static FILE *cdio_open(const char *path) { return fopen(path, "rb"); }
static int cdio_read_at(FILE *f, long off, void *buf, int n) {
  unsigned long long t0 = psx_cd_now();
  int got;
  if (fseek(f, off, SEEK_SET) != 0)
    return 0;
  got = (int)fread(buf, 1, (size_t)n, f);
  psx_cd_io_add(got > 0 ? (unsigned)got : 0, t0);
  return got;
}

/* manifest.json: [{"path": "...", "lba": N, "size": N, "type": "data"|"xa"}, ...] */
static const char *json_key(const char *obj, const char *end, const char *key) {
  size_t n = strlen(key);
  for (const char *p = obj; p + n + 2 < end; p++) {
    if (p[0] == '"' && strncmp(p + 1, key, n) == 0 && p[n + 1] == '"') {
      p += n + 2;
      while (p < end && (isspace((unsigned char)*p) || *p == ':'))
        p++;
      return p;
    }
  }
  return NULL;
}

static int json_str(const char *p, const char *end, char *out, size_t cap) {
  size_t n = 0;
  if (!p || *p != '"')
    return -1;
  for (p++; p < end && *p != '"'; p++) {
    if (*p == '\\' && p + 1 < end)
      p++;
    if (n + 1 >= cap)
      return -1;
    out[n++] = *p;
  }
  out[n] = 0;
  return 0;
}

static void cd_close_files(void) {
  for (int i = 0; i < s_nfiles; i++)
    if (s_files[i].fp)
      fclose(s_files[i].fp);
  free(s_files);
  s_files = NULL;
  s_nfiles = 0;
  s_image = 0;
  psx_cdimage_close();
}

static int manifest_error(const char *path, const char *why) {
  fprintf(stderr, "libcd: %s %s\n", path, why);
  s_err.code = PSX_CD_ERR_MANIFEST;
  snprintf(s_err.title, sizeof s_err.title, "MANIFEST.JSON IS DAMAGED");
  snprintf(s_err.text, sizeof s_err.text,
           "%s %s. Run tools/extract_disc.py again, or delete manifest.json and copy the disc "
           "image (.bin + .cue) here instead.",
           path, why);
  return -1;
}

/* No manifest.json: a disc image in the folder, its file table from the
 * image's own directory. */
static int open_image(const char *disc_dir) {
  PsxCdImageFile *img;
  int n;
  if (psx_cdimage_open(disc_dir, &img, &n, &s_err) != 0) {
    fprintf(stderr, "libcd: %s: %s\n", s_err.title, s_err.text);
    return -1;
  }
  s_files = calloc((size_t)n, sizeof *s_files);
  if (!s_files) {
    free(img);
    return -1;
  }
  for (int i = 0; i < n; i++) {
    CdFile *cf = &s_files[i];
    snprintf(cf->path, sizeof cf->path, "%s", img[i].path);
    cf->lba = img[i].lba;
    cf->size = img[i].size;
    cf->nsect = (cf->size + 2047) / 2048;
    cf->xa = img[i].xa;
  }
  free(img);
  s_nfiles = n;
  s_image = 1;
  return 0;
}

static int have_file(const char *path) {
  for (int i = 0; i < s_nfiles; i++) {
    const char *a = s_files[i].path, *b = path;
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++)
      ;
    if (!*a && !*b)
      return 1;
  }
  return 0;
}

/* The extracted files' release (D132): SYSTEM.CNF's BOOT file, or else a
 * known executable in the manifest. The US files aren't hashed, as before
 * (tools/extract_disc.py checked them); another region's executable is, and
 * the release is refused until its stages land (psx_cd_check_release). */
static int manifest_has(void *user, const char *name) {
  (void)user;
  return have_file(name);
}

static int files_region(void) {
  const PsxRegion *r;
  char path[512], buf[2048], exe[64] = "", hex[41];
  unsigned char block[4096];
  size_t n;
  PsxSha1 sha;
  FILE *f;
  snprintf(path, sizeof path, "%s/SYSTEM.CNF", s_dir);
  if ((f = fopen(path, "rb"))) {
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    psx_region_boot_exe(buf, n, exe, sizeof exe);
  }
  r = psx_region_of_disc(exe, manifest_has, NULL);
  if (!r || r->id == PSX_REGION_US)
    return 0;
  snprintf(path, sizeof path, "%s/%s", s_dir, r->exe);
  if (!(f = fopen(path, "rb"))) {
    s_err.code = PSX_CD_ERR_MANIFEST;
    snprintf(s_err.title, sizeof s_err.title, "A FILE IS MISSING");
    snprintf(s_err.text, sizeof s_err.text,
             "%s is in manifest.json but not in %s. Run tools/extract_disc.py again.", r->exe,
             s_dir);
    return -1;
  }
  psx_sha1_init(&sha);
  while ((n = fread(block, 1, sizeof block, f)) > 0)
    psx_sha1_update(&sha, block, (unsigned)n);
  fclose(f);
  psx_sha1_hex(&sha, hex);
  fprintf(stderr, "libcd: %s is Spyro the Dragon (%s), %s, SHA-1 %.8s\n", r->exe, r->area,
          r->serial, hex);
  return psx_cd_check_release(r, hex, &s_err);
}

int psx_cd_open(const char *disc_dir) {
  char path[512], type[8];
  FILE *f;
  long len;
  char *json;
  int cap = 0;
  const char *e = getenv("SPYRO_CD_LOG");

  if (e && *e && *e != '0')
    cd.log = 1;
  cd_close_files();
  memset(&s_err, 0, sizeof s_err);
  g_Region = PSX_REGION_US;
  g_RegionTiming = 0;
  snprintf(s_dir, sizeof s_dir, "%s", disc_dir);
  snprintf(path, sizeof path, "%s/manifest.json", disc_dir);
  f = cdio_open(path);
  if (!f) /* no extracted files: a disc image (D60) */
    return open_image(disc_dir);
  fprintf(stderr, "libcd: extracted files (%s)\n", path);
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  json = malloc((size_t)len + 1);
  if (!json || cdio_read_at(f, 0, json, (int)len) != len) {
    fclose(f);
    free(json);
    return manifest_error(path, "can't be read");
  }
  fclose(f);
  json[len] = 0;

  for (const char *p = strchr(json, '{'); p; p = strchr(p, '{')) {
    const char *end = strchr(p, '}'), *v;
    CdFile *cf;
    if (!end)
      break;
    if (s_nfiles == cap) {
      cap = cap ? cap * 2 : 32;
      s_files = realloc(s_files, sizeof *s_files * (size_t)cap);
    }
    cf = &s_files[s_nfiles];
    memset(cf, 0, sizeof *cf);
    if (json_str(json_key(p, end, "path"), end, cf->path, sizeof cf->path) ||
        json_str(json_key(p, end, "type"), end, type, sizeof type) ||
        !(v = json_key(p, end, "lba"))) {
      int at = (int)(p - json);
      char why[64];
      free(json);
      cd_close_files();
      snprintf(why, sizeof why, "has a bad entry near byte %d", at);
      return manifest_error(path, why);
    }
    cf->lba = atoi(v);
    v = json_key(p, end, "size");
    cf->size = v ? atoi(v) : 0;
    cf->nsect = (cf->size + 2047) / 2048;
    cf->xa = strcmp(type, "xa") == 0;
    s_nfiles++;
    p = end + 1;
  }
  free(json);
  if (s_nfiles == 0)
    return manifest_error(path, "lists no files");
  if (files_region() != 0) {
    fprintf(stderr, "libcd: %s: %s\n", s_err.title, s_err.text);
    cd_close_files();
    return -1;
  }
  return 0;
}

void psx_cd_set_read_latency(int ticks) { cd.latency = ticks < 0 ? 0 : ticks; }
void psx_cd_set_read_async(int on) { s_async = on != 0; }
int psx_cd_read_async(void) { return s_async; }
void psx_cd_set_log(int on) { cd.log = on; }
const PsxCdStream *psx_cd_stream(void) { return &s_stream; }

/* Save states (D51): the drive, a read in flight (its buffer is in the arena,
 * its callback in the executable) and the stream; not the configuration. */
void psx_cd_state(PsxStateIO *io) {
  int latency = cd.latency, log = cd.log;
  psx_cdn_read_drain(); /* savestate.c drained it before the arena too */
  psx_state_io(io, &cd, sizeof cd);
  psx_state_io(io, &s_stream, sizeof s_stream);
  cd.latency = latency;
  cd.log = log;
  if (io->mode == PSX_STATE_LOAD && g_psx_cd_native && s_stream.active)
    psx_cdn_stream_start(s_stream.lba);
}

static CdFile *cd_find(int lba) {
  int lo = 0, hi = s_nfiles - 1;
  while (lo <= hi) { /* manifest is sorted by LBA */
    int mid = (lo + hi) / 2;
    CdFile *f = &s_files[mid];
    if (lba < f->lba)
      hi = mid - 1;
    else if (lba >= f->lba + f->nsect)
      lo = mid + 1;
    else
      return f;
  }
  for (int i = 0; i < s_nfiles; i++) /* in case it was not sorted */
    if (lba >= s_files[i].lba && lba < s_files[i].lba + s_files[i].nsect)
      return &s_files[i];
  return NULL;
}

static FILE *cd_fp(CdFile *f) {
  if (!f->fp) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", s_dir, f->path);
    f->fp = cdio_open(path);
    if (!f->fp)
      fprintf(stderr, "libcd: cannot open %s\n", path);
  }
  return f->fp;
}

/* From the image: XA sectors as they are on the disc; data sectors as
 * extract_disc.py writes them (the same made-up data subheader as below, and
 * zeros past the file's end), so both backends serve the same bytes. */
static int image_read_raw(CdFile *f, int lba, unsigned char *raw) {
  int n;
  if (f->xa)
    return psx_cdimage_read(lba, 16, raw, PSX_CD_RAW_SECTOR) < 0 ? -1 : 0;
  raw[2] = raw[6] = 0x08;
  n = f->size - (lba - f->lba) * 2048;
  return psx_cdimage_read(lba, 24, raw + 8, n < 2048 ? n : 2048) < 0 ? -1 : 0;
}

int psx_cd_read_raw(int lba, unsigned char raw[PSX_CD_RAW_SECTOR]) {
  CdFile *f = cd_find(lba);
  FILE *fp;
  memset(raw, 0, PSX_CD_RAW_SECTOR);
  if (f && s_image)
    return image_read_raw(f, lba, raw);
  if (!f || !(fp = cd_fp(f)))
    return -1;
  if (f->xa) {
    cdio_read_at(fp, (long)(lba - f->lba) * PSX_CD_RAW_SECTOR, raw, PSX_CD_RAW_SECTOR);
  } else {
    raw[2] = raw[6] = 0x08; /* submode: data */
    cdio_read_at(fp, (long)(lba - f->lba) * 2048, raw + 8, 2048);
  }
  return 0;
}

int psx_cd_read_file(const char *path, void *buf, int cap) {
  unsigned char raw[PSX_CD_RAW_SECTOR];
  const CdFile *f = NULL;
  int i, n;
  for (i = 0; i < s_nfiles && !f; i++) {
    const char *a = s_files[i].path, *b = path;
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++)
      ;
    if (!*a && !*b)
      f = &s_files[i];
  }
  if (!f || f->xa)
    return -1;
  if (!buf)
    return f->size;
  n = f->size < cap ? f->size : cap;
  for (i = 0; i * 2048 < n; i++) {
    int k = n - i * 2048 < 2048 ? n - i * 2048 : 2048;
    if (psx_cd_read_raw(f->lba + i, raw) != 0)
      return -1;
    memcpy((unsigned char *)buf + i * 2048, raw + 8, (size_t)k);
  }
  return n;
}

int psx_cd_xa_locate(int lba, PsxCdXaLoc *loc) {
  CdFile *f = cd_find(lba);
  if (!f || !f->xa)
    return -1;
  loc->left = f->lba + f->nsect - lba;
  if (s_image && psx_cdimage_pbp())
    return -1; /* the digital copy's sectors can't be read by offset: its reader's are the game thread's */
  if (s_image) {
    int sector, base;
    psx_cdimage_layout(&sector, &base);
    snprintf(loc->path, sizeof loc->path, "%s", psx_cdimage_path());
    loc->offset = (long)(lba - base) * sector;
    loc->stride = sector;
    loc->skip = 16; /* sync and header, as image_read_raw */
  } else {
    snprintf(loc->path, sizeof loc->path, "%s/%s", s_dir, f->path);
    loc->offset = (long)(lba - f->lba) * PSX_CD_RAW_SECTOR;
    loc->stride = PSX_CD_RAW_SECTOR;
    loc->skip = 0;
  }
  return 0;
}

int psx_cd_locate(int lba, PsxCdLoc *loc) {
  CdFile *f = cd_find(lba);
  int rel;
  if (!f)
    return -1;
  rel = lba - f->lba;
  loc->left = f->nsect - rel;
  loc->xa = f->xa;
  loc->data_limit = -1;
  loc->pbp = 0;
  if (s_image) {
    int sector, base;
    psx_cdimage_layout(&sector, &base);
    snprintf(loc->path, sizeof loc->path, "%s", psx_cdimage_path());
    loc->pbp = psx_cdimage_pbp() != NULL;
    loc->offset = loc->pbp ? lba : (long)(lba - base) * sector; /* the copy: an LBA, not bytes */
    loc->stride = sector;
    loc->raw_skip = 16; /* sync and header, as image_read_raw */
    loc->data_skip = 24;
    if (!f->xa)
      loc->data_limit = (long)f->size - (long)rel * 2048;
  } else {
    snprintf(loc->path, sizeof loc->path, "%s/%s", s_dir, f->path);
    loc->stride = f->xa ? PSX_CD_RAW_SECTOR : 2048;
    loc->offset = (long)rel * loc->stride;
    loc->raw_skip = 0;
    loc->data_skip = f->xa ? 8 : 0;
  }
  return 0;
}

static unsigned char bcd(int v) { return (unsigned char)(((v / 10) << 4) | (v % 10)); }
static int unbcd(unsigned char v) { return (v >> 4) * 10 + (v & 15); }

CdlLOC *CdIntToPos(int i, CdlLOC *p) {
  i += 150;
  p->minute = bcd(i / 4500);
  p->second = bcd(i / 75 % 60);
  p->sector = bcd(i % 75);
  return p;
}

int CdPosToInt(CdlLOC *p) {
  return (unbcd(p->minute) * 60 + unbcd(p->second)) * 75 + unbcd(p->sector) - 150;
}

static int cd_head(void) { return s_stream.active ? cd.head_q / 4 : cd.head; }

static void cd_set_stat(void) {
  cd.stat = CdlStatStandby | (s_stream.active || cd.rd_pending ? CdlStatRead : 0);
}

static void cd_stream_stop(void) {
  if (s_stream.active)
    cd.head = cd.head_q / 4;
  s_stream.active = 0;
}

/* The read's sectors into its buffer, one psx_cd_read_raw each. */
static void cd_copy_sectors(void) {
  unsigned char raw[PSX_CD_RAW_SECTOR];
  unsigned char *dst = cd.rd_buf;
  int big = cd.rd_mode & CdlModeSize1;

  for (int i = 0; i < cd.rd_count; i++) {
    int lba = cd.rd_lba + i;
    if (psx_cd_read_raw(lba, raw) != 0)
      fprintf(stderr, "libcd: read of LBA %d outside any file\n", lba);
    if (big) { /* 2340: header + subheader + data */
      CdlLOC loc;
      CdIntToPos(lba, &loc);
      dst[0] = loc.minute;
      dst[1] = loc.second;
      dst[2] = loc.sector;
      dst[3] = 2;
      memcpy(dst + 4, raw, PSX_CD_RAW_SECTOR);
      dst += 2340;
    } else {
      memcpy(dst, raw + 8, 2048);
      dst += 2048;
    }
  }
}

/* cd_native (D93) reads 2048-byte sectors; the 2340-byte mode stays here */
static int cd_native_read(void) { return g_psx_cd_native && !(cd.rd_mode & CdlModeSize1); }

static void cd_complete_read(void) {
  unsigned long long t0 = psx_cd_now();
  CdlCB cb;

  if (cd_native_read())
    psx_cdn_read_finish(cd.rd_lba, cd.rd_count, cd.rd_buf);
  else
    cd_copy_sectors();
  s_io.loads++;
  s_io.wait_us += psx_cd_now() - t0;
  cd.head = cd.rd_lba + cd.rd_count;
  cd.rd_pending = 0;
  cd_set_stat();
  cd.sync_status = CdlComplete;
  memset(cd.result, 0, sizeof cd.result);
  cd.result[0] = cd.stat;
  cb = cd.read_cb;
  if (cb)
    cb(CdlComplete, cd.result);
}

/* an asynchronous read (D159): done when the worker is */
static int cd_async_read(void) { return s_async && cd_native_read(); }

static void cd_tick(int where) {
  if (!cd.rd_pending)
    return;
  if (cd_async_read()) {
    if (psx_cdn_read_done()) {
      s_async_at[where]++;
      cd_complete_read();
    }
  } else if (--cd.rd_ticks <= 0) {
    cd_complete_read();
  }
}

int psx_cd_async_report(char *out, size_t cap) {
  if (!s_async)
    return 0;
  snprintf(out, cap,
           "cd: asynchronous CdReads (D159): %u completed at a vblank, %u at a CdSync poll, %u at a "
           "CDLoadTime poll with the worker done; %u waited for in a loop that polls without a vblank "
           "(CDLoadSync), %u by CdSync(0)",
           s_async_at[ASYNC_VBLANK], s_async_at[ASYNC_SYNC_POLL], s_async_at[ASYNC_LOAD_POLL],
           s_async_at[ASYNC_LOOP_WAIT], s_async_at[ASYNC_SYNC_WAIT]);
  return 1;
}

/* CDLoadTime (cd.c) calls this first (patches/spyro-1-port/0016, D159).
 * Without an asynchronous read pending it does nothing and returns 0: the
 * other modes complete inside CdRead or at a tick, as before, so an image's
 * or the host's runs don't change.
 *
 * With one pending:
 * - The worker is done: the read is completed here. CDLoadSync spins on
 *   m_IsReading calling only CDLoadTime, without a VSync or a CdSync (on the
 *   PS1 the CD interrupt ends that loop), so no tick would complete it.
 * - The worker still has it, and this is one of the first POLLS_A_VBLANK
 *   calls since the last vblank: nothing. The loaders (loaders.c, the title
 *   screen, the credits) call CDLoadTime once a frame and draw meanwhile.
 * - The worker still has it, and the calls keep coming with no vblank
 *   between: this is a loop that only the read's end can leave (CDLoadSync,
 *   the title screen's wait before the demo), so the game thread sleeps
 *   until the worker is done (psx_cdn_read_finish's wait, as a synchronous
 *   read does) in place of spinning on its core.
 * Returns 1 while the worker still has the read. CDLoadTime then holds the
 * game's read timer at 0: the timer is the PS1's guard against a drive that
 * stopped answering (600 vblanks, then CdInit and the read again), the
 * worker always answers, and a retry here would wait for the same read and
 * then read it twice. So a slow card never trips it, as it never could
 * while reads completed inside CdRead. */
int PortCdLoadPoll(void) {
  if (!cd.rd_pending || !cd_async_read())
    return 0;
  if (psx_cdn_read_done()) {
    s_async_at[ASYNC_LOAD_POLL]++;
  } else {
    if (s_polls++ < POLLS_A_VBLANK)
      return 1;
    s_async_at[ASYNC_LOOP_WAIT]++;
  }
  cd_complete_read();
  return 0;
}

void psx_cd_vblank(void) {
  s_polls = 0;
  cd_tick(ASYNC_VBLANK);
  if (s_stream.active) {
    int from = s_stream.lba;
    /* the drive by real time: 2.5 sectors a 60 Hz vblank at double speed,
     * 3 a 50 Hz one (PAL at its own timing, D138) */
    int hz = psx_region_vblank_hz();
    cd.head_q += (cd.mode & CdlModeSpeed) ? 150 * 4 / hz : 75 * 4 / hz;
    s_stream.lba = cd.head_q / 4;
    for (; from < s_stream.lba; from++) /* sectors the head passed */
      psx_xa_sector(from);
  }
}

static void cd_getloc_l(unsigned char *res) {
  unsigned char raw[PSX_CD_RAW_SECTOR];
  int lba = cd_head();
  CdlLOC loc;
  CdIntToPos(lba, &loc);
  psx_cd_read_raw(lba, raw);
  res[0] = loc.minute;
  res[1] = loc.second;
  res[2] = loc.sector;
  res[3] = 2;
  memcpy(res + 4, raw, 4);
}

static void cd_getloc_p(unsigned char *res) {
  int lba = cd_head();
  CdlLOC loc;
  res[0] = 1; /* track */
  res[1] = 1; /* index */
  CdIntToPos(lba - 150, &loc); /* relative to track 1, which starts at LBA 0 */
  res[2] = loc.minute;
  res[3] = loc.second;
  res[4] = loc.sector;
  CdIntToPos(lba, &loc);
  res[5] = loc.minute;
  res[6] = loc.second;
  res[7] = loc.sector;
}

static int cd_command(u_char com, u_char *param, u_char *result) {
  unsigned char *res = cd.result;
  cd.last_com = com;
  memset(res, 0, sizeof cd.result);

  switch (com) {
  case CdlSetloc:
    if (param) {
      memcpy(&cd.lastpos, param, 3);
      cd.setloc = CdPosToInt((CdlLOC *)param);
    }
    break;
  case CdlSetmode:
    if (param)
      cd.mode = s_stream.mode = param[0];
    break;
  case CdlSetfilter:
    if (param) {
      s_stream.file = param[0];
      s_stream.chan = param[1];
    }
    break;
  case CdlReadN:
  case CdlReadS:
    if (param) {
      memcpy(&cd.lastpos, param, 3);
      cd.setloc = CdPosToInt((CdlLOC *)param);
    }
    if (!(cd.mode & CdlModeRT))
      fprintf(stderr, "libcd: data streaming (%s without ModeRT) is not supported\n",
              com == CdlReadS ? "ReadS" : "ReadN");
    s_stream.active = 1;
    cd.head_q = cd.setloc * 4;
    s_stream.lba = cd.setloc;
    psx_xa_reset(); /* a seek drops what the XA buffer held */
    if (g_psx_cd_native)
      psx_cdn_stream_start(cd.setloc);
    break;
  case CdlPlay:
  case CdlSeekL:
  case CdlSeekP:
    if (param) {
      memcpy(&cd.lastpos, param, 3);
      cd.setloc = CdPosToInt((CdlLOC *)param);
    }
    cd_stream_stop();
    cd.head = cd.setloc;
    if (com == CdlPlay)
      fprintf(stderr, "libcd: CD-DA play is not supported\n");
    break;
  case CdlPause:
  case CdlStop:
  case CdlStandby:
    cd_stream_stop();
    break;
  case CdlMute:
    s_stream.muted = 1;
    break;
  case CdlDemute:
    s_stream.muted = 0;
    break;
  case CdlGetparam:
    res[1] = cd.mode;
    res[3] = s_stream.file;
    res[4] = s_stream.chan;
    break;
  case CdlGetlocL:
    cd_getloc_l(res);
    break;
  case CdlGetlocP:
    cd_getloc_p(res);
    break;
  case CdlGetTN:
    res[1] = res[2] = 1;
    break;
  case CdlGetTD: {
    CdlLOC loc;
    const CdFile *last = s_nfiles ? &s_files[s_nfiles - 1] : NULL;
    CdIntToPos(last ? last->lba + last->nsect : 0, &loc);
    res[1] = loc.minute;
    res[2] = loc.second;
    break;
  }
  case CdlNop:
  case CdlForward:
  case CdlBackward:
    break;
  default:
    fprintf(stderr, "libcd: command 0x%02x ignored\n", com);
    break;
  }

  cd_set_stat();
  if (com != CdlGetlocL && com != CdlGetlocP)
    res[0] = cd.stat;
  cd.sync_status = CdlComplete;
  if (result)
    memcpy(result, res, sizeof cd.result);
  if (cd.sync_cb)
    cd.sync_cb(CdlComplete, res);
  return 1;
}

int CdControl(u_char com, u_char *param, u_char *result) { return cd_command(com, param, result); }
int CdControlB(u_char com, u_char *param, u_char *result) { return cd_command(com, param, result); }
int CdControlF(u_char com, u_char *param) { return cd_command(com, param, NULL); }

int CdSync(int mode, u_char *result) {
  if (mode == 0) {
    if (cd.rd_pending) {
      s_async_at[ASYNC_SYNC_WAIT] += cd_async_read();
      cd_complete_read();
    }
  } else if (cd.rd_pending) {
    cd_tick(ASYNC_SYNC_POLL);
  }
  if (result)
    memcpy(result, cd.result, sizeof cd.result);
  return cd.sync_status;
}

int CdRead(int sectors, u_long *buf, int mode) {
  if (sectors <= 0 || !buf)
    return 0;
  cd.mode = s_stream.mode = (unsigned char)mode;
  cd_stream_stop();
  cd.last_com = CdlReadN;
  cd.rd_lba = cd.setloc;
  cd.rd_count = sectors;
  cd.rd_buf = (unsigned char *)buf;
  cd.rd_mode = mode;
  cd.rd_ticks = cd.latency;
  cd.rd_pending = 1;
  cd.sync_status = CdlNoIntr;
  cd_set_stat();
  if (cd.log)
    fprintf(stderr, "libcd: CdRead lba %d sectors %d mode 0x%02x\n", cd.rd_lba, sectors, mode);
  if (cd_async_read()) { /* the worker reads; cd_tick completes it once done (D159) */
    s_polls = 0; /* a new read: its first polls look, whatever the last read's wait counted */
    psx_cdn_read_post(cd.rd_lba, cd.rd_count, cd.rd_buf);
  }
  else if (cd.latency == 0)
    cd_complete_read();
  else if (cd_native_read()) /* read while the ticks pass; complete at the same tick */
    psx_cdn_read_post(cd.rd_lba, cd.rd_count, cd.rd_buf);
  return 1;
}

int CdReadSync(int mode, u_char *result) {
  if (mode == 0 && cd.rd_pending) {
    s_async_at[ASYNC_SYNC_WAIT] += cd_async_read();
    cd_complete_read();
  }
  if (result) {
    memset(result, 0, 8);
    result[0] = cd.stat;
  }
  return cd.rd_pending ? cd.rd_count : 0;
}

void CdReadBreak(void) {
  psx_cdn_read_drain(); /* the buffer may get the whole read: the drive's DMA would stop part way */
  cd.rd_pending = 0;
  cd.sync_status = CdlComplete;
  cd_set_stat();
}

CdlCB CdReadCallback(CdlCB func) {
  CdlCB old = cd.read_cb;
  cd.read_cb = func;
  return old;
}

CdlCB CdSyncCallback(CdlCB func) {
  CdlCB old = cd.sync_cb;
  cd.sync_cb = func;
  return old;
}

CdlCB CdReadyCallback(CdlCB func) { /* no data sectors are streamed */
  CdlCB old = cd.ready_cb;
  cd.ready_cb = func;
  return old;
}

int CdInit(void) {
  psx_cdn_read_drain();
  cd.rd_pending = 0;
  cd_stream_stop();
  cd.mode = s_stream.mode = 0;
  cd.read_cb = cd.sync_cb = cd.ready_cb = NULL;
  cd.sync_status = CdlComplete;
  cd.last_com = CdlNop;
  memset(cd.result, 0, sizeof cd.result);
  cd_set_stat();
  if (s_nfiles == 0) {
    fprintf(stderr, "libcd: CdInit without a disc (call psx_cd_open first)\n");
    return 0;
  }
  return 1;
}

int CdReset(int mode) {
  (void)mode;
  return CdInit();
}

void CdFlush(void) {}
int CdSetDebug(int level) { (void)level; return 0; }
int CdStatus(void) { return cd.stat; }
int CdMode(void) { return cd.mode; }
int CdLastCom(void) { return cd.last_com; }
CdlLOC *CdLastPos(void) { return &cd.lastpos; }

int CdMix(CdlATV *vol) {
  s_stream.mix[0] = vol->val0;
  s_stream.mix[1] = vol->val1;
  s_stream.mix[2] = vol->val2;
  s_stream.mix[3] = vol->val3;
  return 1;
}

/* "\\DIR\\NAME.EXT;1" -> manifest entry. */
CdlFILE *CdSearchFile(CdlFILE *fp, char *name) {
  char want[128];
  size_t n = 0;
  for (const char *p = name; *p && *p != ';' && n + 1 < sizeof want; p++) {
    if (n == 0 && *p == '\\')
      continue;
    want[n++] = *p == '\\' ? '/' : (char)toupper((unsigned char)*p);
  }
  want[n] = 0;
  for (int i = 0; i < s_nfiles; i++) {
    char have[128];
    size_t k;
    for (k = 0; s_files[i].path[k] && k + 1 < sizeof have; k++)
      have[k] = (char)toupper((unsigned char)s_files[i].path[k]);
    have[k] = 0;
    if (strcmp(have, want) == 0) {
      CdIntToPos(s_files[i].lba, &fp->pos);
      fp->size = (u_int)s_files[i].size;
      snprintf(fp->name, sizeof fp->name, "%s", name);
      return fp;
    }
  }
  return NULL;
}
