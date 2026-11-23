/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Drive Loader, boot side (Analog Four MKII, OS 1.56): dl_boot, which core calls in ev_boot_drive
 * from its site 0x401061e8 (core-a4mk2 0.9), the skip key, the crash guard, /mods/active, and the
 * tick that clears the crash guard; and dl_format, in core's ev_drive_format. The linker is
 * dlink.c, the store store.c.
 *
 * At the site main() (0x40105fb6) runs as an RTOS task with interrupts on: the static
 * constructors and the heap are up, SD init is done (its result in 0x41865e98) and stock has read
 * the power-off image with the same raw sector read the store uses. The boot project load
 * (0x400f1b18, the call core's site wraps), the audio interrupt (0x4010623a) and the UI
 * (0x40106302) come after it, so no table the linker rewrites can be read while it does
 * (trace-phase1-boot-load.md). Factory boots skip the site. */
#include "dl.h"
#include "fs.h"
#include "dstore.h"
#include "skip.h"

/* The panel link (trace-phase0-sites.md section 6; emu-checks noskip, checked 2026-11-14) */
typedef void (*v_fn)(void);
typedef void (*byte_fn)(u32 b);
typedef void (*sleep_fn)(u32 us);
typedef u32 (*held_fn)(u32 code);
#define PANEL_BYTE ((byte_fn)0x4000274a)             /* main's one-byte panel send */
#define SLEEP_US ((sleep_fn)0x40078064)              /* blocks the task on the DMA timer */
#define KEY_HELD ((held_fn)0x400647a2)               /* reads main's key bitmap 0x417bf8b8 */
#define KEY_NO 0x45

#define CRASH_TICKS 200                              /* ev_tick is about 20 Hz: 10 s */
#define CRASH_TICKS_SITES 1200                       /* 60 s, after a boot that applied sites */
#define DTCN0 (*(volatile u32 *)0xfc07000c)          /* DMA timer 0's count (dl.h, "boot time") */

extern char core_desc[];
void dl_flush(void);

/* stock's heap (core-a4mk2 rows.c): operator new (core's core_new at 0x400653b8) and delete.
 * main() has run the static constructors, which use it, before the site. */
#define OP_NEW ((void *(*)(u32))0x400653b8)
#define OP_DELETE ((void (*)(void *))0x400653c4)

#ifndef DL_HOST_HEAP                    /* tests/drive_a4_payload_host.c: its own heap */
void *dl_alloc(u32 n)
{
    void *p = OP_NEW(n);
    if (p)
        dl_memset(p, 0, n);
    return p;
}

void dl_free(void *p)
{
    if (p)
        OP_DELETE(p);
}
#endif

/* dl.h: 1 ms for the tasks below the caller's, each 32 KB of a long check. Not during the
 * boot's link: main() runs it before the UI task and the audio start, the T178 boot linked
 * 6.3 MB without them, and the sleeps (about 2 ms each, 8 for each 32 KB of a file over both passes) made up
 * about 4 s of an 8.7 MB link (T178 against T192). */
static u8 booting;

void dl_breathe(void)
{
    if (!booting)
        SLEEP_US(1000);
}

struct dl_choice *dl_boot_choice;        /* the boot's set: dl_status[i] is line i's */
static u32 armed_ticks;
static u8 armed;

static void path_of(char *out, const char *name)
{
    const char *d = DL_DIR;
    u32 k = 0;
    while (*d)
        out[k++] = *d++;
    out[k++] = '/';
    while (*name && k < DL_PATH_MAX - 1)
        out[k++] = *name++;
    out[k] = 0;
}

/* path (DL_PATH_MAX + 4 bytes) with DL_NEW after it */
static void add_new(char *path)
{
    u32 k = dl_strlen(path), i;
    for (i = 0; i < sizeof DL_NEW; i++)
        path[k + i] = DL_NEW[i];
}

/* The updates an upload over a file in use left (route.c, <name>.new), before the boot stages
 * anything: each /mods/active line's <name>.new takes <name>'s place in one directory write
 * (store.c ds_swap). The line takes the new file's CRC, since it still names the old file's (the
 * Rytm's emulator gate, 2026-11-22: without it the boot dropped the update, code 2), and
 * /mods/active is written with the new CRCs before any swap: a power cut then leaves either the
 * old pair with active already naming the new CRC (the next boot swaps it and active stands) or
 * the swap done; never the new file under an active naming the old CRC. A swap that fails keeps
 * both for the next boot, its line takes back the old CRC and active is written again; an active
 * that can't be written swaps nothing. dl_updates[1] counts each. */
u32 dl_updates[2];

/* the staged CRC of a file below DL_DIR (its payload's hex skipped), as /mods/active names it:
 * read through the live stage, which nothing else uses before the link. -> 0, or -1 */
static int staged_crc(const char *name, u32 *crc)
{
    struct dl_reader rd;
    int r;
    if (dl_read_open(&rd, name) < 0)
        return -1;
    while (!(r = dl_read_more(&rd, dl_stage_at, dl_stage_len)))
        ;
    if (r != 1 && r != -2)               /* -2: staged past the stage, the CRC is the whole file's */
        return -1;
    *crc = ~rd.st.crc;
    return 0;
}

/* /mods/active written from the set (its lines that name no CRC or are comments load nothing, so
 * they go), straight over its name: the store names the new file only at the close, so a failed
 * write leaves the old one (fs.h). -> 0, or -1 */
static int write_active(const struct dl_choice *ch, int n)
{
    static const char head[] = "# updates applied at boot\n";
    char path[DL_PATH_MAX], *text, *at;
    dl_file f;
    int i, r, b;
    u32 k;
    text = dl_alloc(sizeof head + (u32)n * (DL_LINE_NAME_MAX + 10));
    if (!text)
        return -1;
    dl_memcpy(text, head, sizeof head - 1);
    at = text + sizeof head - 1;
    for (i = 0; i < n; i++) {
        k = dl_strlen(ch[i].name);
        dl_memcpy(at, ch[i].name, k);
        at += k;
        *at++ = ' ';
        for (b = 28; b >= 0; b -= 4)
            *at++ = "0123456789abcdef"[(ch[i].crc >> b) & 15];
        *at++ = '\n';
    }
    k = (u32)(at - text);
    path_of(path, "active");
    dl_memset(&f, 0, sizeof f);
    r = dl_fs_open(&f, path, 1);
    if (!r)
        r = dl_fs_write(&f, text, k);
    dl_fs_close(&f);
    dl_free(text);
    if (!r && dl_fs_file_size(path) != (int)k)
        r = -1;
    return r ? -1 : 0;
}

void dl_apply_updates(struct dl_choice *ch, int n)
{
    char path[DL_PATH_MAX + 4], nw[DL_PATH_MAX + 4];
    u32 *old, k;
    int i, changed = 0, back = 0;
    dl_updates[0] = dl_updates[1] = 0;
    old = n > 0 ? dl_alloc((u32)n * 8) : 0;  /* each line's CRC before, and 1 for a line with an update */
    if (!old)
        return;
    for (i = 0; i < n; i++) {             /* the new files' CRCs: dl_read_open reads <name>.new */
        path_of(path, ch[i].name);
        dl_memcpy(nw, path, sizeof path);
        add_new(nw);
        old[2 * i] = ch[i].crc;
        if (dl_fs_file_size(nw) < 0)
            continue;
        old[2 * i + 1] = 1;
        if (staged_crc(ch[i].name, &k) == 0 && k != ch[i].crc) {
            ch[i].crc = k;
            changed = 1;
        }
    }
    if (changed && write_active(ch, n)) { /* the old files stay with the active that names them */
        for (i = 0; i < n; i++)
            ch[i].crc = old[2 * i];
        dl_updates[1]++;
        dl_free(old);
        return;
    }
    for (i = 0; i < n; i++) {
        if (!old[2 * i + 1])
            continue;
        path_of(path, ch[i].name);
        dl_memcpy(nw, path, sizeof path);
        add_new(nw);
        if (ds_swap(path, nw) == 0) {
            dl_updates[0]++;
            continue;
        }
        dl_updates[1]++;
        back |= ch[i].crc != old[2 * i];
        ch[i].crc = old[2 * i];           /* the old file stays: its CRC */
    }
    if (back && write_active(ch, n))
        dl_updates[1]++;
    dl_free(old);
}

/* dl_stage_file's reads (skip.h): from the file at `at`, 32 KB at most, 1 ms for the tasks below
 * after each outside the boot */
static s32 read_at(void *ctx, u32 at, u8 *dst, u32 n)
{
    dl_file *f = ctx;
    s32 r;
    if (at >= f->size)
        return 0;
    f->pos = at;                         /* reads stream from f->pos (store.c): a seek is a store */
    r = dl_fs_read(f, dst, n);
    if (r > 0)
        dl_breathe();
    return r;
}

/* dl.h: a file read whole for the linker, its payload's hex skipped (skip.h). At boot each payload
 * skipped is noted for the Drive payload table. */
s32 dl_read_file(const char *name, u8 *buf, u32 max)
{
    char path[DL_PATH_MAX];
    dl_file f;
    struct dl_pay pay;
    s32 r;
    path_of(path, name);
    dl_memset(&f, 0, sizeof f);
    if (dl_fs_open(&f, path, 0) < 0) {
        dl_fs_close(&f);
        return -1;
    }
    r = dl_stage_file(read_at, &f, buf, max, &pay);
    dl_fs_close(&f);
    if (r >= 0 && pay.len && booting)
        dl_payload_note(path, &pay);
    return r;
}

/* The MODS page's read of a file: its pending update (<name>.new, an upload over a file in use;
 * skip.h dl_payload_held) when there is one, since that is the file the next boot links, so the
 * page checks it and a save names its CRC */
int dl_read_open(struct dl_reader *rd, const char *name)
{
    char path[DL_PATH_MAX + 4];
    path_of(path, name);
    dl_memset(rd, 0, sizeof *rd);
    add_new(path);
    if (dl_fs_file_size(path) < 0)
        path[dl_strlen(path) - (sizeof DL_NEW - 1)] = 0;
    if (dl_fs_open(&rd->f, path, 0) < 0) {
        dl_fs_close(&rd->f);
        return -1;
    }
    rd->open = 1;
    rd->size = rd->f.size;
    dl_stage_rd_init(&rd->st);
    return 0;
}

void dl_read_close(struct dl_reader *rd)
{
    if (rd->open)
        dl_fs_close(&rd->f);
    rd->open = 0;
}

/* dl_read_more's reads: read_at without the breath (the page reads one stretch a tick) */
static s32 read_page(void *ctx, u32 at, u8 *dst, u32 n)
{
    dl_file *f = ctx;
    if (at >= f->size)
        return 0;
    f->pos = at;
    return dl_fs_read(f, dst, n);
}

/* The page's reads stage as dl_read_file does (skip.h dl_stage_more): the linker's check then
 * sees the payload's hex as the boot does, and the CRC is the staged file's, as /mods/active and
 * the boot take it. A whole-file read gave payload mods "!" (BAD FILE) on the page, 2026-11-15. */
int dl_read_more(struct dl_reader *rd, u8 *buf, u32 max)
{
    s32 r;
    if (!rd->open)
        return -3;
    r = dl_stage_more(&rd->st, read_page, &rd->f, buf, max);
    if (!r)
        return 0;
    dl_read_close(rd);
    return r;
}

static int exists(const char *name)
{
    char path[DL_PATH_MAX];
    dl_file f;
    int r;
    path_of(path, name);
    dl_memset(&f, 0, sizeof f);
    r = dl_fs_open(&f, path, 0);
    dl_fs_close(&f);
    return r == 0;
}

static void remove_file(const char *name)
{
    char path[DL_PATH_MAX];
    path_of(path, name);
    dl_fs_delete(path);
}

static void write_armed(void)
{
    char path[DL_PATH_MAX];
    dl_file f;
    static const char text[] = "armed\n";
    path_of(path, ".armed");
    dl_memset(&f, 0, sizeof f);
    if (dl_fs_open(&f, path, 1) == 0)
        dl_fs_write(&f, text, sizeof text - 1);
    dl_fs_close(&f);
}

static void panel(u8 a, u8 b)
{
    PANEL_BYTE(a);
    PANEL_BYTE(b);
}

/* [NO] held, read with the full report query (60 01): a key held since power-on was reported to
 * the bootstrap, not to main, so main's bitmap needs the query to see it; stock's receive handler
 * fills the bitmap from the 12 group reports within the 20 ms. Then main's own mode (60 00) again,
 * as main sets it before the site. A held [NO] also reaches the UI later as one NO press. [emu,
 * 2026-11-14: held 1, not held 0, boot unchanged; the mode switch itself is for the unit run] */
static int skip_key(void)
{
    int held;
    panel(0x60, 0x01);
    SLEEP_US(20000);
    held = KEY_HELD(KEY_NO) != 0;
    panel(0x60, 0x00);
    return held;
}

/* The boot's time in µs: the whole of dl_boot, then its checks (drive, skip key, crash
 * guard), the link (/mods/active, .armed, the linker, the cache flush) and the inits (each
 * drive_init). A part the boot didn't reach takes 0. */
u32 dl_boot_us[4];
static u32 t_checks, t_link;

static void boot(void);

void dl_boot(void)
{
    u32 t0 = DTCN0, t1, lo, hi;
    t_checks = t_link = 0;
    dl_stage_top = 1;                    /* each file read into the +Drive area itself (dl.h) */
    booting = 1;
    boot();
    booting = 0;
    if (dl_area((u32)core_desc, &lo, &hi) == 0)   /* the free tail, above the layout's end: the */
        dl_stage_tail(dl_used_hi > lo && dl_used_hi <= hi ? dl_used_hi : lo, hi);   /* route and the
                                          * MODS page stage there from now on (fs.h) */
    t1 = DTCN0;
    if (!t_checks)
        t_checks = t1;
    if (!t_link)
        t_link = t1;
    dl_boot_us[DL_T_ALL] = dl_bus_us(t0, t1);
    dl_boot_us[DL_T_CHECKS] = dl_bus_us(t0, t_checks);
    dl_boot_us[DL_T_LINK] = dl_bus_us(t_checks, t_link);
    dl_boot_us[DL_T_INIT] = dl_bus_us(t_link, t1);
}

/* /mods/active, read whole into the heap (the caller frees it). -> its length, <= 0 none. */
static s32 read_active(char **text)
{
    char path[DL_PATH_MAX];
    s32 size, len;
    *text = 0;
    path_of(path, "active");
    size = dl_fs_file_size(path);
    if (size <= 0)
        return 0;
    *text = dl_alloc((u32)size + 1);
    if (!*text)
        return 0;
    len = dl_read_file("active", (u8 *)*text, (u32)size);
    return len;
}

/* ---- /mods/.store (dstore.h): read before the link, written after it when it changed ---- */
static u8 *store_prev;
u8 dl_store_io;                          /* .status: 0 none, 1 kept, 2 written, 3 a write failed */
u32 dl_store_info[3];                    /* .status: the live layout ID, fields, parked fields */

static void read_store(void)
{
    char path[DL_PATH_MAX];
    s32 size, len;
    dl_free(store_prev);
    store_prev = 0;
    dl_store_prev = 0;
    dl_store_prev_n = 0;
    path_of(path, ".store");
    size = dl_fs_file_size(path);
    if (size <= 0 || size > DS_MAX_FILE)
        return;
    store_prev = dl_alloc((u32)size);
    if (!store_prev)
        return;
    len = dl_read_file(".store", store_prev, (u32)size);
    if (len != size) {
        dl_free(store_prev);
        store_prev = 0;
        return;
    }
    dl_store_prev = store_prev;
    dl_store_prev_n = (u32)len;
}

/* the placement's .store, when its bytes changed: over .store (the store keeps the old one until
 * the new one is whole) */
static void write_store(void)
{
    char path[DL_PATH_MAX];
    const struct ds_out *o = &dl_store_out;
    dl_file f;
    int r;
    u32 i;
    dl_store_io = 0;
    if (!o->bytes)
        return;
    dl_store_io = 1;
    dl_store_info[0] = o->id;
    dl_store_info[1] = o->nfields;
    dl_store_info[2] = o->nparked;
    if (dl_store_prev && dl_store_prev_n == o->nbytes) {
        for (i = 0; i < o->nbytes && dl_store_prev[i] == o->bytes[i]; i++)
            ;
        if (i == o->nbytes)
            return;
    }
    path_of(path, ".store");
    dl_lock();
    dl_memset(&f, 0, sizeof f);
    r = dl_fs_open(&f, path, 1);
    if (r == 0)
        r = dl_fs_write(&f, o->bytes, o->nbytes);
    dl_fs_close(&f);
    if (r == 0 && dl_fs_file_size(path) != (int)o->nbytes)
        r = -1;
    dl_unlock();
    dl_store_io = r == 0 ? 2 : 3;
}

static void boot(void)
{
    s32 len;
    int n, loaded;
    u32 i;
    char *active;

    dl_status_n = 0;
    dl_payload_reset();
    dl_skip_reason = DL_SKIP_NONE;
    if (ds_mount() != 0) {               /* SD init failed, no card, or a card too small */
        dl_skip_reason = DL_SKIP_DRIVE;
        return;
    }
    if (skip_key()) {
        dl_skip_reason = DL_SKIP_KEY;
        if (!exists(".armed")) {         /* no mods, but the +Drive fields keep their layout */
            read_store();
            if (dl_store_prev) {
                write_armed();
                dl_link((u32)core_desc, 0, 0, 0, 0);
                remove_file(".armed");
            }
            dl_skip_reason = DL_SKIP_KEY;
        }
        return;
    }
    if (exists(".armed")) {              /* the last start with mods didn't get 10 s in */
        remove_file(".armed");
        dl_skip_reason = DL_SKIP_ARMED;
        return;
    }
    t_checks = DTCN0;
    len = read_active(&active);
    n = len > 0 ? dl_parse_active(active, (u32)len, 0) : 0;   /* how many lines, first */
    if (n > 0) {
        dl_boot_choice = dl_alloc((u32)n * sizeof *dl_boot_choice);
        if (dl_boot_choice)
            dl_parse_active(active, (u32)len, dl_boot_choice);
    }
    dl_free(active);
    if (n < 0) {
        dl_skip_reason = DL_SKIP_COUNT;
        return;
    }
    read_store();
    if (!n && !dl_store_prev)
        return;                          /* no set chosen, no +Drive fields placed before */
    if (n && !dl_boot_choice) {
        dl_skip_reason = DL_SKIP_MEMORY;
        return;
    }
    dl_apply_updates(dl_boot_choice, n); /* before any file is staged */
    write_armed();                       /* before the link, so a link that faults or hangs is
                                          * caught too: the next boot skips the set */
    loaded = dl_link((u32)core_desc, dl_boot_choice, n, 0, 0);   /* the stage at the top of the area */
    if (dl_status)                       /* the payloads skipped: the loaded mods' kept (skip.h), */
        dl_payload_keep(dl_status, dl_boot_choice, (int)dl_status_n);   /* before their drive_init */
    dl_flush();                          /* after the area, the tables and the sites (each site's
                                          * lines already pushed by dl_apply_site) */
    write_store();                       /* a set that left none loaded parks its fields too */
    t_link = DTCN0;
    if (loaded <= 0) {
        remove_file(".armed");           /* nothing runs, so nothing to guard */
        return;
    }
    armed = 1;
    armed_ticks = 0;
    for (i = 0; i < dl_inits_n; i++)
        if (dl_inits[i])
            ((v_fn)dl_inits[i])();
}

/* ev_tick: the MODS page's frames (page.c), and the crash guard comes off 10 s into
 * ticking; after a boot that applied a site (dl_applied_n), 60 s in and once the MODS page
 * doesn't show, whichever is later, so a site that breaks a page or the audio is caught. */
void dl_page_tick(void *ctrl);
int dl_page_shown(void);

void dl_tick(void *ctrl)
{
    dl_page_tick(ctrl);
    if (!armed || ++armed_ticks < (dl_applied_n ? CRASH_TICKS_SITES : CRASH_TICKS))
        return;
    if (dl_applied_n && dl_page_shown())
        return;
    armed = 0;
    dl_lock();
    remove_file(".armed");
    dl_unlock();
}

/* The MODS page's APPLY & RESTART, under dl_lock: the guard comes off before the reboot, as
 * dl_tick would take it off had the page not shown (page.c, restart_tick) */
void dl_guard_off(void)
{
    armed = 0;
    remove_file(".armed");
}

/* ev_drive_format (core-a4mk2 0.9, 0x40074af6 in FORMAT +DRIVE, after the slot erases): the store
 * is emptied with the rest, /mods/active and every mod with it (decided 2026-11-14). The same
 * routine formats a card with no header at UI start, after the boot link: a fresh card's sectors
 * held no valid directory, so the boot read the store as empty. */
void dl_format(void *fs)
{
    (void)fs;
    ds_format();
    armed = 0;                           /* .armed went with it */
}
