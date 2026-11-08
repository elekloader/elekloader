/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Drive Loader, boot side: links the +Drive mods chosen in /mods/active into
 * the +Drive mods' area at boot (docs/FORMAT.md, "+Drive mods").
 *
 * The linker itself (json.c, dlink.c) touches memory only through DL_MEM and
 * files only through dl_read_file, so tests/drive_link_host.c runs the same
 * code on the computer against the host twin. boot.c is the unit's side. */
#ifndef DL_H
#define DL_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

#define DL_MAX_MODS 256                      /* lines of /mods/active a boot links */
#define DL_ID_MAX 28                     /* a mod id, NUL included, as the status keeps it */
#define DL_LINE_NAME_MAX 128                  /* a path below /mods in /mods/active (folders
                                             * included: "a/b/c/x.elemod"), NUL included */
#define DL_VER_MAX 12                        /* a version as dl_check reports it, NUL included */

/* ---- status records: /mods/.status and the MODS page (32 bytes per /mods/active line) -- */
struct dl_status {
    u8 state;                            /* DL_ST_* */
    u8 code;                             /* DL_R_* when refused */
    u16 arg;                             /* a number: bytes over in KB, a table's index... */
    char id[DL_ID_MAX];                  /* the mod's id, or its file name before it is known */
};
enum { DL_ST_NONE, DL_ST_LOADED, DL_ST_REFUSED, DL_ST_DROPPED };

/* Refusal codes. The numbers are shared with the host twin and the MODS page
 * (docs/FORMAT.md, "+Drive refusal codes"). */
enum {
    DL_R_OK = 0,
    DL_R_MISSING = 1,                    /* the file named in /mods/active is not there */
    DL_R_CHANGED = 2,                    /* its CRC-32 is not the one /mods/active has */
    DL_R_TOO_BIG = 3,                    /* larger than the staging buffer */
    DL_R_JSON = 4,                       /* not a JSON object the reader can read */
    DL_R_FORMAT = 5,                     /* not format 2 */
    DL_R_TARGET = 6,                     /* made for another device or OS */
    DL_R_FEATURE = 7,                    /* a field +Drive mods can't use (detail: its name) */
    DL_R_DUPLICATE = 8,                  /* the id of a flashed mod, or of another in the set */
    DL_R_CORE = 9,                       /* needs a newer core (detail: the version) */
    DL_R_REQUIRES = 10,                  /* a required mod is missing (detail: its id) */
    DL_R_CONFLICTS = 11,                 /* conflicts with a mod present (detail: its id) */
    DL_R_EXPORT = 12,                    /* exports a name already exported (detail: it) */
    DL_R_CLAIM = 13,                     /* claims a resource already claimed (detail: it) */
    DL_R_IMPORT = 14,                    /* imports a name nothing defines (detail: it) */
    DL_R_TABLE = 15,                     /* adds to a table +Drive mods can't use (detail) */
    DL_R_FULL = 16,                      /* a table has no spare entry left (detail: it) */
    DL_R_ROOM = 17,                      /* the area is full (arg: KB over) */
    DL_R_RANGE = 18,                     /* a pc16 reference out of range */
    DL_R_LIMIT = 19,                     /* past the loader's own limits (symbols, entries) */
    DL_R_COUNT = 20,                     /* more than DL_MAX_MODS lines in /mods/active */
};

/* Why nothing was linked (dl_skip_reason). */
enum { DL_SKIP_NONE, DL_SKIP_KEY, DL_SKIP_ARMED, DL_SKIP_DRIVE, DL_SKIP_ABI, DL_SKIP_UNSTABLE, DL_SKIP_COUNT,
       DL_SKIP_MEMORY };

/* dl_link's records, one per line of the set it was given (dl_status_n), taken from the heap
 * by dl_link and kept until the next dl_link (0 before the first). */
extern struct dl_status *dl_status;
extern char (*dl_detail)[32];            /* the refusal's name argument, NUL-terminated */
extern u32 dl_status_n;
extern u8 dl_skip_reason;

/* ---- boot time (/mods/.status "boot:") ------------------------------------------------ */
/* DTIM0's count (0xfc07000c) runs free from the 132 MHz bus clock: the bootstrap sets its
 * DTMR to 0x0b and stock's own 64-bit clock (0x401006bc) reads it. 32 bits wrap after 32 s,
 * far past a boot, so one unsigned difference is enough. */
#define DL_BUS_PER_US 132u
static inline u32 dl_bus_us(u32 from, u32 to) { return (to - from) / DL_BUS_PER_US; }
enum { DL_T_ALL, DL_T_CHECKS, DL_T_LINK, DL_T_INIT };   /* dl_boot_us[]: the whole, then its parts */
extern u32 dl_boot_us[4];

/* ---- memory ------------------------------------------------------------------------ */
#ifdef DL_HOST
u8 *dl_host_mem(u32 addr);
#define DL_MEM(a) dl_host_mem((u32)(a))
#else
#define DL_MEM(a) ((u8 *)(a))
#endif

static inline u32 dl_rd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static inline u16 dl_rd16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static inline void dl_wr32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static inline void dl_wr16(u8 *p, u32 v) { p[0] = v >> 8; p[1] = v; }

/* Heap memory (the unit: stock's operator new and delete; the host: calloc and free).
 * dl_alloc zeroes what it gives, and gives 0 when there is none; dl_free(0) does nothing. */
void *dl_alloc(u32 n);
void dl_free(void *p);

