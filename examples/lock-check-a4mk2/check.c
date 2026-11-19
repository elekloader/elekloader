/* SPDX-License-Identifier: GPL-2.0-or-later */
/* lock-check: the smallest user of core-a4mk2 0.5's lockable parameters and
 * their p-locks on the Analog Four MKII (OS 1.56).
 *
 * mod.json declares four lockable fields, so the lockfile gives each a lock
 * code of its kind, and registers them in core_lock_params:
 *   A  a sound field, 0..127, the synth tracks' (knob A of LOCK CHECK);
 *   C  a sound field, 0..1000, above 127, so its locks take two rows
 *      (knob B of LOCK CHECK);
 *   K  a kit field, 0..127, the FX track's (knob A of LOCK CHECK FX);
 *   V  a kit field with "track": "cv", 0..127, the CV track's (knob A of
 *      LOCK CHECK CV).
 * LOCK CHECK sits behind the AMP key on a synth track, after stock's AMP
 * page; LOCK CHECK FX and LOCK CHECK CV behind the LFO key on the FX and CV
 * tracks. The synth knobs show the current track's sound of the current kit,
 * the FX and CV knobs the current kit. With a trig held a knob sets that
 * trig's p-lock instead, and shows it.
 *
 * Every audio block (ev_render_out) lk_seen records, after the magic LKCK:
 * for voices 0..5 the playing lock of A and C (core_lock_value_voice) and
 * C's slide delta, and core_lock_value of A, C, K and V on tracks 1, 1, 5
 * and 6, for the emulator's checks; and, since a lock lasts only while its
 * trig plays, for each of A, C, K and V the blocks it was seen in and the
 * last value seen. Two bars along the bottom of the screen
 * show track 1's lock of A (half a pixel a step) and of C (a pixel per 8),
 * none while its trig has none.
 *
 * From 1.2 A and V also declare "cc": true (C and K have none, so they are
 * on MIDI by their NRPN only), and lk_midi keeps the four as stored each UI
 * tick, for core-a4mk2 0.5's MIDI checks.
 */
#include "analog-four-mk2/store.h"
#include "analog-four-mk2/lock.h"
#include "analog-four-mk2/pages.h"

typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#define FILLRECT ((fillrect_t)FILLRECT_AT)

STORE_FIELD(lk_a);
STORE_FIELD(lk_c);
STORE_FIELD(lk_k);
STORE_FIELD(lk_v);

#define ID_A 0x200
#define ID_C 0x201
#define ID_K 0x202
#define ID_V 0x203

const struct core_lock_param lk_param_a = {STORE(lk_a), ID_A, 0, 127, "LKA", "Lock Check A", 0};
const struct core_lock_param lk_param_c = {STORE(lk_c), ID_C, 0, 1000, "LKC", "Lock Check C",
                                           CORE_LOCK_CATEGORY("LCHK")};
const struct core_lock_param lk_param_k = {STORE(lk_k), ID_K, 0, 127, "LKK", "Lock Check K", 0};
const struct core_lock_param lk_param_v = {STORE(lk_v), ID_V, 0, 127, "LKV", "Lock Check V", 0};

/* the current kit: the stored project's working kit index, the live kits */
static unsigned char *kit(void)
{
    const volatile unsigned char *ix = (const unsigned char *)0x4045c974 + 0x199330;
    unsigned k = (unsigned)ix[0] << 24 | (unsigned)ix[1] << 16 | (unsigned)ix[2] << 8 | ix[3];
    return k < 128 ? (unsigned char *)0x411e9936 + 0x960 * k : 0;
}

/* the current track (0..5), as stock's lock calls find it */
#define APP ((unsigned (*)(void))0x401494de)
#define APP_TRACKS ((unsigned (*)(unsigned))0x400a51be)
#define CURRENT_TRACK ((int (*)(unsigned))0x400adbd2)

static void *item(int32_t id)
{
    unsigned char *k = kit();
    int t;
    if (!k)
        return 0;
    if (id == ID_K || id == ID_V)
        return k;
    t = CURRENT_TRACK(APP_TRACKS(APP()));
    return (unsigned)t < 4 ? k + 0x1c + 0x152 * t : 0;
}

static unsigned field(int32_t id)
{
    return id == ID_A ? STORE(lk_a) : id == ID_C ? STORE(lk_c) : id == ID_K ? STORE(lk_k) : STORE(lk_v);
}

static int lk_get(int32_t id, int32_t *value)
{
    void *s = item(id);
    if (!s)
        return 0;
    *value = core_store_get(field(id), s, 0) << 8;
    return 1;
}

