/* The user's own disc image, read in place by sector: a 2048-byte .iso, or a raw image of 2352-byte sectors (2448
 * with subchannel data; a .bin or a CloneCD .img), mode 1 or mode 2 form 1, with any pregap.  CD sectors are the
 * same on PS1 and PS2, so the layer serves both.  64-bit offsets everywhere (a PS2 DVD image is over 2 GiB): the
 * kernel's sceIo on the Vita, fopen / fseeko with 64-bit off_t elsewhere.  Nothing is written.
 *
 * vpk_disc_sniff says what a file is (raw, .iso, or a format to convert first); vpk_disc_open opens it.  Reads go
 * straight to the file, or (windows = 1) through two windows of 32 sectors, one file-system read each, so a
 * game's sequential reads cost one call per 32 sectors.  vpk_disc_find looks a file up by ISO9660 path,
 * vpk_disc_walk lists the whole directory tree (with each record's CD-XA attributes).  SHA-1 for disc checks.
 * vpk_disc_find_image picks the image in a data folder (cue sheets first, vpk_cue_parse), vpk_disc_boot_serial
 * reads SYSTEM.CNF's boot line; what each refusal says to the player stays in the game.
 *
 * From kh1-decomp host/port/cd_image.c @ 85befa4a (2048 / 2352 images, 64-bit reads, the path lookup) and
 * spyro-vita src/platform/common/cdimage.c @ 987363e (sniffing, sync-pattern layout and pregap, windows, the
 * directory walk, SHA-1; decisions D58, D60) and @ bb62f38 (the folder search, the cue sheet, the boot serial;
 * D60). */
#ifndef VPK_DISCIMAGE_H
#define VPK_DISCIMAGE_H

/* what vpk_disc_sniff found */
enum { VPK_DISC_RAW, VPK_DISC_ISO, VPK_DISC_CHD, VPK_DISC_PBP, VPK_DISC_ECM, VPK_DISC_UNKNOWN, VPK_DISC_UNREADABLE };

typedef struct {
  int sector;   /* bytes a sector in the file: 2048, 2352 or 2448 */
  int data_off; /* where a sector's 2048 data bytes start: 0 (.iso), 16 (mode 1) or 24 (mode 2 form 1) */
  int base;     /* LBA of the file's first sector (a raw image's pregap) */
} VpkDiscLayout;

typedef struct {
  VpkDiscLayout l;
  long long size; /* bytes */
  int nsect;      /* whole sectors in the file */
  int open;       /* nonzero while open (a zeroed VpkDisc is closed) */
  int fd;         /* the Vita: sceIo's */
  void *fp;       /* elsewhere: the FILE */
  int windows;
  struct {
    int lba, n;
    unsigned stamp;
    unsigned char *buf;
  } win[2];
  unsigned stamp;
  int bad_lba; /* vpk_disc_walk: the sector it could not read */
  /* optional I/O accounting: now() before each file-system read, io(bytes, t0) after it */
  unsigned long long (*now)(void);
  void (*io)(unsigned bytes, unsigned long long t0);
} VpkDisc;

/* What path is: VPK_DISC_RAW (with *l: sector size, first LBA from the first sync pattern whose next sector has one
 * too, data offset from sector 16's mode), VPK_DISC_ISO (2048-byte sectors, "CD001" at sector 16), a container to
 * convert (CHD, PBP, ECM), unknown, or unreadable.  Reads the first ~720 KiB. */
int vpk_disc_sniff(const char *path, VpkDiscLayout *l);
/* Opens path with layout l (d zeroed before its first open; a reopen keeps the windows' buffers) (from vpk_disc_sniff); windows 1: reads through two 32-sector windows, 0: straight to
 * the file.  0 on success, -1 if it cannot be opened, -2 if its size cannot be read. */
int vpk_disc_open(VpkDisc *d, const char *path, const VpkDiscLayout *l, int windows);
/* vpk_disc_sniff then vpk_disc_open for a raw image or an .iso, and the primary volume descriptor checked (type 1,
 * "CD001"); 0 on success, else -1 (d is left closed). */
