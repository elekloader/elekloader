/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2 (Analog Four MKII, OS 1.56): the loader's file store and its mutex, shared
 * by the /mods route (route.c) and the boot linker (boot.c).
 *
 * The A4's +Drive has no file system: stock keeps 128 project slots and 4,096 sound slots at fixed
 * sectors and never addresses sectors 0x3000..0x7a000 (README, "The store"). The loader keeps its
 * own small store there (store.c): a directory in two copies, each file one contiguous run of
 * sectors. The calls below are the Rytm loader's ekFS calls, kept by name and meaning, so the
 * route's and the linker's logic stays the Rytm's. Paths are absolute ("/mods/a/x.elemod"). */
#ifndef DL_FS_H
#define DL_FS_H

/* the folder that holds the mods; a test build for a unit run uses a `~T` name (the test rules
 * allow only `~T` items) */
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

/* ---- the store's layout (store.c; sectors of 512 bytes) ---------------------------------- */
#define DS_FIRST 0x3000u               /* the first sector stock never addresses */
#define DS_END 0x7a000u                /* stock's power-off image starts here */
#define DS_DIR_SECTORS 320u            /* one directory copy: a header sector and the entries */
#define DS_COPY_A DS_FIRST
#define DS_COPY_B (DS_FIRST + DS_DIR_SECTORS)
#define DS_SCRATCH (DS_FIRST + 2 * DS_DIR_SECTORS)   /* one sector no file gets: a route entry with
                                                       * no file points here (route.c) */
#define DS_DATA (DS_SCRATCH + 8)       /* files from here, 4 KB aligned, to DS_END */
#define DS_PATH 128                    /* an entry's path below the store's root, NUL-padded */
#define DS_MAX 1024                    /* entries: files and folders */
#define DS_MAGIC 0x444c4134u           /* 'DLA4' */

/* one entry of the directory, as it is on the card (big-endian, the CPU's own order) */
struct ds_ent {
    char path[DS_PATH];                /* "mods/a/x.elemod", no leading "/" */
    unsigned sector;                   /* a file's first sector; 0 for a folder */
    unsigned size;                     /* a file's bytes */
    unsigned flags;                    /* DS_F_DIR */
    unsigned spare;
};
#define DS_F_DIR 1

/* the store's state: 0 not read yet (or no card), 1 read (empty when the card held no store) */
extern unsigned char ds_ready;
/* Read the directory (the newer valid copy). -> 0, or -1 when the card isn't there, too small, or
 * SD init failed. A card whose sectors hold no valid copy reads as an empty store, written fresh at
 * the first change. */
int ds_mount(void);
/* FORMAT +DRIVE (ev_drive_format): the store emptied, both copies written. */
void ds_format(void);
/* the directory's generation, bumped by every change (the route's listings, .status) */
extern unsigned ds_gen;
/* the free room in bytes: the largest run of free sectors, and all of them */
void ds_room(unsigned *largest, unsigned *total);

/* Route uploads straight to sectors (route.c): ds_reserve takes a run for `size` bytes and gives
 * its first sector (0 when none is free); ds_commit makes path that run, replacing any file there,
 * when the reservation is still the live one (token) and size fits it. A newer ds_reserve drops an
 * older one; a reservation is never on the card, so a dropped one costs nothing. */
unsigned ds_reserve(unsigned size, unsigned *token);
int ds_commit(const char *path, unsigned token, unsigned size);
/* a file's first sector and size, -> 0, or -1 when there is no such file */
int ds_file(const char *path, unsigned *sector, unsigned *size);

/* ---- the Rytm loader's calls (fs.h on drive-loader-ar2) ----------------------------------- */
/* A file open for reading streams from its sectors; one open for writing takes the largest free
 * run, streams into it and becomes the file only at dl_fs_close (an old file of that name is
 * replaced then, so a failed write leaves it as it was). One writer at a time. */
typedef struct dl_file {
    unsigned sector;                   /* first sector */
    unsigned size;                     /* read: the file's size; write: bytes written */
    unsigned pos;                      /* read: the next byte */
    unsigned cap;                      /* write: the run's bytes */
    unsigned token;                    /* write: the reservation */
    unsigned char open;                /* 1 read, 2 write */
    unsigned char bad;                 /* write: an I/O error, so the close drops it */
    char path[DS_PATH];
} dl_file;

/* 0 or a negative error. Every opened file is closed with dl_fs_close, also after a failed open. */
int dl_fs_open(dl_file *f, const char *path, int write);
int dl_fs_read(dl_file *f, void *buf, unsigned n);        /* bytes read, or < 0 */
int dl_fs_write(dl_file *f, const void *buf, unsigned n); /* 0, or < 0 */
int dl_fs_size(dl_file *f);              /* the file's size; read mode */
void dl_fs_close(dl_file *f);
int dl_fs_delete(const char *path);      /* 0, or < 0 (also when the file isn't there) */
int dl_fs_rename(const char *from, const char *to);   /* 0, or < 0: `to` must not be there; a folder
                                                       * moves with everything below it */
int dl_fs_file_size(const char *path);   /* the data size of a file, or < 0 */
int dl_fs_rmdir(const char *path);       /* 0, or < 0: an empty folder only */
int dl_fs_mkdir(const char *path);       /* 0, or < 0: one folder, its parent there (DL_DIR is) */

/* The MODS page's stepped read of a file below DL_DIR (struct dl_reader) is in skip.h: it stages
 * as the boot does. */

/* one entry of a folder below DL_DIR (route.c) */
struct dl_ent {
    char name[DL_SEG_MAX + 1];
    unsigned char dir;                   /* a folder */
};
/* the entries of DL_DIR "/" rel (rel "" for DL_DIR itself), folders and files, without a name past
 * DL_SEG_MAX, up to max; out == 0 gives the folder's entry count. -> how many */
int dl_list_dir(const char *rel, struct dl_ent *out, int max);

/* the loader's mutex: the route's commits and its shared state. Take it before store calls (which
 * take the store's own), never inside them. Not needed at boot (no other task yet). */
void dl_lock(void);
void dl_unlock(void);

/* A small RAM buffer for the route's two uploads that write no file (a .mv's names and a .dir's
 * empty body), and the MODS page's work later. Files never pass through it: uploads go straight to
 * sectors, reads straight from them. */
extern unsigned char drive_staging[];
#define DL_STAGING_SIZE 0x400

/* the live stage (dl.h's linker and the MODS page use it): here the floor above, until the boot
 * gives it the area's free tail (dl_stage_tail) */
extern unsigned char *volatile dl_stage_at;
extern volatile unsigned dl_stage_len;
void dl_stage_tail(unsigned lo, unsigned hi);

/* who holds the staging buffer outside a transfer (the MODS page, later): 1 taken, 0 not */
int dl_staging_take(void);
void dl_staging_give(void);
extern volatile unsigned dl_staging_epoch;
extern volatile int dl_staging_owner;
/* the Rytm's open-read count and reader polls (the MODS page keeps off the stage during a read):
 * here a read streams from its sectors, never the stage, so dl_reads stays 0, the polls do nothing
 * and dl_staging_take_idle is dl_staging_take */
extern volatile int dl_reads;
int dl_staging_take_idle(unsigned now, unsigned stall);
void dl_reader_add(unsigned char *r, unsigned now);
void dl_reader_remove(unsigned char *r);
void dl_readers_poll(unsigned now);

#endif
