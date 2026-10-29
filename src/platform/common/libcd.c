/* PsyQ libcd over the files extracted from the user's disc
 * (docs/porting-plan.md §6). manifest.json maps LBAs to files; data files hold
 * 2048 bytes per sector, XA files 2336 (subheader + data). CdRead completes on
 * a deterministic CD tick (psx_cd_vblank or a CdSync(1) poll), then fires the
 * read callback like the PS1 interrupt. ReadS streams move the head at the
 * real rate (75 or 150 sectors/s) per virtual vblank; no audio yet. */
#include "psx_cd.h"
#include "psx_hooks.h"

#include <libcd.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VBLANK_HZ 60 /* virtual vblank rate the stream speed is based on */

typedef struct {
  char path[128];
  int lba, nsect, size, xa;
  FILE *fp;
} CdFile;

static CdFile *s_files;
static int s_nfiles;
static char s_dir[256];

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

static PsxCdStream s_stream = {.mix = {0x80, 0, 0, 0x80}};

/* File access (stdio works on both hosts and on the Vita's newlib). */
static FILE *cdio_open(const char *path) { return fopen(path, "rb"); }
static int cdio_read_at(FILE *f, long off, void *buf, int n) {
  if (fseek(f, off, SEEK_SET) != 0)
    return 0;
  return (int)fread(buf, 1, (size_t)n, f);
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
  snprintf(s_dir, sizeof s_dir, "%s", disc_dir);
  snprintf(path, sizeof path, "%s/manifest.json", disc_dir);
  f = cdio_open(path);
  if (!f) {
    fprintf(stderr, "libcd: cannot open %s\n", path);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  len = ftell(f);
  json = malloc((size_t)len + 1);
  if (!json || cdio_read_at(f, 0, json, (int)len) != len) {
    fclose(f);
    free(json);
    return -1;
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
      fprintf(stderr, "libcd: bad manifest entry near byte %d\n", (int)(p - json));
      free(json);
      cd_close_files();
      return -1;
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
  if (s_nfiles == 0) {
    fprintf(stderr, "libcd: %s lists no files\n", path);
    return -1;
  }
  return 0;
}

void psx_cd_set_read_latency(int ticks) { cd.latency = ticks < 0 ? 0 : ticks; }
void psx_cd_set_log(int on) { cd.log = on; }
const PsxCdStream *psx_cd_stream(void) { return &s_stream; }

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

int psx_cd_read_raw(int lba, unsigned char raw[PSX_CD_RAW_SECTOR]) {
  CdFile *f = cd_find(lba);
  FILE *fp;
  memset(raw, 0, PSX_CD_RAW_SECTOR);
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

static void cd_complete_read(void) {
  unsigned char raw[PSX_CD_RAW_SECTOR];
  unsigned char *dst = cd.rd_buf;
  int big = cd.rd_mode & CdlModeSize1;
  CdlCB cb;

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

static void cd_tick(void) {
  if (cd.rd_pending && --cd.rd_ticks <= 0)
    cd_complete_read();
}

void psx_cd_vblank(void) {
  cd_tick();
  if (s_stream.active) {
    cd.head_q += (cd.mode & CdlModeSpeed) ? 150 * 4 / VBLANK_HZ : 75 * 4 / VBLANK_HZ;
    s_stream.lba = cd.head_q / 4;
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
    if (cd.rd_pending)
      cd_complete_read();
  } else if (cd.rd_pending) {
    cd_tick();
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
  if (cd.latency == 0)
    cd_complete_read();
  return 1;
}

int CdReadSync(int mode, u_char *result) {
  if (mode == 0 && cd.rd_pending)
    cd_complete_read();
  if (result) {
    memset(result, 0, 8);
    result[0] = cd.stat;
  }
  return cd.rd_pending ? cd.rd_count : 0;
}

void CdReadBreak(void) {
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
