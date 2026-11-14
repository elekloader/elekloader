/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TELOS for the Octatrack (OS 1.40C), 0.1: mods from the CF card, linked at boot.
 *
 * At the card's first mount (core-ot 0.41's ev_card, in the sys task: after main's end and the USB
 * init, before the project load and the first audio frame), the loader reads
 * /ELEKLOADER/MODS/active, links the .elemod files it names into drive_area with the Rytm's boot
 * linker (mods/drive-loader-ar2/dlink.c, built with DL_SITES: mods may write sites), and writes
 * /ELEKLOADER/MODS/.status. Sites and the tables' merge are written with interrupts masked, once no
 * task is stopped inside a site (dl_commit_begin). The design: research/telos-ot/.
 */
#include "../drive-loader-ar2/dl.h"
#include "octatrack/core.h"

extern char core_desc[];
extern u8 drive_staging[], telos_heap[];
extern char drive_staging_size[], telos_heap_size[];
void dl_flush(void);
u32 telos_irq_off(void);
void telos_irq_restore(u32 sr);
u32 telos_sp(void);

#define DIR "/ELEKLOADER/MODS/"
#define F_SIZE ((int (*)(void *))0x400148d4)      /* a file's size: the FS's size op on its handle */
#define CUR_TCB (*(volatile u32 *)0x800068fc)     /* the RTOS's current task */
#define IMAGE_LO 0x40000400u                       /* the OS image in RAM (sites may go here only) */
#define IMAGE_HI 0x4010fdf0u

/* What the boot did, for .status and a debugger: telos_state[0] the phase (1 started, 2 linked,
 * 3 skipped), [1] mods loaded, [2] lines in active, [3] skip reason, [4] quiescence tries,
 * [5..7] the first site as written (address, length, its first word), [8] status.txt's open. */
volatile u32 telos_state[12];

/* ---- the linker's host: memory, files, sha256 ------------------------------------------------- */
static u32 heap_used;

void *dl_alloc(u32 n)
{
    u32 size = (u32)(uintptr_t)telos_heap_size;
    u8 *p;
    n = (n + 15) & ~15u;
    if (heap_used + n > size)
        return 0;
    p = telos_heap + heap_used;
    heap_used += n;
    dl_memset(p, 0, n);
    return p;
}

void dl_free(void *p) { (void)p; }        /* the bump heap is reset at each boot's link */

/* GCC calls memcpy for dlink.c's struct copies; core-ot exports none (core-ar2 does). The loop
 * must not become a call to itself. */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *d, const void *s, unsigned long n)
{
    u8 *a = d;
    const u8 *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void dl_breathe(void) {}

static struct ot_file f;
static char path[DL_LINE_NAME_MAX + 32];

static void path_of(const char *name)
{
    u32 k = 0;
    const char *d = DIR;
    while (*d)
        path[k++] = *d++;
    while (*name && k < sizeof path - 1)
        path[k++] = *name++;
    path[k] = 0;
}

s32 dl_read_file(const char *name, u8 *buf, u32 max)
{
    int n;
    path_of(name);
    if (ot_file_open(&f, path, "r") < 0)
        return -1;
    n = F_SIZE(&f);
    if (n < 0) {
        ot_file_close(&f);
        return -3;
    }
    if ((u32)n > max) {
        ot_file_close(&f);
        return -2;
    }
    if (n && ot_file_read(&f, buf, (u32)n) != 1) {
        ot_file_close(&f);
        return -3;
    }
    ot_file_close(&f);
    return n;
}

static const u32 K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))

static void sha_block(u32 *h, const u8 *p)
{
    u32 w[64], a, b, c, d, e, g, hh, ff, i;
    for (i = 0; i < 16; i++)
        w[i] = dl_rd32(p + 4 * i);
    for (; i < 64; i++) {
        u32 s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; ff = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
        u32 t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & ff) ^ (~e & g)) + K[i] + w[i];
        u32 t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = ff; ff = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += ff; h[6] += g; h[7] += hh;
}

void dl_sha256(const u8 *p, u32 n, u8 out[32])
{
    static const u32 h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
                              0x1f83d9ab, 0x5be0cd19};
    u32 h[8];
    u8 blk[64];
    u32 i, done = 0, r;
    for (i = 0; i < 8; i++)             /* word by word: a struct copy would call memcpy */
        h[i] = h0[i];
    while (n - done >= 64) {
        sha_block(h, p + done);
        done += 64;
    }
    r = n - done;
    dl_memset(blk, 0, 64);
    dl_memcpy(blk, p + done, r);
    blk[r] = 0x80;
    if (r >= 56) {
        sha_block(h, blk);
        dl_memset(blk, 0, 64);
    }
    dl_wr32(blk + 56, 0);
    dl_wr32(blk + 60, n << 3);
    sha_block(h, blk);
    for (i = 0; i < 8; i++)
        dl_wr32(out + 4 * i, h[i]);
}

