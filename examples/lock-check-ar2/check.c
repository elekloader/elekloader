/* SPDX-License-Identifier: GPL-2.0-or-later */
/* lock-check: the smallest user of core-ar2's lockable parameters (core 0.8)
 * and their p-locks.
 *
 * mod.json declares three sound fields with "lock": true, so the lockfile
 * gives each a lock code, and registers them in core_lock_params. A and C
 * are also "lfo": true, so core lists them at the end of every sound's LFO
 * destination menu, as MOD:Lock Check A and LCHK:Lock Check C (1.5; shown as
 * LCA and LCC in the DST value); B,
 * lockable only, is not listed (1.4). A bar along the bottom edge shows the
 * offset track 1's LFO gives A or C (core_lfo_offset), from the middle, one
 * pixel per stock knob step; a 3x3 square at the left end shows which (top:
 * A, bottom: C).
 *
 * Behind the LFO key, a LOCK CHECK page shows A on knob A (borrowing stock's
 * id 1, as mods' pages do) and C, 0..1000, a value above 127, on knob B (id
 * 2) for track 1's sound of the current kit: RANDOMIZE PAGE and RELOAD PAGE
 * reach them through their lock parameters' ids. With a trig
 * held, knob A sets that trig's p-lock of A instead, and a second
 * bar above the first shows the lock of the trig track 1 plays, half a pixel
 * a step from the left (core_lock_value), none when it has no lock of A.
 * With a scene's or a performance's pad held, knob A sets the scene's lock or
 * the performance's depth of A, and a third bar above shows A as the active
 * scene and the performances leave it (core_scene_value, core_perf_offset),
 * none while neither touches it.
 */
#include "analog-rytm-mk2/store.h"
#include "analog-rytm-mk2/lock.h"
#include "analog-rytm-mk2/pages.h"
#include "analog-rytm-mk2/load.h"

typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#define FILLRECT ((fillrect_t)FILLRECT_AT)

STORE_FIELD(lc_a);
STORE_FIELD(lc_b);
STORE_FIELD(lc_c);
STORE_FIELD(lc_d);
STORE_FIELD(lc_k);
STORE_FIELD(lc_n);

/* 1.5: A takes core's category (MOD), C its own (LCHK) */
const struct core_lock_param lc_param_a = {STORE(lc_a), 1, 0, 127, "LCA", "Lock Check A", 0};
/* 1.12 (core 0.11): B, 0..15, on knob D of the LOCK CHECK page (id 5, an
 * Error record), shown on the dial's scale: its get answers round(v * 127 /
 * 15) and its core_param_scales entry asks core's linear map, so a held
 * trig's or scene's lock, a performance's depth, its NRPN and its text
 * agree with the knob as it shows without a pad held */
const struct core_lock_param lc_param_b = {STORE(lc_b), 5, 0, 15, "LCB", "Lock Check B", 0};
const struct core_param_scale lc_scale_b = {5, 5, 0, 0, 0};
const struct core_lock_param lc_param_c = {STORE(lc_c), 2, 0, 1000, "LCC", "Lock Check C",
                                           CORE_LOCK_CATEGORY("LCHK")};
/* 1.6: D's strings are longer than core shows (4, 32 and 4 characters), so
 * the DST value, the menu and the category show core's cut; the category is
 * given without CORE_LOCK_CATEGORY, which would refuse it */
#define X10 "LCDXLCDXLC"
const struct core_lock_param lc_param_d = {
    STORE(lc_d), 0, 0, 1, X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10 X10,
    "Lock Check D, a name longer than core shows in the menu", "LCHKXYZ"};
/* 1.9 (core 0.9): K, a kit field: the FX track's, an FX LFO destination with
 * a CC, 0..127 on knob A of a LOCK CHECK FX page behind the FX LFO key (id 3,
 * an Error record as ids 1 and 2) */
