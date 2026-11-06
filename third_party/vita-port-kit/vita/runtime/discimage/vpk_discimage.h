/* The user's own disc image, read in place by sector: a 2048-byte .iso, or a raw image of 2352-byte sectors (2448
 * with subchannel data; a .bin or a CloneCD .img), mode 1 or mode 2 form 1, with any pregap.  CD sectors are the
 * same on PS1 and PS2, so the layer serves both.  64-bit offsets everywhere (a PS2 DVD image is over 2 GiB): the
 * kernel's sceIo on the Vita, fopen / fseeko with 64-bit off_t elsewhere.  Nothing is written.
 *
 * vpk_disc_sniff says what a file is (raw, .iso, or a format to convert first); vpk_disc_open opens it.  Reads go
 * straight to the file, or (windows = 1) through two windows of 32 sectors, one file-system read each, so a
 * game's sequential reads cost one call per 32 sectors.  vpk_disc_find looks a file up by ISO9660 path,
 * vpk_disc_walk lists the whole directory tree (with each record's CD-XA attributes).  SHA-1 for disc checks.
 *
 * From kh1-decomp host/port/cd_image.c @ 85befa4a (2048 / 2352 images, 64-bit reads, the path lookup) and
 * spyro-vita src/platform/common/cdimage.c @ 987363e (sniffing, sync-pattern layout and pregap, windows, the
 * directory walk, SHA-1; decisions D58, D60). */
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