static int lk_delta(void *view, int32_t id, int32_t delta, int32_t func)
{
    void *s = item(id);
    int v, top = id == ID_C ? 1000 : 127;
    (void)view;
    (void)func;
    if (!s)
        return 0;
    v = (int)core_store_get(field(id), s, 0) + (delta > 0 ? (delta + 255) / 256 : -((255 - delta) / 256));
    core_store_set(field(id), s, 0, v < 0 ? 0 : v > top ? top : v);
    return 1;
}

/* the value as a number */
static int lk_text(struct core_text *t)
{
    int v = t->value >> 8, n = 0, d;
    char *b = t->buf;
    if (v < 0)
        v = 0;
    for (d = 1000; d > 1; d /= 10)
        if (v >= d || n)
            b[n++] = '0' + v / d % 10;
    b[n++] = '0' + v % 10;
    b[n] = 0;
    return CORE_TEXT_DONE;
}

const struct core_param lk_params = {ID_A, ID_V, lk_get, lk_delta, lk_text};

const struct core_page lk_page = {49, "LOCK CHECK", {ID_A, ID_C}};
const struct core_page lk_fx_page = {50, "LOCK CHECK FX", {ID_K}};
const struct core_page lk_cv_page = {51, "LOCK CHECK CV", {ID_V}};
const struct core_view_page lk_view = {CORE_TRACK_SYNTH, CORE_KEY_AMP, 49, 0};
const struct core_view_page lk_fx_view = {CORE_TRACK_FX, CORE_KEY_LFO, 50, 0};
const struct core_view_page lk_cv_view = {CORE_TRACK_CV, CORE_KEY_LFO, 51, 0};

static void probe(void);

/* the record the emulator reads */
struct seen {
    char magic[4];
    int32_t blocks;
    int16_t a[6], c[6], slide_c[6];
    int16_t track_a, track_c, track_k, track_v;
    int32_t hits[4];                    /* A, C, K, V: blocks with a lock */
    int16_t last[4];                    /* their last lock seen, -1 none */
    int8_t routes[6], dests[6];         /* core_dst_voice's counts, voices 0..5 */
    struct core_dst first[6];           /* each voice's first modulation */
    int16_t macros;                     /* core_dst_macros' count */
    struct core_dst macro0;             /* and its first */
    uint16_t target[8];                 /* voice 0's Dest selectors' engine target slots */
};

volatile struct seen lk_seen = {{'L', 'K', 'C', 'K'}, .last = {-1, -1, -1, -1}};

static void note(int i, int v)
{
    if (v >= 0) {
        lk_seen.hits[i]++;
        lk_seen.last[i] = v;
    }
}

void lk_render_out(void)
{
    int v, ca = core_lock_code(STORE(lk_a)), cc = core_lock_code(STORE(lk_c));
    for (v = 0; v < 6; v++) {
        lk_seen.a[v] = core_lock_value_voice(v, ca);
        lk_seen.c[v] = core_lock_value_voice(v, cc);
        lk_seen.slide_c[v] = core_slide_delta_voice(v, cc);
    }
    lk_seen.track_a = core_lock_value(0, STORE(lk_a));
    lk_seen.track_c = core_lock_value(0, STORE(lk_c));
    lk_seen.track_k = core_lock_value(4, STORE(lk_k));
    lk_seen.track_v = core_lock_value(5, STORE(lk_v));
    note(0, lk_seen.track_a);
    note(1, lk_seen.track_c);
    note(2, lk_seen.track_k);
    note(3, lk_seen.track_v);
    for (v = 0; v < 6; v++) {
        int r, d;
        const struct core_dst *e = core_dst_voice(v, &r, &d);
        lk_seen.routes[v] = r;
        lk_seen.dests[v] = d;
        if (r + d)
            lk_seen.first[v] = e[0];
    }
    {
        int n;
        const struct core_dst *e = core_dst_macros(&n);
        lk_seen.macros = n;
        if (n)
            lk_seen.macro0 = e[0];
    }
    for (v = 0; v < 8; v++) {           /* stock's engine: the Dest list, the map, the base words */
        int32_t sel = ((const volatile int32_t *)0x4045c7a8)[v];
        lk_seen.target[v] = sel < 0 ? 0xffff
            : ((const volatile uint16_t *)0x80003e38)[((const volatile int16_t *)0x42878da0)[sel]];
    }
    lk_seen.blocks++;
    probe();
}