int vpk_disc_open_any(VpkDisc *d, const char *path, int windows);
int vpk_disc_is_open(const VpkDisc *d);
void vpk_disc_close(VpkDisc *d);
/* n bytes from offset off of the sector at lba as the file holds it (a raw image: off 16 the subheader, 24 form 1's
 * data; an .iso: off 0 the data).  Returns n, or -1 past the end of the image or of the sector. */
int vpk_disc_read_raw(VpkDisc *d, int lba, int off, void *buf, int n);
/* count sectors' 2048 data bytes from lba; 0, or -1 if any is past the end */
int vpk_disc_read(VpkDisc *d, int lba, int count, void *buf);
/* A file by ISO9660 path ("\SYSTEM.CNF;1": '\' or '/' separators, a leading one optional, case and ";version"
 * ignored): its extent's LBA and size; 0 if found (a file, not a directory), else -1. */
int vpk_disc_find(VpkDisc *d, const char *path, unsigned *lba, unsigned *size);

typedef struct {
  const char *path; /* "S0/CRASH.EXE": '/' between parts, no ";version" */
  int lba, size;
  int xa_attr;      /* the record's CD-XA attributes (0x1000 form 2, 0x2000 interleaved, 0x4000 CD-DA), or 0 */
} VpkDiscEntry;
/* Every file in the directory tree (not the directories), depth first in directory order; each to fn, which
 * returns nonzero to stop the walk (then returned).  -1 when a directory cannot be read (its sector in
 * d->bad_lba), deeper than 8 or over 64 sectors. */
int vpk_disc_walk(VpkDisc *d, int (*fn)(void *user, const VpkDiscEntry *e), void *user);

/* ---- cue sheets and the image in a folder (from spyro-vita src/platform/common/cdimage.c @ bb62f38, D60) ---- */

/* a cue sheet track's mode (the sector size after the '/') */
enum {
  VPK_CUE_AUDIO, VPK_CUE_CDG, VPK_CUE_MODE1_2048, VPK_CUE_MODE1_2352, VPK_CUE_MODE2_2336, VPK_CUE_MODE2_2352,
  VPK_CUE_MODE2_2448, /* not in the cue format, but some dumps of 2448-byte sectors say it */
  VPK_CUE_OTHER       /* CDI/..., MODE2/2324 ...: a data track all the same */
};

#define VPK_CUE_TRACKS 99
typedef struct {
  int number; /* TRACK nn */
  int mode;   /* VPK_CUE_* */
  int sector; /* bytes a sector as the mode says (AUDIO 2352, CDG 2448), 0 if it says none */
  int file;   /* its FILE (vpk_cue_file) */
  int index0; /* INDEX 00, the pregap's start in its file, in sectors; -1 if none */
  int index1; /* INDEX 01, the track's start in its file, in sectors; -1 if none */
  int pregap; /* PREGAP: sectors not in the file */
} VpkCueTrack;

typedef struct {
  int ntracks, nfiles;
  VpkCueTrack track[VPK_CUE_TRACKS];
  int file_at[VPK_CUE_TRACKS]; /* each FILE's name in names */
  char names[4096];
} VpkCue;

/* A cue sheet's text (n bytes, no NUL needed): FILE (quoted, or unquoted up to its type word), TRACK, INDEX 00 / 01
 * and PREGAP, keywords in any case, CRLF or LF, a UTF-8 BOM; REM, CATALOG, TITLE ... skipped.  Multi-file sheets
 * (one FILE a track) and single-file ones alike.  Returns the number of tracks, or -1 when there is no FILE with a
 * TRACK under it, a TRACK before any FILE, or more than 99 tracks or 4 KiB of names. */
int vpk_cue_parse(const char *text, int n, VpkCue *c);
/* The data track: the first that isn't AUDIO or CDG (track 1 on a PS1 or PS2 disc), as an index into c->track;
 * -1 if there is none. */
int vpk_cue_data_track(const VpkCue *c);
/* FILE number file's name as the sheet spells it (folders and all) */
const char *vpk_cue_file(const VpkCue *c, int file);