const struct core_lock_param lc_param_k = {STORE(lc_k), 3, 0, 127, "LCK", "Lock Check K",
                                           CORE_LOCK_CATEGORY("LCHK")};

/* 1.10 (core 0.10): N, a sound field with "init": 100, so it reads 100 in a
 * new project and in a sound from another layout; an LFO destination with a
 * CC, on knob C of the LOCK CHECK page (id 4, an Error record as ids 1..3) */
const struct core_lock_param lc_param_n = {STORE(lc_n), 4, 0, 127, "LCN", "Lock Check N",
                                           CORE_LOCK_CATEGORY("LCHK")};

/* track 1's sound of the current kit: the live kits as edited, the stored
 * project's working kit index */
static void *sound1(void)
{
    const volatile unsigned char *ix = (const unsigned char *)0x405c8f20 + 0x19d3f7;
    unsigned k = (unsigned)ix[0] << 24 | (unsigned)ix[1] << 16 | (unsigned)ix[2] << 8 | ix[3];
    return k < 128 ? (void *)(0x407c89cb + 0xa8cf18 + 2531 * k + 42) : 0;
}

/* the current kit (the FX track's item) */
static void *kit1(void)
{
    unsigned char *s = sound1();
    return s ? s - 42 : 0;
}

static volatile unsigned lc_ticks;

void lc_tick(void *ctrl)
{
    if ((++lc_ticks & 3) == 0)
        *((volatile unsigned char *)ctrl + 0x20) = 1;
}

void lc_draw(void *bmp, void *ctrl)
{
    int a = core_lfo_offset(0, STORE(lc_a)), c = core_lfo_offset(0, STORE(lc_c));
    int v = (a ? a : c) / 256, p;
    void *s;
    (void)ctrl;
    /* y counts from the bottom row: 0..3 is the bottom edge */
    FILLRECT(bmp, 0, 0, 127, 3, 0);
    if (v > 60)
        v = 60;
    if (v < -60)
        v = -60;
    if (v > 0)
        FILLRECT(bmp, 64, 0, 64 + v, 1, 1);
    else if (v < 0)
        FILLRECT(bmp, 64 + v, 0, 64, 1, 1);
    if (a)
        FILLRECT(bmp, 0, 2, 2, 3, 1);
    if (c)
        FILLRECT(bmp, 0, 0, 2, 1, 1);
    FILLRECT(bmp, 0, 5, 127, 6, 0);
    if ((v = core_lock_value(0, STORE(lc_a))) >= 0)
        FILLRECT(bmp, 0, 5, v / 2, 6, 1);
    /* the scene's lock of A, else the sound's, plus the performances' */
    FILLRECT(bmp, 0, 8, 127, 9, 0);
    v = core_scene_value(0, STORE(lc_a));
    p = core_perf_offset(0, STORE(lc_a));
    if (v >= 0 || p) {
        if (v < 0)
            v = (s = sound1()) ? (int)core_store_get(STORE(lc_a), s, 0) : 0;
        v += p / 256;
        v = v < 0 ? 0 : v > 127 ? 127 : v;
        FILLRECT(bmp, 0, 8, v / 2, 9, 1);
    }
    /* 1.6: the same two for C in the right half, from x 64, a pixel per
     * 16 steps (drawn above A's would cover the knobs' labels): the lock of
     * the trig track 1 plays, and C as the active scene and the performances
     * leave it */
    if ((v = core_lock_value(0, STORE(lc_c))) >= 0)
        FILLRECT(bmp, 64, 5, 64 + v / 16, 6, 1);
    v = core_scene_value(0, STORE(lc_c));
    p = core_perf_offset(0, STORE(lc_c));
    if (v >= 0 || p) {
        if (v < 0)
            v = (s = sound1()) ? (int)core_store_get(STORE(lc_c), s, 0) : 0;
        v += p / 256;
        v = v < 0 ? 0 : v > 1000 ? 1000 : v;
        FILLRECT(bmp, 64, 8, 64 + v / 16, 9, 1);
    }
}

