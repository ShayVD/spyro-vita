/* The user's own disc image, read in place by sector: a 2048-byte .iso, or a raw image of 2352-byte sectors (2448
 * with subchannel data; a .bin or a CloneCD .img), mode 1 or mode 2 form 1, with any pregap.  CD sectors are the
 * same on PS1 and PS2, so the layer serves both.  64-bit offsets everywhere (a PS2 DVD image is over 2 GiB): the
 * kernel's sceIo on the Vita, fopen / fseeko with 64-bit off_t elsewhere.  Nothing is written.
 *
 * enc_dimg_sniff says what a file is (raw, .iso, or a format to convert first); enc_dimg_open opens it.  Reads go
 * straight to the file, or (windows = 1) through two windows of 32 sectors, one file-system read each, so a
 * game's sequential reads cost one call per 32 sectors.  enc_dimg_find looks a file up by ISO9660 path,
 * enc_dimg_walk lists the whole directory tree (with each record's CD-XA attributes).  SHA-1 for disc checks.
 * enc_dimg_find_image picks the image in a data folder (cue sheets first, enc_cue_parse), enc_dimg_boot_serial
 * reads SYSTEM.CNF's boot line; what each refusal says to the player stays in the game.  enc_dimg_open_source
 * serves the same reads, lookups and walk from sectors the game supplies (a container it decodes itself).
 *
 * From kh1-decomp host/port/cd_image.c @ 85befa4a (2048 / 2352 images, 64-bit reads, the path lookup) and
 * spyro-vita src/platform/common/cdimage.c @ 987363e (sniffing, sync-pattern layout and pregap, windows, the
 * directory walk, SHA-1; decisions D58, D60) and @ bb62f38 (the folder search, the cue sheet, the boot serial;
 * D60); enc_dimg_open_source from spyro-vita PR #146 (D158). */
#ifndef ENC_DISCIMAGE_H
#define ENC_DISCIMAGE_H

/* what enc_dimg_sniff found */
enum { ENC_DIMG_RAW, ENC_DIMG_ISO, ENC_DIMG_CHD, ENC_DIMG_PBP, ENC_DIMG_ECM, ENC_DIMG_UNKNOWN, ENC_DIMG_UNREADABLE };

typedef struct {
  int sector;   /* bytes a sector in the file: 2048, 2352 or 2448 */
  int data_off; /* where a sector's 2048 data bytes start: 0 (.iso), 16 (mode 1) or 24 (mode 2 form 1) */
  int base;     /* LBA of the file's first sector (a raw image's pregap) */
} EncDimgLayout;

typedef struct {
  EncDimgLayout l;
  long long size; /* bytes */
  int nsect;      /* whole sectors in the file */
  int open;       /* nonzero while open (a zeroed EncDimg is closed) */
  int fd;         /* the Vita: sceIo's */
  void *fp;       /* elsewhere: the FILE */
  int windows;
  struct {
    int lba, n;
    unsigned stamp;
    unsigned char *buf;
  } win[2];
  unsigned stamp;
  int bad_lba; /* enc_dimg_walk: the sector it could not read */
  /* optional I/O accounting: now() before each file-system read, io(bytes, t0) after it */
  unsigned long long (*now)(void);
  void (*io)(unsigned bytes, unsigned long long t0);
  /* a source of sectors in place of a file (enc_dimg_open_source): the last sector it gave */
  int (*source)(void *user, int lba, unsigned char raw[2352]);
  void *source_user;
  int source_lba;
  unsigned char *source_buf;
} EncDimg;

/* Opens d over a source of raw sectors in place of a file, for a disc kept in a container the game decodes itself
 * (spyro-vita reads the PlayStation Store's compressed PBP this way, its D158): read(user, lba, raw) fills the
 * 2352-byte raw sector at lba (mode 2: the subheader at 16, form 1's data at 24) and returns 0, or nonzero when it
 * can't; nsect is how many sectors it holds, from LBA 0.  enc_dimg_read_raw, enc_dimg_read, enc_dimg_find and
 * enc_dimg_walk then work as on a raw image, through a one-sector buffer: no windows, and the now / io hooks aren't
 * called (the source counts its own reads).  user must stay valid until enc_dimg_close.  0, or -1 (no memory, no
 * read, nsect <= 0); d as for enc_dimg_open. */
int enc_dimg_open_source(EncDimg *d, int (*read)(void *user, int lba, unsigned char raw[2352]), void *user,
                         int nsect);