/* Long work (a file's CRC, the scans of its long strings) calls dl_breathe with each 32 KB it
 * has done: on the unit it sleeps 1 ms, so lower tasks (USB, the Data router, analoghealth's
 * RPC) run while the UI task checks the MODS page's files (found 2026-11-08: one 482 KB file's
 * check held them for seconds in the emulator); on the host it does nothing. */
#define DL_BREATHE 0x8000u
void dl_breathe(void);

void dl_memcpy(void *d, const void *s, u32 n);
void dl_memset(void *d, int c, u32 n);
int dl_strcmp(const char *a, const char *b);
u32 dl_strlen(const char *s);
u32 dl_crc32(const u8 *p, u32 n);
u32 dl_crc32_add(u32 c, const u8 *p, u32 n);   /* the register carried on: ~0 to start, ~ at the end */
u32 dl_hash(const char *s, u32 n);       /* FNV-1a, 32 bits */

/* ---- what the linker asks of its host ------------------------------------------------ */
/* Read /mods/<name> into buf (at most max bytes). -> its length, -1 missing, -2 larger
 * than max, -3 a read error. */
s32 dl_read_file(const char *name, u8 *buf, u32 max);

/* ---- the linker (dlink.c) ------------------------------------------------------------ */
struct dl_choice {
    char name[DL_LINE_NAME_MAX];
    u32 crc;
};
/* Link the chosen files (in /mods/active order, n <= DL_MAX_MODS) against the build
 * descriptor at `desc`, using `buf` (max bytes) to hold one file at a time. Fills the status
 * records (n of them, from the heap). Writes the area and the tables. -> the number of mods
 * loaded. Its working memory (the mods, the pools, the symbol buckets) comes from the heap
 * for the call and goes back after it: DL_SKIP_MEMORY when there is none. */
int dl_link(u32 desc, const struct dl_choice *choice, int n, u8 *buf, u32 max);

/* After dl_link: each loaded mod's drive_init (0 if it has none), in id order, and the
 * range the linker wrote, for the cache flush. */
extern u32 *dl_inits;                    /* dl_status_n of them, from the heap */
extern u32 dl_inits_n;
extern u32 dl_used_lo, dl_used_hi;

/* The MODS page's check of a set (design steps 4 to 6: pass 1, the checks, the layout),
 * which writes nothing but its own records: st and det (n each, n <= DL_MAX_MODS) as
 * dl_status and dl_detail, with DL_ST_LOADED for a mod that would load, and each file's
 * version (ver may be 0). Pass 2's own refusals aren't seen. -> how many would load, -1 when
 * the descriptor isn't one the loader reads or the heap has no room for the check. */
int dl_check(u32 desc, const struct dl_choice *choice, int n, u8 *buf, u32 max,
             struct dl_status *st, char (*det)[32], char (*ver)[DL_VER_MAX]);

/* dl_check in steps, so a caller can read one file at a time and let other tasks in between
 * (the MODS page's save: one file a tick). dl_check_start takes the memory and the flashed
 * build's names (-> 0, or -1 as dl_check); dl_check_step reads line i's file (lines in order,
 * each once, the buffer and descriptor as given to start) and gives how many lines are left;
 * dl_check_finish gives what dl_check gives; dl_check_cancel gives the memory back. The steps
 * use the same link state as dl_check and dl_link: nothing else may check or link between
 * start and finish. */
int dl_check_start(u32 desc, int n, u8 *buf, u32 max, struct dl_status *st, char (*det)[32],
                   char (*ver)[DL_VER_MAX]);
int dl_check_step(const struct dl_choice *ch);
/* dl_check_step of line i's file the caller has already read into the buffer (len bytes, or
 * -1 missing, -2 larger than the buffer, -3 a read error), crc its CRC-32: the page reads a file
 * a stretch a tick, so no one tick holds the UI task for the whole file */
int dl_check_step_buf(const struct dl_choice *ch, s32 len, u32 crc);
int dl_check_finish(void);
void dl_check_cancel(void);
/* dl_check of one file the caller has already read into buf (len bytes; its CRC is taken as
 * the caller's): pass 1, checks and layout without a second read. */
int dl_check_buf(u32 desc, const struct dl_choice *ch, s32 len, u8 *buf, u32 max,
                 struct dl_status *st, char (*det)[32], char (*ver)[DL_VER_MAX]);

/* The set the boot linked, in /mods/active order: dl_status[i] is dl_boot_choice[i]'s
 * (dl_status_n of them, from the heap; 0 when the boot linked nothing). */
extern struct dl_choice *dl_boot_choice;

/* /mods/active -> choices (out == 0: only count them). -> the count, or -1 past DL_MAX_MODS
 * lines. */
int dl_parse_active(const char *text, u32 len, struct dl_choice *out);

/* The linker's pools, sized to the +Drive area (docs/FORMAT.md, "+Drive mods"): per started
 * MB of the area (at least 1), each capped where its 16-bit index ends. The host twin
 * (elekloader/drive.py, pools()) has the same table. */
#define DL_POOL_ARENA 12288                  /* name bytes: ids, requires, imports... */
#define DL_POOL_LISTS 1024                   /* names in lists */
#define DL_POOL_SYMS 1024                    /* +Drive exports (past the flashed build's names) */
#define DL_POOL_LOCALS 1024                  /* one file's own symbols, in pass 2 */
#define DL_POOL_CONTRIBS 256                 /* +Drive table contributions */
#define DL_POOL_EDATA 2048                   /* their bytes */
#define DL_POOL_ERELOCS 256                  /* their relocations */
#define DL_POOL_CAP 0xFFF0                   /* every pool's most */

#endif