int dl_site_ok(u32 addr, u32 len) { return addr >= IMAGE_LO && addr + len <= IMAGE_HI; }

/* The OS changes some of its image's data before the card mounts (0x400b9714 is one, measured), so a
 * mod's stock parts there would copy the changed bytes. ev_boot_early (before the OS runs) keeps a
 * copy of the data region; the stock parts come from it. The DSP payloads after it are wiped by
 * then anyway (research/telos-ot/DSP.md). */
#define DATA_LO 0x400b9000u
#define DATA_HI 0x400e2324u
extern u8 telos_stockdata[];

void telos_boot_early(void)
{
    dl_memcpy(telos_stockdata, (const void *)DATA_LO, DATA_HI - DATA_LO);
}

/* What each site the boot wrote replaced: the menu's checks (menu.c) see the OS as it was before
 * the link, so a mod already running checks as it did at boot. */
#define SAVES 1024
static struct { u32 addr; u16 len, off; } saved[SAVES];
static u8 saved_bytes[0x8000];
static u32 nsaved, saved_n;

void dl_site_saving(u32 addr, u32 len)
{
    if (nsaved >= SAVES || saved_n + len > sizeof saved_bytes)
        return;
    saved[nsaved].addr = addr;
    saved[nsaved].len = (u16)len;
    saved[nsaved].off = (u16)saved_n;
    dl_memcpy(saved_bytes + saved_n, (const void *)addr, len);
    saved_n += len;
    nsaved++;
}

void dl_stock_read(u8 *dst, u32 a, u32 n)
{
    u32 i, k;
    for (i = 0; i < n; i++)
        dst[i] = a + i >= DATA_LO && a + i < DATA_HI ? telos_stockdata[a + i - DATA_LO] : *(const u8 *)(a + i);
    for (k = 0; k < nsaved; k++) {
        u32 lo = saved[k].addr > a ? saved[k].addr : a;
        u32 hi = saved[k].addr + saved[k].len < a + n ? saved[k].addr + saved[k].len : a + n;
        for (; lo < hi; lo++)
            dst[lo - a] = saved_bytes[saved[k].off + (lo - saved[k].addr)];
    }
}

/* ---- quiescence ------------------------------------------------------------------------------
 * The RTOS's tasks are static (octabam's model, ot_emu/rtos.cpp): each TCB saves d0-sp at +12, so
 * its stack pointer at +72, and a stopped task's PC is in the exception frame there. A site may be
 * written only when no live stack word points strictly inside one: a task stopped there, or a
 * return address into its middle. */
struct task { u32 tcb, stack, size; };
static const struct task tasks[] = {
    {0x46c7fb0c, 0x46c7ea20, 0x1000}, {0x460fab80, 0x460fabd4, 0x2000}, {0x460ffd44, 0x460fdd44, 0x2000},
    {0x460e0e38, 0x460dee38, 0x2000}, {0x460ddde4, 0x460d9de4, 0x4000}, {0x46105508, 0x4610555c, 0x2000},
    {0x46c7bed8, 0x460d6de4, 0x2000}, {0x460bcc2c, 0x460bc42c, 0x0800}, {0x460d4f80, 0x460d4780, 0x0800},
    {0x460d59d4, 0x460d51d4, 0x0800},
};

static int inside(u32 v)
{
    u32 i, a, n;
    for (i = 0; i < dl_sites_n(); i++)
        if (dl_site_live(i, &a, &n) && v > a && v < a + n)
            return 1;
    return 0;
}

static int stack_busy(u32 lo, u32 hi)
{
    for (lo &= ~1u; lo + 4 <= hi; lo += 2)
        if (inside(*(volatile u32 *)lo))
            return 1;
    return 0;
}

static u32 commit_sr;

