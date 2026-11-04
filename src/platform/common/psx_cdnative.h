/* Native disc reads (src/platform/common/cdnative.c, decisions D93,
 * docs/native-plan.md stage 6), behind cd_native=1 (Vita config.txt) or
 * --cd-native (linux32). libcd.c keeps the game's CD model; what changes is
 * how the bytes get there: a CdRead of an LBA range is a read of a file at an
 * offset (psx_cd_locate) on a worker thread, and the XA stream is read
 * ahead from where it starts, keeping the sectors of the channel it plays. */
#ifndef PSX_CDNATIVE_H
#define PSX_CDNATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

struct PsxXaPcm;

extern int g_psx_cd_native; /* 1 once psx_cdn_start has started the worker */

/* The platform's switch on: starts the worker thread and sets
 * g_psx_cd_native. 0, or -1 (the sector reads as before). */
int psx_cdn_start(void);
/* The worker's thread id (the Vita's SceUID), 0 if none. */
int psx_cdn_thread(void);
/* 1 while the worker reads or decodes, 0 while it waits (watchdog lines) */
extern volatile int g_psx_cdn_busy;

/* ---- libcd.c: CdRead ---- */
/* The read of count 2048-byte sectors from lba into dst starts on the worker. */
void psx_cdn_read_post(int lba, int count, void *dst);
/* That read is complete: waits for it, or does it now if none was posted
 * for these (a save state loaded with a read in flight). */
void psx_cdn_read_finish(int lba, int count, void *dst);
/* Waits for a posted read, which then counts as not posted (CdReadBreak,
 * CdInit, save states). */
void psx_cdn_read_drain(void);

/* ---- the XA stream ---- */
/* libcd.c: a ReadS/ReadN stream starts at lba (and after a state load). */
void psx_cdn_stream_start(int lba);
/* psx_xa_accept from the stream's read-ahead: 1 if the stream plays the
 * sector at lba, which is then in raw. With pcm, also its PCM when the worker
 * decoded it (mixer=3, D84): held until psx_cdn_xa_release. */
int psx_cdn_xa_accept(int lba, unsigned char *raw, const struct PsxXaPcm **pcm);
void psx_cdn_xa_release(const struct PsxXaPcm *pcm);

/* Counts since the start, read across threads (approximate while it runs). */
typedef struct {
  /* CdRead on the worker: file-system calls, KB, time in them (us); the
   * worker's whole time on the reads (us), and the reads that waited for it
   * to finish a stream chunk first */
  unsigned reads, kbytes;
  unsigned long long us, job_us;
  unsigned behind_chunk;
  /* the stream: chunks the worker filled, the file-system calls, KB and us
   * that took; sectors answered from them, read on the game thread instead;
   * asks that found the slot busy (being read) or pinned; sectors decoded to
   * PCM and the us it took */
  unsigned chunks, chunk_reads, chunk_kb, hits, misses, busy, pinned, decoded;
  unsigned long long chunk_us, decode_us;
} PsxCdnStats;
void psx_cdn_stats(PsxCdnStats *out);

#ifdef __cplusplus
}
#endif

#endif