const struct core_page lc_page = {14, "LOCK CHECK", {1, 2, 4, 5, 0, 0, 0, 0}};
const struct core_view_page lc_view_page = {CORE_VIEW_LFO, 14, 0};
const struct core_page lc_fx_page = {15, "LOCK CHECK FX", {3, 0, 0, 0, 0, 0, 0, 0}};
const struct core_view_page lc_fx_view_page = {CORE_VIEW_FX_LFO, 15, 0};

/* knob A is A (id 1, 0..127), knob B is C (id 2, 0..1000), knob C is N (id
 * 4, 0..127), knob D is B (id 5, 0..15 on the dial's 0..127); on the FX
 * page knob A is K (id 3, 0..127), in the kit */
static unsigned lc_field(int32_t id)
{
    return id == 1 ? STORE(lc_a) : id == 3 ? STORE(lc_k) : id == 4 ? STORE(lc_n) : id == 5 ? STORE(lc_b) : STORE(lc_c);
}

static void *lc_item(int32_t id)
{
    return id == 3 ? kit1() : sound1();
}

static int lc_get(int32_t id, int32_t *value)
{
    void *s = lc_item(id);
    if (id < 1 || id > 5 || !s)
        return 0;
    *value = core_store_get(lc_field(id), s, 0);
    *value = id == 5 ? (*value * 127 + 7) / 15 << 8 : *value << 8;
    return 1;
}

static int lc_delta(void *view, int32_t id, int32_t delta, int32_t func)
{
    void *s = lc_item(id);
    int v, top = id == 2 ? 1000 : id == 5 ? 15 : 127;
    (void)view;
    (void)func;
    if (id < 1 || id > 5 || !s)
        return 0;
    v = (int)core_store_get(lc_field(id), s, 0) + (delta > 0 ? (delta + 255) / 256 : -((255 - delta) / 256));
    core_store_set(lc_field(id), s, 0, v < 0 ? 0 : v > top ? top : v);
    return 1;
}

/* C's value as a number: stock draws a 0..127 knob's */
static int lc_text(struct core_text *t)
{
    int v = t->value >> 8, n = 0, d;
    char *b = t->buf;
    if (t->id != 2)
        return CORE_TEXT_PASS;
    for (d = 1000; d > 1; d /= 10)
        if (v >= d || n)
            b[n++] = '0' + v / d % 10;
    b[n++] = '0' + v % 10;
    b[n] = 0;
    return CORE_TEXT_DONE;
}

const struct core_param lc_params = {1, 5, lc_get, lc_delta, lc_text};

/* 1.7 (core 0.9): every audio block (1.13: every 8th), core_param_value of A, B ("smooth":
 * false) and C on track 1, with A's and C's values in the sound, the
 * offsets the LFO and the performances give A and A's p-lock and scene lock,
 * in a ring the emulator's
 * checks read (after the magic SMCK: the next index, then the three calls'
 * cost in DMA timer 0's ticks, the most and the sum, and the number of
 * blocks) */
#define DTCN0 (*(volatile unsigned *)0xfc07000c)
#define RING 512

struct lc_rec {
    unsigned block;
    int a, b, c;
    short ta, tc;
    short lfo_a, perf_a;
    short lock_a, scene_a;
    int k;                      /* 1.9: core_param_value of K on the FX track */
    short tk, lfo_k;            /* K in the kit, the FX LFO's offset to K */
    short lock_k, scene_k;      /* K's p-lock and scene lock */
    short perf_k, pad;
};

struct {
    char magic[4];
    unsigned next, cost_max, cost_sum, blocks;
    struct lc_rec rec[RING];
} lc_ring = {{'S', 'M', 'C', 'K'}};