/* What path is: ENC_DIMG_RAW (with *l: sector size, first LBA from the first sync pattern whose next sector has one
 * too, data offset from sector 16's mode), ENC_DIMG_ISO (2048-byte sectors, "CD001" at sector 16), a container to
 * convert (CHD, PBP, ECM), unknown, or unreadable.  Reads the first ~720 KiB. */
int enc_dimg_sniff(const char *path, EncDimgLayout *l);
/* Opens path with layout l (from enc_dimg_sniff); d zeroed before its first open, and an open d is closed first (a
 * reopen keeps the windows' buffers).  windows 1: reads through two 32-sector windows, 0: straight to the file.
 * 0 on success, -1 if it cannot be opened, -2 if its size cannot be read. */
int enc_dimg_open(EncDimg *d, const char *path, const EncDimgLayout *l, int windows);
/* enc_dimg_sniff then enc_dimg_open for a raw image or an .iso, and the primary volume descriptor checked (type 1,
 * "CD001"); 0 on success, else -1 (d is left closed). */
int enc_dimg_open_any(EncDimg *d, const char *path, int windows);
int enc_dimg_is_open(const EncDimg *d);
void enc_dimg_close(EncDimg *d);
/* n bytes from offset off of the sector at lba as the file holds it (a raw image: off 16 the subheader, 24 form 1's
 * data; an .iso: off 0 the data).  Returns n, or -1 past the end of the image or of the sector. */
int enc_dimg_read_raw(EncDimg *d, int lba, int off, void *buf, int n);
/* count sectors' 2048 data bytes from lba; 0, or -1 if any is past the end */
int enc_dimg_read(EncDimg *d, int lba, int count, void *buf);
/* A file by ISO9660 path ("\SYSTEM.CNF;1": '\' or '/' separators, a leading one optional, case and ";version"
 * ignored): its extent's LBA and size; 0 if found (a file, not a directory), else -1. */
int enc_dimg_find(EncDimg *d, const char *path, unsigned *lba, unsigned *size);

typedef struct {
  const char *path; /* "S0/CRASH.EXE": '/' between parts, no ";version" */
  int lba, size;
  int xa_attr;      /* the record's CD-XA attributes (0x1000 form 2, 0x2000 interleaved, 0x4000 CD-DA), or 0 */
} EncDimgEntry;
/* Every file in the directory tree (not the directories), depth first in directory order; each to fn, which
 * returns nonzero to stop the walk (then returned).  -1 when a directory cannot be read (its sector in
 * d->bad_lba), deeper than 8 or over 64 sectors. */
int enc_dimg_walk(EncDimg *d, int (*fn)(void *user, const EncDimgEntry *e), void *user);

/* ---- cue sheets and the image in a folder (from spyro-vita src/platform/common/cdimage.c @ bb62f38, D60) ---- */

/* a cue sheet track's mode (the sector size after the '/') */
enum {
  ENC_CUE_AUDIO, ENC_CUE_CDG, ENC_CUE_MODE1_2048, ENC_CUE_MODE1_2352, ENC_CUE_MODE2_2336, ENC_CUE_MODE2_2352,
  ENC_CUE_MODE2_2448, /* not in the cue format, but some dumps of 2448-byte sectors say it */
  ENC_CUE_OTHER       /* CDI/..., MODE2/2324 ...: a data track all the same */
};

#define ENC_CUE_TRACKS 99
typedef struct {
  int number; /* TRACK nn */
  int mode;   /* ENC_CUE_* */
  int sector; /* bytes a sector as the mode says (AUDIO 2352, CDG 2448), 0 if it says none */
  int file;   /* its FILE (enc_cue_file) */
  int index0; /* INDEX 00, the pregap's start in its file, in sectors; -1 if none */
  int index1; /* INDEX 01, the track's start in its file, in sectors; -1 if none */
  int pregap; /* PREGAP: sectors not in the file */
} EncCueTrack;

typedef struct {
  int ntracks, nfiles;
  EncCueTrack track[ENC_CUE_TRACKS];
  int file_at[ENC_CUE_TRACKS]; /* each FILE's name in names */
  char names[4096];
} EncCue;

/* A cue sheet's text (n bytes, no NUL needed): FILE (quoted, or unquoted up to its type word), TRACK, INDEX 00 / 01
 * and PREGAP, keywords in any case, CRLF or LF, a UTF-8 BOM; REM, CATALOG, TITLE ... skipped.  Multi-file sheets
 * (one FILE a track) and single-file ones alike.  Returns the number of tracks, or -1 when there is no FILE with a
 * TRACK under it, a TRACK before any FILE, or more than 99 tracks or 4 KiB of names. */