/* 1.1: the value read (core-a4mk2 0.5, part 3). Each block, after core's
 * snapshot, lk_ring keeps core's read of A on voice 0 in the engine's scale
 * beside stock's engine copy of the stock parameter lk_cfg.index on voice 0
 * (its slot by stock's map), for a block-by-block compare in the emulator;
 * lk_cost the read's cost in DMA timer ticks, one parameter and four at once.
 * lk_cfg.push, poked to 1, pushes track 1's sound to voice 0 from the UI, as
 * a trig does, so poked sound words reach the engine (with lk_cfg.voice 1..3 track
 * 2..4's sound to that voice, with 4
 * or 5, the kit's FX or CV block, and the compare is of K or V there). */
#define DTCN0 (*(const volatile uint32_t *)0xfc07000c)
#define RING 2048
#define SOUND_PUSH ((void (*)(void *, int, int))0x40100a04)   /* (sound, voice, track) */
#define ROUTE_PUSH ((void (*)(void *, int))0x40100baa)        /* (sound, voice) */

struct cfg {
    char magic[4];
    int16_t index;                      /* the stock parameter beside A */
    int8_t push, pushed;                /* 1: push track 1's sound to voice 0, 2: the stock parameter as a knob turn (then 0) */
    int16_t a_value, stock_value;       /* at the push, A's value and the stock parameter's word (-1: none) */
    int8_t locks, macro;                /* 1: lock both on track 1's steps; n + 1: push macro n */
    int8_t voice, pad;                  /* the voice compared: 0..3 with A, 4 (FX) with K, 5 (CV) with V */
};
struct cost {
    char magic[4];
    uint32_t one_min, one_max, one_sum, four_min, four_max, four_sum, n, period;
    int32_t values[4];                  /* the batch's last values */
    int32_t mismatches;                 /* the batch against the single reads */
    int8_t track_voice[6];              /* core_param_voice of tracks 1..6 */
    int16_t track_a;                    /* core_param_value_ex(track 1, A), the engine's scale */
};
volatile struct cfg lk_cfg = {{'L', 'K', 'C', 'F'}, 0x24, 0, 0, -1, -1, 0, 0, 0, 0};
volatile struct cost lk_cost = {{'L', 'K', 'C', 'T'}, 0xffffffffu, 0, 0, 0xffffffffu, 0, 0, 0, 0, {0}, 0, {0}, 0};
volatile uint32_t lk_ring_n;
volatile int16_t lk_ring[RING][2];
static int handles[4];

static void probe(void)
{
    static uint32_t last;
    uint32_t t0, t1, t2;
    int one, four[4], i, slot, v = lk_cfg.voice;
    int h = core_param_handle(v == 4 ? STORE(lk_k) : v == 5 ? STORE(lk_v) : STORE(lk_a));
    if (!handles[0]) {
        handles[0] = core_param_handle(STORE(lk_a));
        handles[1] = core_param_handle(STORE(lk_c));
        handles[2] = core_param_handle(STORE(lk_a));
        handles[3] = core_param_handle(STORE(lk_c));
    }
    if ((unsigned)v > 5)
        v = 0;
    t0 = DTCN0;
    core_param_values_voice(v, &h, 1, CORE_PARAM_ENGINE, &one);
    t1 = DTCN0;
    core_param_values_voice(0, handles, 4, CORE_PARAM_ENGINE, four);
    t2 = DTCN0;
    if (last)
        lk_cost.period = t0 - last;
    last = t0;
    for (i = 0; i < 4; i++)
        lk_cost.values[i] = four[i];
    if (v == 0 && (four[0] != one || four[2] != one))
        lk_cost.mismatches++;
    t2 -= t1;
    t1 -= t0;
    if (t1 < lk_cost.one_min) lk_cost.one_min = t1;
    if (t1 > lk_cost.one_max) lk_cost.one_max = t1;
    if (t2 < lk_cost.four_min) lk_cost.four_min = t2;
    if (t2 > lk_cost.four_max) lk_cost.four_max = t2;
    lk_cost.one_sum += t1;
    lk_cost.four_sum += t2;
    lk_cost.n++;
    for (i = 0; i < 6; i++)
        lk_cost.track_voice[i] = core_param_voice(i);
    lk_cost.track_a = core_param_value_ex(0, STORE(lk_a), CORE_PARAM_ENGINE);
    slot = ((const volatile int16_t *)0x42878da0)[0x100 * v + (lk_cfg.index & 0xff)];
    lk_ring[lk_ring_n % RING][0] = one;
    lk_ring[lk_ring_n % RING][1] = (unsigned)slot < 0x24c ? ((const volatile uint16_t *)0x80005af0)[slot] : -1;
    lk_ring_n++;
}