/* why vpk_disc_find_image passed a candidate over (VpkDiscFound.status) */
enum {
  VPK_IMG_OK,
  VPK_IMG_NONE,           /* no candidate in the folder (or it can't be listed) */
  VPK_IMG_KIND,           /* the image's data is a kind the game didn't take (kind says which: an .iso, a CHD,
                             unknown, unreadable ...) */
  VPK_IMG_CUE_UNREADABLE, /* the .cue can't be opened */
  VPK_IMG_CUE_BAD,        /* no FILE with a TRACK in it (vpk_cue_parse) */
  VPK_IMG_CUE_TRACK,      /* no data track, or its mode isn't one the game takes (mode says which) */
  VPK_IMG_CUE_MISSING     /* the file the data track names isn't in the folder (image says which) */
};

typedef struct {
  int status;      /* VPK_IMG_* */
  char name[256];  /* the candidate: a .cue, or the image itself */
  char image[256]; /* the image's file name as the folder spells it (a .cue's FILE: as the sheet does if missing) */
  char path[512];  /* the folder and image: what to open */
  int cue;         /* 1 when name is a .cue */
  int track, mode; /* a .cue's data track: its number and VPK_CUE_* mode; else 0 and -1 */
  int kind;        /* vpk_disc_sniff's VPK_DISC_* for the image (VPK_DISC_UNREADABLE until sniffed) */
  VpkDiscLayout l; /* its layout when kind is VPK_DISC_RAW or VPK_DISC_ISO */
} VpkDiscFound;

typedef struct {
  unsigned kinds;     /* the kinds the game takes, as bits 1u << VPK_DISC_*; 0: raw images and .iso */
  unsigned cue_modes; /* the data track modes it takes from a .cue, as bits 1u << VPK_CUE_*; 0: any */
  void (*seen)(void *user, const VpkDiscFound *f); /* optional: each candidate tried, as it was found */
  void *user;
} VpkDiscSearch;

/* The first usable image in dir.  Candidates are the files named .cue, .bin, .img, .iso, .chd, .pbp or .ecm (the
 * extension in any case): every .cue first, then the others, each group in name order ignoring case.  A .cue is
 * followed to its data track's FILE, looked up in dir ignoring case and any folder in the name (the Vita's memory
 * card ignores case; Linux doesn't).  What an image is comes from its data (vpk_disc_sniff), never its name: a raw
 * image named .iso is raw.  A candidate the game can't take (s->kinds, s->cue_modes) is passed over for the next
 * one; a .cue it can't use leaves its image to be tried on its own.  0 with *out the image; -1 with *out the first
 * candidate's reason (VPK_IMG_NONE if there was none).  s may be NULL (the defaults).  Opens nothing: the game
 * calls vpk_disc_open(d, out->path, &out->l, ...) and says what each status means to the player. */
int vpk_disc_find_image(const char *dir, const VpkDiscSearch *s, VpkDiscFound *out);

/* The boot executable in a SYSTEM.CNF's text (n bytes) as a serial: the BOOT (PS1) or BOOT2 (PS2) line's file,
 * any device and folders dropped, '.' and '_' removed, a '-' after the first four ("cdrom:\SCUS_942.28;1" and
 * "cdrom0:\SLUS_203.70;1" give "SCUS-94228" and "SLUS-20370"), upper case.  0, or -1 with "" when there is no
 * BOOT line.  For telling a wrong disc or region apart from a bad dump; the wording is the game's. */
int vpk_disc_boot_serial(const char *cnf, int n, char *serial, int cap);

/* SHA-1 (FIPS 180-4) */
typedef struct {
  unsigned int h[5];
  unsigned long long len;
  unsigned char buf[64];
} VpkSha1;
void vpk_sha1_init(VpkSha1 *s);
void vpk_sha1_update(VpkSha1 *s, const void *data, unsigned n);
void vpk_sha1_hex(VpkSha1 *s, char out[41]);

#endif /* VPK_DISCIMAGE_H */
