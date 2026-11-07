/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Drive Loader, boot side (Analog Rytm MKII, OS 1.74): the boot stub's C half (flush.s has
 * the entry at the site 0x401009e0), the skip key, the crash guard, /mods/active, and the
 * tick that clears the crash guard. The linker is dlink.c.
 *
 * At the site, main() (FUN_401006ee) has mounted ekFS and runs as an RTOS task with
 * interrupts on; the audio start (FUN_4011f9e0) and the UI task (0x40100a8c) come after it,
 * so no table the linker rewrites can be read while it does. */
#include "dl.h"
#include "fs.h"

#define EMMC_ERROR (*(volatile u32 *)0x419ae2c0)     /* 0 = the eMMC came up */
#define EKFS_MOUNT (*(volatile u32 *)0x419ae2bc)     /* 0 = ekFS is mounted */

/* The panel link (design-step-3.md, section 5) */
typedef void (*v_fn)(void);
typedef void (*send_fn)(u32 len, const u8 *buf);
typedef void (*sleep_fn)(u32 us);
typedef u32 (*held_fn)(u32 code);
#define PANEL_LOCK ((v_fn)0x40001bf6)                /* the panel TX mutex */
#define PANEL_UNLOCK ((v_fn)0x40001c06)
#define PANEL_SEND ((send_fn)0x400019e4)
#define SLEEP_US ((sleep_fn)0x40098c84)
#define KEY_HELD ((held_fn)0x40081c28)               /* reads the bitmap 0x418d1340 */
#define KEY_NO 0x45

#define CRASH_TICKS 200                              /* ev_tick is about 20 Hz: 10 s */
#define DTCN0 (*(volatile u32 *)0xfc07000c)          /* bus clock count (dl.h, "boot time") */

extern char core_desc[];
void dl_flush(void);

struct dl_choice dl_boot_choice[DL_MAX_MODS];     /* the boot's set: dl_status[i] is line i's */
static char active[DL_MAX_MODS * (DL_LINE_NAME_MAX + 12) + 64];
static u32 armed_ticks;
static u8 armed;

static void path_of(char *out, const char *name)
{
    const char *d = DL_DIR;
    u32 k = 0;
    while (*d)
        out[k++] = *d++;
    out[k++] = '/';
    while (*name && k < 127)
        out[k++] = *name++;
    out[k] = 0;
}

s32 dl_read_file(const char *name, u8 *buf, u32 max)
{
    char path[128];
    dl_file f;
    u32 got = 0;
    s32 r;
    u8 more;
    path_of(path, name);
    dl_memset(&f, 0, sizeof f);
    if (dl_fs_open(&f, path, 0) < 0) {
        dl_fs_close(&f);
        return -1;
    }
    while (got < max) {                  /* to the file's end, or max */
        r = dl_fs_read(&f, buf + got, max - got);
        if (r < 0) {
            dl_fs_close(&f);
            return -3;
        }
        if (r == 0)
            break;
        got += (u32)r;
    }
    r = got == max ? dl_fs_read(&f, &more, 1) : 0;
    dl_fs_close(&f);
    return r > 0 ? -2 : (s32)got;
}

static int exists(const char *name)
{
    char path[128];
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
    char path[128];
    path_of(path, name);
    dl_fs_delete(path);
}

static void write_armed(void)
{
    char path[128];
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
    static u8 msg[2];                    /* the panel's DMA ring copies from here */
    msg[0] = a;
    msg[1] = b;
    PANEL_LOCK();
    PANEL_SEND(2, msg);
    PANEL_UNLOCK();
}

/* [NO] held, read with the bootstrap's own "report every button group" query (61 01): a
 * key held since power-on was reported to the bootstrap, not to MAIN, so the bitmap needs
 * the query to see it. Then MAIN's own mode (60 00) again. */
static int skip_key(void)
{
    int i, held;
    panel(0x61, 0x01);
    for (i = 0; i < 20; i++)
        SLEEP_US(1000);
    held = KEY_HELD(KEY_NO) != 0;
    panel(0x60, 0x00);
    return held;
}

/* The boot's time in µs: the whole of dl_boot, then its checks (drive, skip key, crash
 * guard), the link (/mods/active, the linker, the cache flush) and the inits (.armed, each
 * drive_init). A part the boot didn't reach takes 0. */
u32 dl_boot_us[4];
static u32 t_checks, t_link;

static void boot(void);

void dl_boot(void)
{
    u32 t0 = DTCN0, t1;
    t_checks = t_link = 0;
    boot();
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

static void boot(void)
{
    s32 len;
    int n, loaded;
    u32 i;

    dl_status_n = 0;
    dl_skip_reason = DL_SKIP_NONE;
    if (EMMC_ERROR != 0 || EKFS_MOUNT != 0) {
        dl_skip_reason = DL_SKIP_DRIVE;
        return;
    }
    if (skip_key()) {
        dl_skip_reason = DL_SKIP_KEY;
        return;
    }
    if (exists(".armed")) {              /* the last start with mods didn't get 10 s in */
        remove_file(".armed");
        dl_skip_reason = DL_SKIP_ARMED;
        return;
    }
    t_checks = DTCN0;
    len = dl_read_file("active", (u8 *)active, sizeof active - 1);
    if (len <= 0)
        return;                          /* no set chosen */
    n = dl_parse_active(active, (u32)len, dl_boot_choice);
    if (n < 0) {
        dl_skip_reason = DL_SKIP_COUNT;
        return;
    }
    if (!n)
        return;
    loaded = dl_link((u32)core_desc, dl_boot_choice, n, (u8 *)drive_staging, DL_STAGING_SIZE);
    dl_flush();
    t_link = DTCN0;
    if (loaded <= 0)
        return;
    write_armed();
    armed = 1;
    armed_ticks = 0;
    for (i = 0; i < dl_inits_n; i++)
        if (dl_inits[i])
            ((v_fn)dl_inits[i])();
}

/* ev_tick: the MODS page's frames (page.c), and the crash guard comes off 10 s into
 * ticking. */
void dl_page_tick(void *ctrl);

void dl_tick(void *ctrl)
{
    dl_page_tick(ctrl);
    if (!armed || ++armed_ticks < CRASH_TICKS)
        return;
    armed = 0;
    dl_lock();
    remove_file(".armed");
    dl_unlock();
}