int dl_commit_begin(void)
{
    u32 tries, i;
    for (tries = 0; tries < 50; tries++) {
        int busy = 0;
        commit_sr = telos_irq_off();
        for (i = 0; i < sizeof tasks / sizeof tasks[0] && !busy; i++) {
            u32 lo, hi = tasks[i].stack + tasks[i].size;
            lo = tasks[i].tcb == CUR_TCB ? telos_sp() : *(volatile u32 *)(tasks[i].tcb + 72);
            if (lo < tasks[i].stack || lo >= hi)
                continue;                /* not made yet, or not on its own stack */
            busy = stack_busy(lo, hi);
        }
        telos_state[4] = tries + 1;
        if (!busy)
            return 1;                    /* interrupts stay masked until dl_commit_end */
        telos_irq_restore(commit_sr);
        for (i = 0; i < 200000; i++)     /* let the tasks above the sys task move on */
            __asm__ volatile("nop");
    }
    return 0;
}

void dl_commit_end(void)
{
    u32 a, n;
    if (dl_site_live(0, &a, &n)) {       /* the first site, as written: for a debugger */
        telos_state[5] = a;
        telos_state[6] = n;
        telos_state[7] = *(volatile u32 *)a;
    }
    dl_flush();
    telos_irq_restore(commit_sr);
}

/* ---- .status ---------------------------------------------------------------------------------- */
static char out[4096];
static u32 out_n;

static void put_s(const char *s)
{
    while (*s && out_n < sizeof out - 1)
        out[out_n++] = *s++;
}

static void put_u(u32 v)
{
    char b[12];
    int k = 0;
    do
        b[k++] = (char)('0' + v % 10);
    while ((v /= 10) && k < 11);
    while (k)
        out[out_n < sizeof out - 1 ? out_n++ : out_n] = b[--k];
}

static void write_status(int n)
{
    static const char *const st[] = {"none", "loaded", "refused", "dropped"};
    int i;
    out_n = 0;
    put_s("telos 0.1: ");
    put_u(telos_state[1]);
    put_s(" loaded of ");
    put_u((u32)n);
    put_s(", skip ");
    put_u(dl_skip_reason);
    put_s(", tries ");
    put_u(telos_state[4]);
    put_s("\n");
    for (i = 0; i < n && dl_status; i++) {
        put_s(dl_status[i].id);
        put_s(" ");
        put_s(dl_status[i].state < 4 ? st[dl_status[i].state] : "?");
        if (dl_status[i].code) {
            put_s(" ");
            put_u(dl_status[i].code);
            if (dl_detail && dl_detail[i][0]) {
                put_s(" ");
                put_s(dl_detail[i]);
            }
        }
        put_s("\n");
    }
    path_of("status.txt");               /* FAT: the OT's file layer takes no leading dot */
    telos_state[8] = (u32)ot_file_open(&f, path, "w") | 0x80000000u;
    if ((s32)(telos_state[8] << 1) >= 0) {
        ot_file_write(&f, out, out_n);
        ot_file_close(&f);
    }
}

/* ---- the boot ----------------------------------------------------------------------------------- */
struct dl_choice telos_choice[64];      /* the boot's set: menu.c reads it */
int telos_nchoice;
#define choice telos_choice
void telos_menu_scan(void);

void telos_heap_reset(void) { heap_used = 0; }
static char active[8192];
static u8 done;

static void telos_boot(void)
{
    s32 len;
    int n, loaded;
    u32 i;
    telos_state[0] = 1;
    heap_used = 0;
    len = dl_read_file("active", (u8 *)active, sizeof active);
    if (len < 0) {
        telos_state[0] = 3;
        return;                          /* no active: nothing chosen */
    }
    n = dl_parse_active(active, (u32)len, 0);
    if (n < 0 || n > (int)(sizeof choice / sizeof choice[0])) {
        telos_state[0] = 3;
        telos_state[3] = DL_SKIP_COUNT;
        return;
    }
    dl_parse_active(active, (u32)len, choice);
    telos_nchoice = n;
    telos_state[2] = (u32)n;
    loaded = dl_link((u32)(uintptr_t)core_desc, choice, n, drive_staging,
                     (u32)(uintptr_t)drive_staging_size);
    telos_state[1] = (u32)loaded;
    telos_state[3] = dl_skip_reason;
    telos_state[0] = 2;
    write_status(n);
    for (i = 0; i < dl_inits_n; i++)
        if (dl_inits[i])
            ((void (*)(void))dl_inits[i])();
}

/* ev_card: the first time a card is mounted. */
void telos_card(unsigned state, int result, unsigned mode)
{
    (void)mode;
    if (done || state != 1 || result != 0)
        return;
    done = 1;
    telos_boot();
    telos_menu_scan();                   /* the MODS rows (menu.c), settings-ot builds them next */
}