#define LOCK_PASTE ((int (*)(unsigned, int, int, int, int))0x400b51f6)   /* (wrapper, step, column, 8.8, fine) */
#define TRACK_WRAPPER ((unsigned (*)(unsigned, int))0x400a1f3c)
#define APP_PATTERN ((unsigned (*)(unsigned))0x400a5f4a)
#define KNOB_PUSH ((void (*)(int, int, int, int))0x401005b8)              /* (value, track, index, Dest) */
#define FX_PUSH ((void (*)(void *))0x40100d84)
#define CV_PUSH ((void (*)(void *))0x40100fb8)
#define PERF_PUSH ((void (*)(void *, int))0x401011e0)                     /* (entry, n) */

/* a lock on step s of track 1 for the compare, or -1 for none */
static int lock_of(int s)
{
    return s % 4 == 3 ? -1 : 4 + s * 37 % 120;
}

/* 1.2: MIDI (core-a4mk2 0.5, part 4). Each UI tick lk_midi keeps the four
 * fields as stored: A and C in tracks 1..4's sounds of the current kit, K
 * and V in the kit, so the emulator sees what an NRPN or a CC set. */
struct midi {
    char magic[4];
    uint32_t ticks;
    int16_t a[4], c[4], k, v;
};
volatile struct midi lk_midi = {{'L', 'K', 'M', 'I'}, 0, {0}, {0}, 0, 0};

static void midi_note(unsigned char *k)
{
    int t;
    for (t = 0; t < 4; t++) {
        lk_midi.a[t] = core_store_get(STORE(lk_a), k + 0x1c + 0x152 * t, 0);
        lk_midi.c[t] = core_store_get(STORE(lk_c), k + 0x1c + 0x152 * t, 0);
    }
    lk_midi.k = core_store_get(STORE(lk_k), k, 0);
    lk_midi.v = core_store_get(STORE(lk_v), k, 0);
    lk_midi.ticks++;
}

void lk_tick(void)
{
    unsigned char *k = kit(), *snd;
    unsigned short sr;
    unsigned w;
    int st, v, vc;
    if (!k)
        return;
    midi_note(k);
    vc = (unsigned)lk_cfg.voice < 4 ? lk_cfg.voice : 0;   /* a synth voice: its own track's sound */
    snd = k + 0x1c + 0x152 * vc;
    if (lk_cfg.push == 1 || lk_cfg.push == 2) {
        __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
        if (lk_cfg.a_value >= 0)
            core_store_set(STORE(lk_a), snd, 0, lk_cfg.a_value);
        if (lk_cfg.stock_value >= 0)
            *(volatile uint16_t *)(snd + 0x14 + 2 * (lk_cfg.index & 0xff)) = lk_cfg.stock_value;
        if (lk_cfg.voice == 4)
            FX_PUSH(k + 0x564);
        else if (lk_cfg.voice == 5)
            CV_PUSH(k + 0x668);
        else if (lk_cfg.push == 2 && lk_cfg.stock_value >= 0)
            KNOB_PUSH(lk_cfg.stock_value, vc, lk_cfg.index & 0xff, 0);   /* as a knob turn */
        else {
            SOUND_PUSH(snd, vc, vc);
            ROUTE_PUSH(snd, vc);
        }
        __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
        lk_cfg.push = 0;
        lk_cfg.pushed++;
    }
    if (lk_cfg.locks == 1 && (w = TRACK_WRAPPER(APP_PATTERN(APP()), 0)) != 0) {
        for (st = 0; st < 64; st++)
            if ((v = lock_of(st)) >= 0) {
                LOCK_PASTE(w, st, lk_cfg.index & 0xff, v << 8, 0);
                LOCK_PASTE(w, st, core_lock_code(STORE(lk_a)), v << 8, 0);
            }
        lk_cfg.locks = 2;
    }
    if (lk_cfg.macro > 0 && lk_cfg.macro <= 12) {
        __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
        PERF_PUSH(k + 0x764 + 0x26 * (lk_cfg.macro - 1), lk_cfg.macro - 1);
        __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
        lk_cfg.macro = 0;
    }
}

void lk_draw(void *bmp, void *ctrl)
{
    int v;
    (void)ctrl;
    FILLRECT(bmp, 0, 0, 127, 1, 0);
    if ((v = lk_seen.track_a) >= 0)
        FILLRECT(bmp, 0, 0, v / 2, 1, 1);
    FILLRECT(bmp, 0, 3, 127, 4, 0);
    if ((v = lk_seen.track_c) >= 0)
        FILLRECT(bmp, 0, 3, v / 8, 4, 1);
}