int enc_cue_parse(const char *text, int n, EncCue *c);
/* The data track: the first that isn't AUDIO or CDG (track 1 on a PS1 or PS2 disc), as an index into c->track;
 * -1 if there is none. */
int enc_cue_data_track(const EncCue *c);
/* FILE number file's name as the sheet spells it (folders and all) */
const char *enc_cue_file(const EncCue *c, int file);

/* why enc_dimg_find_image passed a candidate over (EncDimgFound.status) */
enum {
  ENC_IMG_OK,
  ENC_IMG_NONE,           /* no candidate in the folder (or it can't be listed) */
  ENC_IMG_KIND,           /* the image's data is a kind the game didn't take (kind says which: an .iso, a CHD,
                             unknown, unreadable ...) */
  ENC_IMG_CUE_UNREADABLE, /* the .cue can't be opened */
  ENC_IMG_CUE_BAD,        /* no FILE with a TRACK in it (enc_cue_parse) */
  ENC_IMG_CUE_TRACK,      /* no data track, or its mode isn't one the game takes (mode says which) */
  ENC_IMG_CUE_MISSING     /* the file the data track names isn't in the folder (image says which) */
};

typedef struct {
  int status;      /* ENC_IMG_* */
  char name[256];  /* the candidate: a .cue, or the image itself */
  char image[256]; /* the image's file name as the folder spells it (a .cue's FILE: as the sheet does if missing) */
  char path[512];  /* the folder and image: what to open */
  int cue;         /* 1 when name is a .cue */
  int track, mode; /* a .cue's data track: its number and ENC_CUE_* mode; else 0 and -1 */
  int kind;        /* enc_dimg_sniff's ENC_DIMG_* for the image (ENC_DIMG_UNREADABLE until sniffed) */
  EncDimgLayout l; /* its layout when kind is ENC_DIMG_RAW or ENC_DIMG_ISO */
} EncDimgFound;

typedef struct {
  unsigned kinds;     /* the kinds the game takes, as bits 1u << ENC_DIMG_*; 0: raw images and .iso */
  unsigned cue_modes; /* the data track modes it takes from a .cue, as bits 1u << ENC_CUE_*; 0: any */
  void (*seen)(void *user, const EncDimgFound *f); /* optional: each candidate tried, as it was found */
  void *user;
} EncDimgSearch;

/* The first usable image in dir.  Candidates are the files named .cue, .bin, .img, .iso, .chd, .pbp or .ecm (the
 * extension in any case): every .cue first, then the others, each group in name order ignoring case.  A .cue is
 * followed to its data track's FILE, looked up in dir ignoring case and any folder in the name (the Vita's memory
 * card ignores case; Linux doesn't).  What an image is comes from its data (enc_dimg_sniff), never its name: a raw
 * image named .iso is raw.  A candidate the game can't take (s->kinds, s->cue_modes) is passed over for the next
 * one; a .cue it can't use leaves its image to be tried on its own.  0 with *out the image; -1 with *out the first
 * candidate's reason (ENC_IMG_NONE if there was none).  s may be NULL (the defaults).  Opens nothing: the game
 * calls enc_dimg_open(d, out->path, &out->l, ...) and says what each status means to the player. */
int enc_dimg_find_image(const char *dir, const EncDimgSearch *s, EncDimgFound *out);

/* The boot executable in a SYSTEM.CNF's text (n bytes) as a serial: the BOOT (PS1) or BOOT2 (PS2) line's file,
 * any device and folders dropped, '.' and '_' removed, a '-' after the first four ("cdrom:\SCUS_942.28;1" and
 * "cdrom0:\SLUS_203.70;1" give "SCUS-94228" and "SLUS-20370"), upper case.  0, or -1 with "" when there is no
 * BOOT line.  For telling a wrong disc or region apart from a bad dump; the wording is the game's. */
int enc_dimg_boot_serial(const char *cnf, int n, char *serial, int cap);

/* SHA-1 (FIPS 180-4) */
typedef struct {
  unsigned int h[5];
  unsigned long long len;
  unsigned char buf[64];
} EncSha1;
void enc_sha1_init(EncSha1 *s);
void enc_sha1_update(EncSha1 *s, const void *data, unsigned n);
void enc_sha1_hex(EncSha1 *s, char out[41]);

#endif /* ENC_DISCIMAGE_H */
