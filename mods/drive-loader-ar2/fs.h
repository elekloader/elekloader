/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader (Analog Rytm MKII, OS 1.74): the loader's ekFS calls, its
 * mutex and its staging buffer, shared by the /mods route (route.c) and the
 * boot linker. Every address is OS 1.74's (README, "Firmware calls"). */
#ifndef DL_FS_H
#define DL_FS_H

/* the folder on the +Drive's ekFS that holds the mods; a test build for a
 * unit run uses a `~T` name (the test rules allow only `~T` items) */
#ifdef DL_TEST_DIR
#define DL_DIR "/~T MODS"
#else
#define DL_DIR "/mods"
#endif

#define DL_NAME_MAX 127                /* a path below DL_DIR ("a/b/x.elemod"), without the NUL */
#define DL_SEG_MAX 63                  /* one folder or file name in it, without the NUL */
#define DL_DEPTH 3                     /* folders below DL_DIR, at most (decided 2026-11-08) */
#define DL_PATH_MAX 144                /* DL_DIR "/" path ".tmp", with the NUL */
#define DL_MKDIR ".dir"                /* an upload to FOLDER "/" DL_MKDIR makes FOLDER (names.inc) */
#define DL_MOVE ".mv"                  /* an upload of "FROM\nTO" to DL_DIR "/" DL_MOVE moves or renames (names.inc) */
#define DL_STORE ".store"              /* +Drive storage's places (docs/DRIVE.md): the route serves it read only */

/* stock's ekFS file object (0x2c bytes, ctor 0x4007dcfa), kept opaque, then the
 * loader's own: the files carry a 16-byte header (README, "Files on the +Drive") */
#define DL_FILE_BYTES 0x2c
#define DL_HEADER 16
typedef struct dl_file {
    unsigned char raw[DL_FILE_BYTES];
} __attribute__((aligned(4))) dl_file;

/* 0 or a negative error. write truncates (stock deletes the old file), makes
 * the parent folders and writes the header first; read skips the header (a
 * file without one doesn't open). Every opened file
 * is closed with dl_fs_close, also after a failed open. */
int dl_fs_open(dl_file *f, const char *path, int write);
int dl_fs_read(dl_file *f, void *buf, unsigned n);        /* bytes read, or < 0 */
int dl_fs_write(dl_file *f, const void *buf, unsigned n); /* 0, or < 0 */
int dl_fs_size(dl_file *f);              /* the file's size; read mode, before any read */
void dl_fs_close(dl_file *f);
int dl_fs_delete(const char *path);      /* 0, or < 0 (also when the file isn't there) */
int dl_fs_rename(const char *from, const char *to);   /* 0, or < 0; delete `to` first */
int dl_fs_file_size(const char *path);   /* the data size of a file, or < 0 */
int dl_fs_rmdir(const char *path);       /* 0, or < 0: an empty folder only, as SAMPLES' DELETE DIR */
int dl_fs_mkdir(const char *path);       /* 0, or < 0: one folder, its parent there (route.c makes each level) */

/* A file below DL_DIR read a stretch (32 KB) a call, its CRC-32 taken as it comes, so the
 * MODS page reads one stretch a tick and no tick blocks the UI task in a whole file's read
 * (page.c, read_step: the UI event queue it lets grow refuses keys). The caller holds
 * the staging buffer from the open to the end, and dl_lock during each call. */
struct dl_reader {
    dl_file f;
    unsigned got;                        /* bytes read so far */
    unsigned crc;                        /* the CRC-32 register; ~crc is the file's at the end */
    unsigned char open;
    unsigned char over;                  /* past max: the rest is read for got and crc alone */
};
int dl_read_open(struct dl_reader *rd, const char *name);   /* 0, or -1 missing (closed) */
/* -> 0 more to read, 1 at the end (rd->got bytes in buf), -2 larger than max (read to its end
 * all the same: rd->got is its size and rd->crc its CRC, its bytes not kept), -3 a read
 * error; closed at any answer but 0 */
int dl_read_more(struct dl_reader *rd, unsigned char *buf, unsigned max);
void dl_read_close(struct dl_reader *rd);                   /* a read given up */

/* one entry of a folder below DL_DIR (route.c, dl_list_dir) */
struct dl_ent {
    char name[DL_SEG_MAX + 1];
    unsigned char dir;                   /* a folder */
};
/* the entries of DL_DIR "/" rel (rel "" for DL_DIR itself), folders and files, without "." and
 * ".." and without a name past DL_SEG_MAX, up to max; out == 0 gives the folder's entry count, an
 * upper bound on them. The caller holds dl_lock. -> how many */
int dl_list_dir(const char *rel, struct dl_ent *out, int max);

/* the loader's mutex: the staging buffer, the route's commits and the MODS
 * page's view of the files. Take it before ekFS calls (which take their own),
 * never inside them. Not needed at boot (no other task yet). */
void dl_lock(void);
void dl_unlock(void);

/* the staging buffer's floor (64 KB, .bss). The boot links with the stage at the top of the
 * +Drive area (dl.h, dl_stage_top), and after it the route's transfers and the MODS page's
 * check use the live stage: the free tail of the area above the boot's layout when that is
 * larger than the floor, else this buffer (STAGE.md). */
extern unsigned char drive_staging[];
#define DL_STAGING_SIZE 0x10000

/* The live stage (route.c owns it): its start and size. Every read, upload and page check
 * goes through these two, never drive_staging directly. dl_stage_tail sets them once, at the
 * end of the boot, to the area's free tail [lo, hi) when it is larger than the floor (lo
 * rounded up to 16); otherwise they stay the floor. */
extern unsigned char *volatile dl_stage_at;
extern volatile unsigned dl_stage_len;
void dl_stage_tail(unsigned lo, unsigned hi);

/* who holds the staging buffer outside a transfer: a loader part (the MODS
 * page, a later linker run) takes it with dl_staging_take, and the route then
 * answers every /mods transfer "busy" until dl_staging_give. 1 taken, 0 not. */
int dl_staging_take(void);
void dl_staging_give(void);
/* bumped by each dl_staging_take: an upload whose header arrived under another epoch had
 * its staged bytes overwritten by a loader part, and is dropped at its commit */
extern volatile unsigned dl_staging_epoch;
/* live copies of open reads' fill closures: nonzero while a read of a file is open */
extern volatile int dl_reads;
/* dl_staging_take, also refused while a read is open, unless its reader hasn't moved for
 * `stall` ticks (the MODS page; `now` in its ticks) */
int dl_staging_take_idle(unsigned now, unsigned stall);
/* the open reads' readers (route.c, under dl_lock), and the page's poll of their progress */
void dl_reader_add(unsigned char *r, unsigned now);
void dl_reader_remove(unsigned char *r);
void dl_readers_poll(unsigned now);

#endif