/* 1.10 (core 0.10): N on track 1 at the last block, after the magic INIT:
 * the block, core_param_value (8.8), N in the sound, the LFO's offset to N
 * and N's p-lock and scene lock */
struct {
    char magic[4];
    unsigned block;
    int value;
    short sound, lfo, lock, scene;
} lc_init = {{'I', 'N', 'I', 'T'}};

/* 1.11 (core 0.10): A, B and C read again in one core_param_values call by
 * their handles, after the magic BTCH: that call's cost in DMA timer 0's
 * ticks, the most and the sum, the blocks, and the blocks where any of the
 * three differed from its own core_param_value read in the same block */
struct {
    char magic[4];
    unsigned cost_max, cost_sum, blocks, mismatches;
} lc_batch = {{'B', 'T', 'C', 'H'}};

static void lc_batch_check(const struct lc_rec *r, unsigned single)
{
    static int h[3];
    int o[3];
    unsigned t0, t;
    (void)single;
    if (!h[0]) {
        h[0] = core_param_handle(STORE(lc_a));
        h[1] = core_param_handle(STORE(lc_b));
        h[2] = core_param_handle(STORE(lc_c));
    }
    t0 = DTCN0;
    core_param_values(0, h, 3, 0, o);
    t = DTCN0 - t0;
    if (o[0] != r->a || o[1] != r->b || o[2] != r->c)
        lc_batch.mismatches++;
    if (t > lc_batch.cost_max)
        lc_batch.cost_max = t;
    lc_batch.cost_sum += t;
    lc_batch.blocks++;
}

/* 1.13 (core 0.11): every LC_STRIDE-th audio block only. Every block cost
 * 59 us of the Rytm's 667 us block (8.8 % of the CPU on the unit), enough with core
 * 0.11 to starve the UI task; each record keeps its block number, so the
 * checks read the ring as before, with gaps */
#define LC_STRIDE 8

void lc_render(void)
{
    struct lc_rec *r;
    void *s;
    unsigned t0, t;
    if (core_block % LC_STRIDE)
        return;
    r = &lc_ring.rec[lc_ring.next % RING];
    s = sound1();
    t0 = DTCN0;
    r->a = core_param_value(0, STORE(lc_a));
    r->b = core_param_value(0, STORE(lc_b));
    r->c = core_param_value(0, STORE(lc_c));
    t = DTCN0 - t0;
    r->block = core_block;
    r->ta = s ? (short)core_store_get(STORE(lc_a), s, 0) : -1;
    r->tc = s ? (short)core_store_get(STORE(lc_c), s, 0) : -1;
    r->lfo_a = core_lfo_offset(0, STORE(lc_a));
    r->perf_a = core_perf_offset(0, STORE(lc_a));
    r->lock_a = core_lock_value(0, STORE(lc_a));
    r->scene_a = core_scene_value(0, STORE(lc_a));
    r->k = core_param_value(12, STORE(lc_k));
    r->tk = kit1() ? (short)core_store_get(STORE(lc_k), kit1(), 0) : -1;
    r->lfo_k = core_lfo_offset(12, STORE(lc_k));
    r->lock_k = core_lock_value(12, STORE(lc_k));
    r->scene_k = core_scene_value(12, STORE(lc_k));
    r->perf_k = core_perf_offset(12, STORE(lc_k));
    lc_init.value = core_param_value(0, STORE(lc_n));
    lc_init.sound = s ? (short)core_store_get(STORE(lc_n), s, 0) : -1;
    lc_init.lfo = core_lfo_offset(0, STORE(lc_n));
    lc_init.lock = core_lock_value(0, STORE(lc_n));
    lc_init.scene = core_scene_value(0, STORE(lc_n));
    lc_init.block = core_block;
    lc_batch_check(r, t);
    if (t > lc_ring.cost_max)
        lc_ring.cost_max = t;
    lc_ring.cost_sum += t;
    lc_ring.blocks++;
    lc_ring.next++;
}
