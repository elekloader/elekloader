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

#define DL_NAME_MAX 63                 /* a file name in DL_DIR, without the NUL */
#define DL_PATH_MAX 80                 /* DL_DIR "/" name ".tmp", with the NUL */

/* stock's ekFS file object (0x2c bytes, ctor 0x4007dcfa), kept opaque */
#define DL_FILE_BYTES 0x2c
typedef struct dl_file {
    unsigned char raw[DL_FILE_BYTES];
} __attribute__((aligned(4))) dl_file;

/* 0 or a negative error. write truncates (stock deletes the old file) and
 * makes the parent folders. Every opened file is closed with dl_fs_close,
 * also after a failed open. */
int dl_fs_open(dl_file *f, const char *path, int write);
int dl_fs_read(dl_file *f, void *buf, unsigned n);        /* bytes read, or < 0 */
int dl_fs_write(dl_file *f, const void *buf, unsigned n); /* 0, or < 0 */
int dl_fs_size(dl_file *f);              /* the file's size; read mode, before any read */
void dl_fs_close(dl_file *f);
int dl_fs_delete(const char *path);      /* 0, or < 0 (also when the file isn't there) */
int dl_fs_rename(const char *from, const char *to);   /* 0, or < 0; delete `to` first */
int dl_fs_file_size(const char *path);   /* the data size of a file, or < 0 */

/* the loader's mutex: the staging buffer, the route's commits and the MODS
 * page's view of the files. Take it before ekFS calls (which take their own),
 * never inside them. Not needed at boot (no other task yet). */
void dl_lock(void);
void dl_unlock(void);

/* the staging buffer (512 KB, .bss): the route's transfers and the boot
 * linker share it */
extern unsigned char drive_staging[];
#define DL_STAGING_SIZE 0x80000

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
