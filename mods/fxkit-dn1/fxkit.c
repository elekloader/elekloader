/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fxkit: what the Digitone mk1's effect mods share (digitone-mk1/fxkit.h, docs/ADAPTING.md "fxkit").
 *
 * One effect at a time is active: fxkit's core_audio owner (exclusive, the voices muted) plays it, the inputs
 * through the mixer's input stage, the effect, and the mixer's output stage. Each block, at the render's entry, the
 * active effect gets its values: its own (fxkit keeps each effect's, by slot), the p-locks of the lock track's trig
 * that plays (ev_note_queue: the event, at the time the step plays) and its two LFOs, the stock LFO's arithmetic.
 * The p-locks are the pattern's: fxkit writes them into the lock track's lock table as the firmware's lock setter
 * does (0x40024236: the value, the step's count, the slot's used flag), with a lock trig on a step that has no
 * trig, so the stock saves, loads and plays them with the pattern.
 *
 * The UI task: fxt_tick, fx_activate, the setters and the pages. Interrupt level: fxt_queue, fxt_rin, fxt_render. */
#include <stdint.h>
#include "digitone-mk1/core3.h"
#include "digitone-mk1/fxkit.h"

#pragma GCC optimize ("Os")

typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
typedef uint16_t u16;
typedef uint8_t u8;

#define SLOTS   79
#define MAXFX   15
#define N       32                      /* frames a block */

extern const struct fx_effect *const fx_effects[];

/* ---- the effects and their values ---------------------------------------------------------------- */
struct ent {
    const struct fx_effect *e;
    u32 serial;                         /* changes to base (fx_serial) */
    s16 base[SLOTS];                    /* each slot's value, 8.8; -1: not one of e's */
};
static struct ent fx[MAXFX];
static int nfx;

static const char *const off_on[] = { "OFF", "ON" };
static const char *const mults[24] = { "X1", "X2", "X4", "X8", "X16", "X32", "X64", "X128", "X256", "X512", "X1K",
                                       "X2K", "1", "2", "4", "8", "16", "32", "64", "128", "256", "512", "1K", "2K" };
static const char *const waves[7] = { "TRI", "SIN", "SQR", "SAW", "EXP", "RMP", "RND" };
static const char *const modes[5] = { "FREE", "TRIG", "HOLD", "ONE", "HALF" };
#define FX_DEST 9                       /* a DEST: its value a slot, its text that knob's name */
#define FX_FINE 10                      /* SPD and DEP: FX_BIP in the stock's fine steps, up to 0x7ffe */

/* The mixer's knobs (MIXER, A-H) and the LFOs' (LFO 1, LFO 2), described as an effect's are */
static const struct fx_param mixer[8] = {
    { FX_M_INL, FX_KNOB, 0, 32, "IN L", "Input L", 0 },
    { FX_M_INR, FX_KNOB, 0, 32, "IN R", "Input R", 0 },
    { FX_M_PANL, FX_BIP, 0, 0, "LPAN", "L Pan", 0 },
    { FX_M_PANR, FX_BIP, 0, 127, "RPAN", "R Pan", 0 },
    { FX_M_MIX, FX_KNOB, 0, 127, "MIX", "Mix", 0 },
    { FX_M_BAL, FX_BIP, 0, 64, "BAL", "Balance", 0 },
    { FX_M_VOL, FX_KNOB, 0, 100, "VOL", "Volume", 0 },
    { FX_M_WIDTH, FX_KNOB, 0, 64, "WDTH", "Width", 0 },
};
static const struct fx_param byp = { FX_M_BYP, FX_OPT, 2, 0, "BYP", "Bypass", off_on };
static const struct fx_param lfo_p[8] = {   /* their slots from 0: + FX_L1 or FX_L2; the stock's defaults */
    { 0, FX_FINE, 0, 96, "SPD", "Speed", 0 },
    { 1, FX_OPT, 24, 5, "MULT", "Multiplier", mults },
    { 2, FX_BIP, 0, 64, "FADE", "Fade", 0 },
    { 3, FX_DEST, 0, 0, "DEST", "Destination", 0 },
    { 4, FX_OPT, 7, 0, "WAVE", "Waveform", waves },
    { 5, FX_KNOB, 0, 0, "SPH", "Start Phase", 0 },
    { 6, FX_OPT, 5, 0, "MODE", "Mode", modes },
    { 7, FX_FINE, 0, 64, "DEP", "Depth", 0 },
};

static int is_lfo(s32 k)
{
    return k >= FX_L1 && k < FX_L1 + 16;
}

static s32 vmax(s32 k)                  /* a slot's highest value: SPD's and DEP's fine steps reach 0x7ffe */
{
    s32 i = (k - FX_L1) & 7;
    return is_lfo(k) && (i == 0 || i == 7) ? 0x7ffe : 0x7f00;
}

static const struct fx_param *param_of(const struct fx_effect *e, s32 k, struct fx_param *tmp)
{
    const struct fx_param *p;
    int i;
    for (p = e->params; p && p->slot; p++)
        if (p->slot == k)
            return p;
    for (i = 0; i < 8; i++)
        if (mixer[i].slot == k)
            return (k == FX_M_MIX && (e->flags & FX_OWN_MIX)) ? 0 : &mixer[i];
    if (k == FX_M_BYP)
        return &byp;
    if (is_lfo(k)) {                    /* field by field: a struct copy would be a memcpy call */
        p = &lfo_p[(k - FX_L1) & 7];
        tmp->slot = (u8)k;
        tmp->kind = p->kind;
        tmp->n = p->n;
        tmp->def = p->def;
        tmp->name = p->name;
        tmp->lname = p->lname;
        tmp->opts = p->opts;
        return tmp;
    }
    return 0;
}

static s32 ent_of(const struct fx_effect *e)    /* its entry, made on first use; -1 when full */
{
    s32 i, k;
    struct fx_param t;
    for (i = 0; i < nfx; i++)
        if (fx[i].e == e)
            return i;
    if (!e || e->abi != FX_ABI || nfx >= MAXFX)
        return -1;
    i = nfx++;
    fx[i].e = e;
    for (k = 0; k < SLOTS; k++)
        fx[i].base[k] = -1;             /* (what init sets goes nowhere) */
    if (e->init)
        e->init();
    for (k = 1; k < SLOTS; k++) {
        const struct fx_param *p = param_of(e, k, &t);
        fx[i].base[k] = (s16)(!p ? -1 : p->def << 8);
    }
    return i;
}

static int is_mine(s32 a, s32 k)
{
    return a >= 0 && k > 0 && k < SLOTS && fx[a].base[k] != -1;
}

/* ---- the render ---------------------------------------------------------------------------------- */
static volatile s32 r_act = -1;         /* the active effect's entry, -1 none */
static volatile u8 lock_track = 3;      /* T4 */
static volatile u32 r_overload;         /* the CPU guard bypassed the active effect */
static volatile u32 r_blocks;           /* the render's blocks, 1500 a second: the pages' timers */

#define QLEN 8
struct lockev { u32 time; s16 n; u8 slot[SLOTS]; s16 val[SLOTS]; };
struct lfo { u32 ph, last_ph; s32 out, rnd, fade; u8 stopped, reset; };
static struct lockev q[QLEN];           /* the lock track's trigs queued, oldest first */
static volatile u8 qw, qr;
static s16 lock[SLOTS];                 /* the playing trig's locks, -1 none */
static volatile s16 val[SLOTS];         /* this block's values */
static struct lfo lfo[2];
static u8 trig, byp_now;
/* For test tools: this block's values, the effects' entries (an entry's size, its base's offset), the active one */
const volatile int16_t *const fxt_val = val;
const struct ent *const fxt_fx = fx;
const uint16_t fxt_layout[2] = { (uint16_t)sizeof(struct ent), (uint16_t)__builtin_offsetof(struct ent, base) };
const volatile int32_t *const fxt_act = &r_act;

static uint8_t *lock_row(s32 step);
static volatile u16 *step_word(s32 step);

/* ev_note_queue: a trig of the lock track, at the time it plays (interrupts masked). The first step's, as PLAY
 * starts, is not this way: the event made for it then carries no locks, and a lock trig there makes none, so
 * first_step queues it from the lock table at the render's first block. */
void fxt_queue(void *ev, uint32_t time)
{
    s32 n = 0, i, c;
    struct lockev *e;
    uint8_t *l;
    if (r_act < 0 || FW_EVENT_TRACK(ev) != lock_track || FW_EVENT_KIND(ev) != FW_EVENT_FIRE ||
        !(FW_EVENT_STEP(ev) & (FW_EVENT_NOTE_TRIG | FW_EVENT_LOCK_TRIG)))
        return;
    if ((u8)(qw - qr) >= QLEN)
        qr++;                           /* full (stopped, say): the oldest goes; the render is masked out */
    e = &q[qw % QLEN];
    e->time = time;
    if ((l = FW_EVENT_LOCKS(ev))) {
        c = FW_LOCKS_COUNT(l);
        for (i = 0; i < c && i < SLOTS; i++) {
            u32 k = FW_LOCK_SLOT(l, i);
            if (k < SLOTS) {
                e->slot[n] = (u8)k;
                e->val[n++] = FW_LOCK_VALUE(l, i);
            }
        }
    }
    e->n = (s16)n;
    qw++;
}

/* PLAY: the lock track's first step, from the lock table, when it has a trig (interrupt level) */
static void first_step(u32 now)
{
    uint8_t *row = lock_row(0);
    volatile u16 *w = step_word(0);
    struct lockev *e;
    s32 i, n = 0;
    if (!row || !w || !(*w & (FW_STEP_TRIG | FW_STEP_LOCK_TRIG)))
        return;
    if ((u8)(qw - qr) >= QLEN)
        qr++;
    e = &q[qw % QLEN];
    e->time = now;
    for (i = 1; i < SLOTS; i++) {
        s16 v = *(volatile s16 *)(row + 2 * i);
        if (v >= 0) {
            e->slot[n] = (u8)i;
            e->val[n++] = v;
        }
    }
    e->n = (s16)n;
    qw++;
}

/* EMAC-free fractional multiply, Q31 x Q31 -> Q31 (to within a few units) */
static s32 fmul(s32 a, s32 b)
{
    s32 ah = a >> 16, bh = b >> 16;
    u32 al = (u32)a & 0xffff, bl = (u32)b & 0xffff;
    return ((ah * bh) << 1) + ((ah * (s32)(bl >> 1)) >> 14) + (((s32)(al >> 1) * bh) >> 14);
}

static s32 exp2_fw(s32 x)               /* the firmware's 2^x (0x400fb7b4): x 8.24, -> Q30-ish */
{
    u32 d0 = (((u32)x << 7) | 0x80000000u) >> 1;
    s32 sq = fmul((s32)d0, (s32)d0), sh = -(x >> 24);
    u32 r = (u32)(fmul(sq, 0x55555556) * 2) + 0x55555556u;
    return sh >= 32 ? 0 : sh <= 0 ? (s32)r : (s32)(r >> sh);
}

static s32 fade_step(s32 fade)          /* a block's fade (0x400fe4e8) */
{
    s32 d = fade - 16384;
    if (!d)
        return 0x40000000;
    if (d > 0)
        return -fmul(exp2_fw((fade << 14) - 0x21000000), 0x1c200000);
    return fmul(exp2_fw((d << 14) - 0x01000000), 0x1c200000);
}

static s32 wave(int w, u32 p)           /* the stock waves (0x401d1588), p a whole turn in 2^32 */
{
    s32 d0, d1;
    switch (w) {
    case 0: {                           /* triangle */
        u32 x = p + 0x40000000u, m = (x & 0x80000000u) ? 0xffffffffu : 0;
        return (s32)(((x << 1) ^ m) ^ 0x80000000u);
    }
    case 1:                             /* sine, a parabola's */
        d0 = (s32)(p ^ 0x7fffffffu);
        d0 = (d0 < 0 ? d0 + 1 : d0) >> 1;
        d1 = fmul(d0 < 0 ? -d0 : d0, d0);
        d0 = ((d0 < 0 ? d0 + 1 : d0) >> 1) - d1;
        d1 = fmul(d0 < 0 ? -d0 : d0, 0x73333334) + 0x18cccccc;
        return fmul(d0, d1) << 6;
    case 2:                             /* square */
        return (p & 0x80000000u) ? (s32)0x80000000u : 0x7fffffff;
    case 3:                             /* saw */
        return (s32)(p ^ 0x7fffffffu);
    case 4:                             /* exponential */
        return exp2_fw(fmul((s32)(p >> 1), (s32)0xf3ffe8ebu) + 5909) - 0x80000;
    case 5:                             /* ramp */
        return (p & 0x80000000u) ? 0 : (s32)p;
    }
    return 0;
}

static u32 rnd_seed = 0x2545f491u;

#define PERIOD  0x5265c000u             /* a turn of the phase */
/* One block of an LFO, the stock's (0x400feaee): its eight slots in v */
static void lfo_block(struct lfo *l, const volatile s16 *v, s32 *mod, s32 *dest, int tr, s32 tempo)
{
    s32 m = (int8_t)(v[1] >> 8), T = tempo, w = (int8_t)(v[4] >> 8), mode = (u8)(v[6] >> 8), inc, out;
    u32 ph;
    int hold = 0;
    if (m < 0)
        m = 0;
    else if (m >= 12) {
        m = (m > 23 ? 23 : m) - 12;
        T = 14400;
    }
    if (T < 0 || w < 0)
        w = 0;
    else if (w > 6)
        w = 6;
    inc = (T * ((v[0] - 0x4000) * 2)) >> (11 - m);
    ph = l->ph + (u32)inc;
    if (mode == 4 && !l->reset && !l->stopped &&
        (s32)((l->ph + 0xd6cd2000u) ^ (ph + 0xd6cd2000u)) < 0) {
        l->out = w == 5 ? 0x7fffffff : 0;
        l->stopped = 1;
    }
    if (ph > 0x5265bfffu) {
        if (mode == 3 && !l->reset && !l->stopped) {
            l->out = (inc < 0 && w == 4) ? 0x7fffffff : 0;
            l->stopped = 1;
        }
        ph += inc < 0 ? PERIOD : (u32)-(s32)PERIOD;
    }
    if (mode < 2)
        l->stopped = 0;
    l->reset = 0;
    l->fade += fade_step(v[2]);
    if (tr) {
        if (mode == 1 || mode == 3 || mode == 4) {
            ph = w == 6 ? 0 : (u32)(((s32)(u16)v[5] * (s32)(PERIOD >> 15)) & 0x7fffffff) % PERIOD;
            l->fade = v[2] > 16383 ? 0x3fffffff : 0;
            l->stopped = 0;
            l->reset = 1;
        } else if (mode == 0) {
            l->fade = v[2] > 16383 ? 0x3fffffff : 0;
        } else if (mode == 2) {
            l->stopped = 0;
            hold = 1;
        }
    }
    l->ph = ph;
    if (l->fade < 0)
        l->fade = 0;
    else if (l->fade > 0x3fffffff)
        l->fade = 0x3fffffff;
    if (!l->stopped || hold) {
        if (w == 6) {                   /* random: a new value each sixteenth of a turn */
            u32 d = ph - l->last_ph;
            if ((s32)d < 0)
                d = (u32)-(s32)d;
            if (d > 86399999u || (mode != 0 && tr)) {
                l->last_ph = ph;
                rnd_seed = rnd_seed * 1664525u + 1013904223u;
                l->rnd = (s32)rnd_seed;
            }
            out = l->rnd;
        } else {
            out = wave(w, (u32)fmul((s32)ph, 0x636ba875) << 2);
        }
        l->out = fmul(l->fade, out) * 2;
        if (mode == 2)
            l->stopped = 1;             /* HOLD: the value of the trig, held */
    }
    *dest = (int8_t)(v[3] >> 8);
    *mod = fmul((v[7] - 0x4000) * 2, l->out);
}

/* This block's values of effect a: its own, the playing trig's locks, the LFOs on them */
static void values_block(s32 a, s32 now, int playing, s32 tempo)
{
    const s16 *base = fx[a].base;
    s32 k, i, mod, dest;
    trig = 0;
    if (!playing) {                     /* stopped: the effect's own values; what was queued before the stop goes,
                                         * the first step's (PLAY queues it as it starts) stays */
        for (k = 0; k < SLOTS; k++)
            lock[k] = -1;
        while (qr != qw && (s32)(now - (s32)q[qr % QLEN].time) >= 0)
            qr++;
    }
    while (playing && qr != qw && (s32)(now - (s32)q[qr % QLEN].time) >= 0) {
        const struct lockev *e = &q[qr % QLEN];
        for (k = 0; k < SLOTS; k++)
            lock[k] = -1;
        for (i = 0; i < e->n; i++)
            if (base[e->slot[i]] != -1)     /* the effect's slots only */
                lock[e->slot[i]] = e->val[i];
        trig = 1;
        qr++;
    }
    for (k = 0; k < SLOTS; k++)
        val[k] = lock[k] != -1 ? lock[k] : base[k];
    for (i = 0; i < 2; i++) {
        lfo_block(&lfo[i], &val[FX_L1 + 8 * i], &mod, &dest, trig, tempo);
        if (dest > 0 && dest < SLOTS && base[dest] != -1 && !is_lfo(dest) && mod) {
            s32 v = val[dest] + mod;
            val[dest] = (s16)(v < 0 ? 0 : v > 0x7f00 ? 0x7f00 : v);
        }
    }
}

/* The mixer's curves */
static const s16 qsin[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793, 12539,
    13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621,
    29956, 30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678,
    32728, 32757, 32767,
};

static s32 level(s32 v8)                /* 0-127 (8.8) -> a gain, Q12: (v / 100)^2, 100 unity */
{
    s32 v = v8 >> 4;                    /* 12.4 */
    return (v * v) / 625;               /* (v/16)^2 * 4096 / 10000 */
}

static s32 in_level(s32 v8)             /* an input's, as the stock mixer's (0x40096efe and its x16 at 0x400973ee):
                                         * 16 (v / 127)^2, Q8: 32 unity, 127 +24 dB */
{
    return (v8 * v8) / 258064;          /* v8^2 * 16 * 256 / (127 * 256)^2 */
}

static void pan(s32 v8, s32 *l, s32 *r)  /* 0 left .. 64 centre .. 127 right -> equal-power gains, Q15 */
{
    s32 i = (v8 >> 8) * 64 / 127;
    *l = qsin[64 - i];
    *r = qsin[i];
}

static inline s32 sat16(s32 v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

static s32 buf_a[2 * N], buf_b[2 * N], buf_x[2 * N];

static u32 r_in, r_prev, r_over;
volatile u32 fx_load;

/* The render's entry: the CPU guard's clock, and the active effect's values for this block (its values() then,
 * whether it plays in fxkit's audio or as an insert of its own) */
void fxt_rin(void)
{
    static u8 was_playing;
    const struct fx_effect *e;
    s32 a = r_act;
    int playing = fw_transport != 0;
    r_blocks++;
    r_prev = r_in;
    r_in = *(volatile u32 *)0xFC07000Cu;    /* DMA timer 0's counter */
    if (a >= 0 && playing && !was_playing)
        first_step(fw_timeline);
    was_playing = (u8)playing;
    if (a < 0)
        return;
    e = fx[a].e;
    values_block(a, (s32)fw_timeline, playing, fw_tempo);
    byp_now = (u8)(val[FX_M_BYP] >= 128 || r_overload);
    if (e->values && (!byp_now || (e->flags & FX_OWN_BYPASS)))
        e->values((const int16_t *)val, byp_now ? FX_V_BYPASS : 0);
}

/* The render's share; over 93% for 0.2 s: the active effect bypassed */
void fxt_rout(void)
{
    u32 per = r_in - r_prev, busy = *(volatile u32 *)0xFC07000Cu - r_in, l;
    if (per < 1000 || per > 400000)
        return;
    l = busy * 1000u / per;
    fx_load = l;
    if (l > 930 && r_act >= 0) {
        if (++r_over >= 300) {
            r_overload = 1;
            r_over = 0;
        }
    } else if (r_over) {
        r_over--;
    }
}

/* The owner's render: the inputs through the mixer and the active effect */
void fxt_render(int32_t *out, const int32_t *in)
{
    const struct fx_effect *e;
    s32 a = r_act, gl, gr, ll, lr, rl, rr, i;
    if (a < 0) {
        for (i = 0; i < 2 * N; i++)
            out[i] = in[i];
        return;
    }
    e = fx[a].e;
    gl = in_level(val[FX_M_INL]);
    gr = in_level(val[FX_M_INR]);
    pan(val[FX_M_PANL], &ll, &lr);
    pan(val[FX_M_PANR], &rl, &rr);
    for (i = 0; i < 2 * N; i += 2) {    /* the inputs into the effect: 19 bits through the gain, then 16 */
        s32 L = sat16(((in[i] >> 13) * gl) >> 11), R = sat16(((in[i + 1] >> 13) * gr) >> 11);
        buf_x[i] = sat16(((L * ll) >> 15) + ((R * rl) >> 15));
        buf_x[i + 1] = sat16(((L * lr) >> 15) + ((R * rr) >> 15));
    }
    if (e->render && !(byp_now && !(e->flags & FX_OWN_BYPASS))) {
        s32 mix = (e->flags & FX_OWN_MIX) ? 127 : val[FX_M_MIX] >> 8, md, mw;
        s32 w = val[FX_M_WIDTH] >> 8, bal = val[FX_M_BAL] >> 8, vol = level(val[FX_M_VOL]), bl, br;
        s32 mi = mix * 64 / 127;
        md = qsin[64 - mi];
        mw = qsin[mi];
        bl = bal <= 64 ? 32767 : (127 - bal) * 32767 / 63;
        br = bal >= 64 ? 32767 : bal * 32767 / 64;
        for (i = 0; i < 2 * N; i++)
            buf_a[i] = buf_x[i] << 16;
        e->render(buf_b, buf_a);
        for (i = 0; i < 2 * N; i += 2) {
            s32 L = buf_b[i] >> 16, R = buf_b[i + 1] >> 16, m, sd;
            if (mix < 127) {
                L = ((buf_x[i] * md) >> 15) + ((L * mw) >> 15);
                R = ((buf_x[i + 1] * md) >> 15) + ((R * mw) >> 15);
            }
            if (w != 64) {
                m = (L + R) >> 1;
                sd = (((L - R) >> 1) * w) >> 6;
                L = m + sd;
                R = m - sd;
            }
            L = (((L * bl) >> 15) * vol) >> 12;
            R = (((R * br) >> 15) * vol) >> 12;
            buf_x[i] = sat16(L);
            buf_x[i + 1] = sat16(R);
        }
    }
    for (i = 0; i < 2 * N; i++)
        out[i] = buf_x[i] << 16;
}

struct core_audio_owner fxt_owner = { 0, fxt_render, CORE_AUDIO_MUTE_VOICES };

/* ---- the UI task: the active effect and its values ------------------------------------------------ */
#define SETQ 16
static struct { volatile u8 fx, slot; volatile s16 val; } setq[SETQ];
static volatile u8 sq_w, sq_r;

void fx_activate(const struct fx_effect *e)
{
    s32 a = ent_of(e), k;
    if (a < 0 || a == r_act)
        return;
    r_act = -1;                         /* the render leaves it be meanwhile */
    for (k = 0; k < SLOTS; k++)
        lock[k] = -1;
    qr = qw;
    lfo[0].ph = lfo[1].ph = 0;
    lfo[0].fade = lfo[1].fade = 0x3fffffff;
    r_overload = 0;
    r_over = 0;
    r_act = a;
    fxt_owner.on = 1;
}

void fx_deactivate(const struct fx_effect *e)
{
    s32 a = ent_of(e);
    if (a >= 0 && a == r_act) {
        fxt_owner.on = 0;
        r_act = -1;
    }
}

const struct fx_effect *fx_active(void)
{
    s32 a = r_act;
    return a >= 0 ? fx[a].e : 0;
}

void fx_chain(const struct fx_effect *e, int32_t in_chain)
{
    s32 a = ent_of(e);
    if (a >= 0 && a == r_act)
        fxt_owner.on = in_chain != 0;
}

int32_t fx_get(const struct fx_effect *e, int32_t slot)
{
    s32 a = ent_of(e);
    return a < 0 || slot <= 0 || slot >= SLOTS ? -1 : fx[a].base[slot];
}

void fx_set(const struct fx_effect *e, int32_t slot, int32_t value)
{
    s32 a = ent_of(e);
    if (!is_mine(a, slot))
        return;
    value = value < 0 ? 0 : value > vmax(slot) ? vmax(slot) : value;
    if (fx[a].base[slot] != value) {
        fx[a].base[slot] = (s16)value;
        fx[a].serial++;
    }
    if (slot == FX_M_BYP && a == r_act)
        r_overload = 0;
}

void fx_set_later(const struct fx_effect *e, int32_t slot, int32_t value)
{
    u8 w = sq_w;
    s32 a = ent_of(e);
    if (a < 0 || (u8)(w - sq_r) >= SETQ)
        return;
    setq[w % SETQ].fx = (u8)a;
    setq[w % SETQ].slot = (u8)slot;
    setq[w % SETQ].val = (s16)value;
    sq_w = (u8)(w + 1);
}

int32_t fx_overload(void)
{
    return r_overload ? 1 : 0;
}

/* ---- the p-locks: the lock track's lock table in the active pattern -------------------------------- */
#define LOCKS        0x1e80             /* the pattern's lock tables: a track's 10319 bytes, a step's row 160 */
#define TRACK_LOCKS  10319
#define STEP_LOCKS   160
#define LOCK_COUNT   0x9e               /* a row's count of locks, a u16 */
#define LOCK_USED    0x2800             /* a byte a slot: the track has locks of it */
#define PAT_SETTINGS 0x160f8            /* the pattern's settings: +0x14 its length (u16), +0x19 1 per track */
#define KEY_YES      13
#define KEY_NO       14
#define KEY_UP       15
#define KEY_DOWN     16
#define KEY_PAGE     19
#define KEY_STEP1    26
static u8 lock_on, lock_pg;
static s32 held = -1;

static uint8_t *lock_table(void)
{
    uint8_t *pat = fw_pattern;
    return pat ? pat + LOCKS + TRACK_LOCKS * lock_track : 0;
}

static uint8_t *lock_row(s32 step)
{
    uint8_t *t = lock_table();
    return t && step >= 0 && step < 64 ? t + STEP_LOCKS * step : 0;
}

static volatile u16 *step_word(s32 step)
{
    uint8_t *pat = fw_pattern;
    return pat && step >= 0 && step < 64 ? &FW_STEP(FW_TRACK(pat, lock_track), step) : 0;
}

static void recount(uint8_t *row)       /* the row's count of locks (0x40023ed2) */
{
    s32 k, n = 0;
    for (k = 0; k < SLOTS; k++)
        if (*(volatile s16 *)(row + 2 * k) >= 0)
            n++;
    *(volatile u16 *)(row + LOCK_COUNT) = (u16)n;
}

static s32 lock_get(s32 step, s32 slot)
{
    uint8_t *row = lock_row(step);
    return row && slot > 0 && slot < SLOTS ? *(volatile s16 *)(row + 2 * slot) : -1;
}

static void lock_set(s32 step, s32 slot, s32 value)
{
    uint8_t *row = lock_row(step), *t = lock_table();
    volatile u16 *w = step_word(step);
    if (!row || !w || slot <= 0 || slot >= SLOTS)
        return;
    *(volatile s16 *)(row + 2 * slot) = (s16)value;
    recount(row);
    t[LOCK_USED + slot] = 1;            /* (0x400241f6) */
    if (!(*w & (FW_STEP_TRIG | FW_STEP_LOCK_TRIG)))
        *w = (u16)(*w | FW_STEP_LOCK_TRIG);     /* a lock trig carries the step's locks */
}

static void lock_clear(s32 a, s32 step)  /* the step's locks of effect a; a lock trig left with none goes */
{
    uint8_t *row = lock_row(step);
    volatile u16 *w = step_word(step);
    s32 k;
    if (!row || !w)
        return;
    for (k = 1; k < SLOTS; k++)
        if (is_mine(a, k))
            *(volatile s16 *)(row + 2 * k) = -1;
    recount(row);
    if (!*(volatile u16 *)(row + LOCK_COUNT) && (*w & FW_STEP_LOCK_TRIG) && !(*w & FW_STEP_TRIG))
        *w = (u16)(*w & ~FW_STEP_LOCK_TRIG);
}

int32_t fx_lock_pages(void)
{
    uint8_t *pat = fw_pattern, *ps;
    s32 len;
    if (!pat)
        return 1;
    ps = pat + PAT_SETTINGS;
    len = ps[0x19] == 1 ? FW_TRACK_LENGTH(FW_TRACK(pat, lock_track)) : (ps[0x14] << 8 | ps[0x15]);
    if (len < 1 || len > 64)
        len = 16;
    return (len + 15) / 16;
}

uint32_t fx_lock_mask(const struct fx_effect *e, int32_t page)
{
    s32 a = ent_of(e), s, k;
    u32 m = 0;
    for (s = 0; s < 16; s++) {
        uint8_t *row = lock_row(16 * page + s);
        if (!row || !*(volatile u16 *)(row + LOCK_COUNT))
            continue;
        for (k = 1; k < SLOTS; k++)
            if (is_mine(a, k) && *(volatile s16 *)(row + 2 * k) >= 0) {
                m |= 1u << s;
                break;
            }
    }
    return m;
}

int32_t fx_lock_mode(void)
{
    return lock_on;
}

void fx_lock_mode_set(int32_t on)
{
    lock_on = (u8)(on != 0);
    held = -1;
    if (lock_pg >= fx_lock_pages())
        lock_pg = 0;
}

int32_t fx_lock_page(void)
{
    return lock_pg;
}

int32_t fx_held_step(void)
{
    return lock_on ? held : -1;
}

int32_t fx_lock_key(const struct fx_effect *e, uint32_t key, uint32_t flags, int32_t func)
{
    int press = (flags & 1) && !(flags & 8), release = (flags & 0x10) != 0;
    if (!lock_on)
        return 0;
    if (key >= KEY_STEP1 && key < KEY_STEP1 + 16) {
        s32 step = 16 * lock_pg + (s32)(key - KEY_STEP1);
        if (release) {
            if (held == step)
                held = -1;
        } else if (press) {
            if (func)
                lock_clear(ent_of(e), step);
            else
                held = step;
        }
        return 1;
    }
    if (key == KEY_PAGE) {
        if (press) {
            lock_pg = (u8)((lock_pg + 1) % fx_lock_pages());
            held = -1;
        }
        return 1;
    }
    return 0;
}

int32_t fx_lock_track(void)
{
    return lock_track;
}

void fx_lock_track_set(int32_t track)
{
    if (track >= 0 && track < 4 && track != lock_track) {
        lock_track = (u8)track;
        qr = qw;
        held = -1;
    }
}

int32_t fx_view(int32_t slot, int32_t value)
{
    s32 v = lock_on && held >= 0 ? lock_get(held, slot) : -1;
    return v >= 0 ? v : value;
}

void fx_turned(const struct fx_effect *e, int32_t slot, int32_t value)
{
    value = value < 0 ? 0 : value > vmax(slot) ? vmax(slot) : value;
    if (lock_on && held >= 0) {
        if (is_mine(ent_of(e), slot))
            lock_set(held, slot, value);
    } else {
        fx_set(e, slot, value);
    }
}

/* ---- text, and the MIXER and LFO pages ----------------------------------------------------------- */
/* The stock LFO page's own pieces, from a parameter's UI record (fw_uirecs, its id by fw_slot_ids): its value's
 * text (+0x14: f(value, buf)) and its knob's graphic (the delegate at +0x24, its call at +0x30: f(the delegate,
 * value, bmp, x, y), x and y the cell's lower left), as the stock page calls them */
typedef void (*fmt_fn)(int32_t value, char *buf);
typedef void (*gfx_fn)(void *self, int32_t value, void *bmp, int32_t x, int32_t y);

static uint8_t *uirec(s32 k)
{
    u32 id = is_lfo(k) ? fw_slot_ids[k] : 0;
    return id && id < 182 ? fw_uirecs + 84 * id : 0;
}

static int itoa_(char *b, s32 v)
{
    char t[12];
    int n = 0, i = 0;
    if (v < 0) {
        b[i++] = '-';
        v = -v;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        b[i++] = t[--n];
    b[i] = 0;
    return i;
}

static void copy_n(char *b, const char *s, int n)
{
    int i;
    for (i = 0; i < n && s[i]; i++)
        b[i] = s[i];
    b[i] = 0;
}

static const char *group(const struct fx_effect *e, s32 k, char *buf, int n)   /* DEST's group: MIX, or the
                                                                                 * effect's name but its DIGI */
{
    const char *s = e->name;
    int i;
    if (k >= FX_M_INL && k <= FX_M_BYP)
        return "MIX";
    if ((s[0] | 32) == 'd' && (s[1] | 32) == 'i' && (s[2] | 32) == 'g' && (s[3] | 32) == 'i' && s[4])
        s += 4;
    for (i = 0; i < n && s[i]; i++)
        buf[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    buf[i] = 0;
    return buf;
}

void fx_text(const struct fx_effect *e, int32_t slot, int32_t value, char *buf)
{
    struct fx_param t, t2;
    const struct fx_param *p = param_of(e, slot, &t), *d;
    uint8_t *ui;
    s32 v = value >> 8;
    buf[0] = 0;
    if (!p)
        return;
    if (p->kind == FX_DEST) {
        d = v > 0 && !is_lfo(v) ? param_of(e, v, &t2) : 0;
        copy_n(buf, d ? d->name : "---", 5);
    } else if ((ui = uirec(slot)) && *(fmt_fn *)(ui + 0x14)) {
        char s[24];                     /* the stock's text: SPD's "10.81" */
        (*(fmt_fn *)(ui + 0x14))(value, s);
        copy_n(buf, s, 7);
    } else if (p->kind == FX_OPT) {
        if (p->opts && v >= 0 && v < p->n)
            copy_n(buf, p->opts[v], 5);
        else
            itoa_(buf, v);
    } else if (p->kind == FX_BIP) {
        if (v > 64)
            buf[0] = '+', itoa_(buf + 1, v - 64);
        else
            itoa_(buf, v - 64);
    } else {
        itoa_(buf, v);
    }
}

/* The pages' own state: a turned knob's value under it (until shown_end), DEST's list, the title's "Confirmed!"
 * (until confirm_end), timed in the render's blocks (1500 a second) */
#define SHOW_BLOCKS    6000             /* 4 s, as the stock pages show a turned knob's value */
#define CONFIRM_BLOCKS 1500
static u32 shown_end, confirm_end;
static u8 shown_k;
static u8 list_pg, list_n, list_sel, list_top;    /* list_pg: the LFO page whose DEST list is open, 0 none */
static u8 list[SLOTS + 1];

static int live(u32 end)
{
    return (s32)(end - r_blocks) > 0;
}

const char *fx_page_title(int32_t page)
{
    if (page && live(confirm_end))
        return "Confirmed!";
    return page == 0 ? "Mixer" : page == 1 ? "LFO (1/2)" : "LFO (2/2)";
}

int32_t fx_page_slot(int32_t page, int32_t knob)
{
    if (knob < 0 || knob > 7 || page < 0 || page >= FX_PAGES)
        return 0;
    return page == 0 ? mixer[knob].slot : (page == 1 ? FX_L1 : FX_L2) + knob;
}

const char *fx_page_label(const struct fx_effect *e, int32_t page, int32_t knob)
{
    struct fx_param t;
    s32 k = fx_page_slot(page, knob);
    const struct fx_param *p = k ? param_of(e, k, &t) : 0;
    return p ? p->name : "";
}

/* DEST's list: none, the effect's knobs, the mixer's -> its length */
static s32 dest_list(s32 a, u8 *l)
{
    s32 n = 0, k;
    l[n++] = 0;
    for (k = 1; k < SLOTS; k++)
        if (is_mine(a, k) && !is_lfo(k) && k != FX_M_BYP)
            l[n++] = (u8)k;
    return n;
}

static s32 list_index(const u8 *l, s32 n, s32 cur)
{
    s32 i;
    for (i = 0; i < n && l[i] != cur; i++)
        ;
    return i == n ? 0 : i;
}

static s32 dest_step(s32 a, s32 cur, s32 d)
{
    u8 l[SLOTS + 1];
    s32 n = dest_list(a, l), i = list_index(l, n, cur) + d;
    return l[i < 0 ? 0 : i >= n ? n - 1 : i];
}

static s32 fine_step(s32 d)             /* SPD's and DEP's turn of d detents, as the stock's (measured): 6/256 a
                                         * detent turned slowly, 3 d^2 / 256 when fast */
{
    s32 a = d < 0 ? -d : d;
    return 3 * d * (a < 2 ? 2 : a);
}

void fx_knob_turn(const struct fx_effect *e, int32_t k, int32_t detents)
{
    struct fx_param t;
    const struct fx_param *p = k > 0 && k < SLOTS ? param_of(e, k, &t) : 0;
    s32 v;
    if (!p || !detents)
        return;
    v = fx_view(k, fx_get(e, k));
    if (p->kind == FX_FINE) {
        fx_turned(e, k, v + fine_step(detents));
        return;
    }
    v >>= 8;
    if (p->kind == FX_DEST)
        v = dest_step(ent_of(e), v, detents > 0 ? 1 : -1);
    else if (p->kind == FX_OPT)
        v += detents > 0 ? 1 : -1, v = v < 0 ? 0 : v >= p->n ? p->n - 1 : v;
    else
        v += detents, v = v < 0 ? 0 : v > 127 ? 127 : v;
    fx_turned(e, k, v << 8);
}

void fx_knob_push(const struct fx_effect *e, int32_t k)
{
    struct fx_param t;
    const struct fx_param *p = k > 0 && k < SLOTS ? param_of(e, k, &t) : 0;
    s32 v;
    if (!p)
        return;
    if (p->kind == FX_OPT)
        v = ((fx_view(k, fx_get(e, k)) >> 8) + 1) % p->n;
    else
        v = p->def;
    fx_turned(e, k, v << 8);
}

static void list_move(s32 d)
{
    s32 s = list_sel + d;
    list_sel = (u8)(s < 0 ? 0 : s >= list_n ? list_n - 1 : s);
    if (list_sel < list_top)
        list_top = list_sel;
    else if (list_sel > list_top + 6)
        list_top = (u8)(list_sel - 6);
}

void fx_page_turn(const struct fx_effect *e, int32_t page, int32_t knob, int32_t detents)
{
    s32 k = fx_page_slot(page, knob);
    if (page && knob == 3 && !(lock_on && held >= 0)) {     /* DEST: its list, as the stock's (a held step's
                                                               lock steps it at once) */
        if (list_pg != page) {
            list_n = (u8)dest_list(ent_of(e), list);
            list_sel = (u8)list_index(list, list_n, fx_get(e, k) >> 8);
            list_top = list_sel > 3 ? (u8)(list_sel - 3) : 0;
            if (list_top + 7 > list_n)
                list_top = list_n > 7 ? (u8)(list_n - 7) : 0;
            list_pg = (u8)page;
        } else {
            list_move(detents);
        }
        shown_end = r_blocks;
        return;
    }
    list_pg = 0;
    fx_knob_turn(e, k, detents);
    shown_k = (u8)k;
    shown_end = r_blocks + SHOW_BLOCKS;
}

void fx_page_push(const struct fx_effect *e, int32_t page, int32_t knob)
{
    s32 k = fx_page_slot(page, knob);
    list_pg = 0;
    if (page)                           /* the LFO pages: a push does nothing, as on the stock's */
        return;
    fx_knob_push(e, k);
    shown_k = (u8)k;
    shown_end = r_blocks + SHOW_BLOCKS;
}

int32_t fx_page_key(const struct fx_effect *e, int32_t page, uint32_t key, uint32_t flags)
{
    int press = (flags & 1) && !(flags & 8);
    if (!list_pg)
        return 0;
    if (list_pg != page || (key != KEY_YES && key != KEY_NO && key != KEY_UP && key != KEY_DOWN)) {
        if (press)
            list_pg = 0;                /* another key closes the list, and is the effect's */
        return 0;
    }
    if (press) {
        if (key == KEY_YES) {
            fx_turned(e, fx_page_slot(page, 3), list[list_sel] << 8);
            confirm_end = r_blocks + CONFIRM_BLOCKS;
            list_pg = 0;
        } else if (key == KEY_NO) {
            list_pg = 0;
        } else {
            list_move(key == KEY_UP ? -1 : 1);
        }
    }
    return 1;
}

uint32_t fx_page_state(void)
{
    return (u32)(live(shown_end) | shown_k << 1 | list_pg << 8 | list_sel << 10 | list_top << 18
                 | live(confirm_end) << 26);
}

/* Drawing, as the effects' own pages are (the stock FLTR, AMP and LFO pages' layout) */
typedef void (*rect_fn)(void *bmp, int x0, int y0, int x1, int y1, int colour);
typedef void (*text_fn)(void *bmp, const void *font, int x, int y, int maxlen, const char *fmt, ...);
typedef void (*box_fn)(void *bmp, const void *font, int x, int y, int align, int deco, const char *tmpl,
                       const char *fmt, ...);
#define FILLRECT  ((rect_fn)fw_fillrect)
#define FRAMERECT ((rect_fn)fw_framerect)
#define TEXTF     ((text_fn)fw_textf)
#define TEXTBOX   ((box_fn)fw_textbox)
#define FONT5     ((const void *)fw_font5)
#define FONT_LBL  ((const void *)fw_font_label)

static const u8 col_x[4] = { 33, 59, 86, 112 };
#define ROW_Y(r)    ((r) ? 15 : 42)
#define LBL_Y(r)    ((r) ? 0 : 27)
static const signed char needle[29][2] = {
    {-4, -4}, {-5, -3}, {-5, -3}, {-6, -2}, {-6, -1}, {-6, 0}, {-6, 1}, {-6, 2}, {-5, 3}, {-4, 4}, {-4, 5},
    {-3, 5}, {-2, 6}, {-1, 6}, {0, 6}, {1, 6}, {2, 6}, {3, 5}, {4, 5}, {4, 4}, {5, 3}, {6, 2}, {6, 1}, {6, 0},
    {6, -1}, {6, -2}, {5, -3}, {5, -3}, {4, -4} };

static void px(void *bmp, int x, int y, int c)
{
    FILLRECT(bmp, x, y, x, y, c);
}

static void rframe(void *bmp, int x0, int y0, int x1, int y1)
{
    FRAMERECT(bmp, x0, y0, x1, y1, 1);
    px(bmp, x0, y0, 0);
    px(bmp, x1, y0, 0);
    px(bmp, x0, y1, 0);
    px(bmp, x1, y1, 0);
}

static void line(void *bmp, int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        px(bmp, x0, y0, 1);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int text_w(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n ? 5 * n - 1 : 0;
}

static void small_text(void *bmp, int cx, int cy, const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    TEXTF(bmp, FONT5, cx - (4 * n - 1) / 2, cy - 2, -1, "%s", s);
}

static void knob(void *bmp, int cx, int cy, int v, int bipolar)
{
    int i, k = (v * 28 + 63) / 127;
    FILLRECT(bmp, cx - 2, cy + 8, cx + 2, cy + 8, 1);
    FILLRECT(bmp, cx - 2, cy - 8, cx + 2, cy - 8, 1);
    FILLRECT(bmp, cx - 8, cy - 2, cx - 8, cy + 2, 1);
    FILLRECT(bmp, cx + 8, cy - 2, cx + 8, cy + 2, 1);
    for (i = -1; i <= 1; i += 2) {
        FILLRECT(bmp, cx + 3 * i, cy + 7, cx + 4 * i, cy + 7, 1);
        FILLRECT(bmp, cx + 3 * i, cy - 7, cx + 4 * i, cy - 7, 1);
        FILLRECT(bmp, cx + 7 * i, cy + 3, cx + 7 * i, cy + 4, 1);
        FILLRECT(bmp, cx + 7 * i, cy - 4, cx + 7 * i, cy - 3, 1);
        px(bmp, cx + 5 * i, cy + 6, 1);
        px(bmp, cx + 5 * i, cy - 6, 1);
        px(bmp, cx + 6 * i, cy + 5, 1);
        px(bmp, cx + 6 * i, cy - 5, 1);
    }
    k = k < 0 ? 0 : k > 28 ? 28 : k;
    line(bmp, cx, cy, cx + needle[k][0], cy + needle[k][1]);
    if (bipolar) {
        FILLRECT(bmp, cx - 10, cy - 7, cx - 8, cy - 7, 1);
        FILLRECT(bmp, cx + 8, cy - 7, cx + 10, cy - 7, 1);
        FILLRECT(bmp, cx + 9, cy - 8, cx + 9, cy - 6, 1);
    }
}

/* A control's label, or its value for a while after a turn, as the stock pages show it */
static const char *label_of(const struct fx_effect *e, const struct fx_param *p, s32 k, s32 v, char *buf)
{
    if (!live(shown_end) || k != shown_k)
        return p->name;
    fx_text(e, k, v, buf);
    return buf;
}

void fx_draw_controls(const struct fx_effect *e, void *bmp, const uint8_t *slots)
{
    char buf[8], vb[8];
    s32 i;
    for (i = 0; i < 8; i++) {
        struct fx_param t;
        s32 k = slots[i], cx = col_x[i & 3], r = i >> 2, cy = ROW_Y(r), v;
        const struct fx_param *p = k > 0 && k < SLOTS ? param_of(e, k, &t) : 0;
        const char *l;
        if (!p)
            continue;
        v = fx_view(k, fx_get(e, k));
        if (p->kind == FX_OPT || p->kind == FX_DEST) {
            fx_text(e, k, v, buf);
            rframe(bmp, cx - 8, cy - 8, cx + 8, cy + 8);
            small_text(bmp, cx, cy, buf);
        } else {
            knob(bmp, cx, cy, v >> 8, p->kind == FX_BIP);
        }
        l = label_of(e, p, k, v, vb);
        TEXTF(bmp, FONT_LBL, cx - text_w(l) / 2, LBL_Y(r), -1, "%s", l);
    }
}

/* The stock LFO page: its cells' lower left corners (the stock's coordinates) */
static const u8 lfo_x[4] = { 24, 50, 76, 102 };
#define LFO_Y(r)    ((r) ? 7 : 34)

static void dest_box(const struct fx_effect *e, void *bmp, s32 v, s32 x, s32 y)    /* as the stock's (0x40089a82) */
{
    struct fx_param t;
    char g[8];
    s32 k = v >> 8;
    const struct fx_param *p = k > 0 && !is_lfo(k) ? param_of(e, k, &t) : 0;
    const char *gs = p ? group(e, k, g, 5) : "", *ns = p ? p->name : "---";
    rframe(bmp, x - 1, y, x + 18, y + 16);
    TEXTBOX(bmp, FONT5, x + 8, y + 10, 1, 0, gs, "%s", gs);
    TEXTBOX(bmp, FONT5, x + 8, y + 2, 1, 0, ns, "%s", ns);
}

/* WAVE and SPH as the stock LFO page draws them (one picture over both cells): between braces, a turn of the wave
 * over 28 pixels from its start phase, in the stock LFO's own phase units and waves (wave(), as the render plays
 * them), each column joined to the one before, a jump drawn whole; its axis dotted; a ring where it starts on the
 * axis in the modes that restart it. RND is the stock's one picture, whatever the phase (its columns, bit n:
 * y = 8 + n). Fitted to the stock's pictures: the triangle's and RND's are the same pixels, the others' shapes the
 * same within a pixel. */
static const u16 rnd_cols[29] = {
    0x0000, 0x0080, 0x0080, 0x0f80, 0x0800, 0x0800, 0x0800, 0x0f70, 0x0010, 0x0010, 0x001f, 0x0001, 0x0001, 0x0001,
    0x037f, 0x0200, 0x0200, 0x7e00, 0x4000, 0x4000, 0x4000, 0x7f70, 0x0010, 0x0010, 0x0010, 0x00f0, 0x0080, 0x0080,
    0x0000,
};

static s32 wave_y(s32 w, u32 ph)                /* the wave at ph (a turn: PERIOD), 7 pixels either way of 15 */
{
    s32 v = wave(w, (u32)fmul((s32)ph, 0x636ba875) << 2), y = 15 - (((v >> 8) * 7 + (1 << 22)) >> 23);
    return y < 8 ? 8 : y > 22 ? 22 : y;
}

static void wave_graphic(void *bmp, s32 w, s32 sph8, s32 mode)
{
    u32 ph0 = (u32)((s32)(u16)sph8 * (s32)(PERIOD >> 15)) % PERIOD;
    s32 x, prev = 15, start = 15;
    FILLRECT(bmp, 26, 23, 27, 23, 1);           /* the braces */
    FILLRECT(bmp, 25, 17, 25, 22, 1);
    FILLRECT(bmp, 24, 16, 25, 16, 1);
    FILLRECT(bmp, 24, 14, 25, 14, 1);
    FILLRECT(bmp, 25, 8, 25, 13, 1);
    FILLRECT(bmp, 26, 7, 27, 7, 1);
    FILLRECT(bmp, 65, 23, 66, 23, 1);
    FILLRECT(bmp, 67, 17, 67, 22, 1);
    FILLRECT(bmp, 67, 16, 68, 16, 1);
    FILLRECT(bmp, 67, 14, 68, 14, 1);
    FILLRECT(bmp, 67, 8, 67, 13, 1);
    FILLRECT(bmp, 65, 7, 66, 7, 1);
    for (x = -1; x <= 28; x++) {
        s32 y, lo, hi;
        u32 ph;
        if (w == 6) {                           /* RND: the stock's picture, a column's runs */
            u32 m = x < 0 ? 0 : rnd_cols[x];
            for (y = 0; y < 15; y++) {
                s32 y1 = y;
                if (!(m & (1u << y)))
                    continue;
                while (y1 < 14 && (m & (1u << (y1 + 1))))
                    y1++;
                FILLRECT(bmp, 32 + x, 8 + y, 32 + x, 8 + y1, 1);
                y = y1;
            }
            continue;
        }
        ph = ph0 + (x < 0 ? PERIOD - 49371429u : (u32)x * 49371428u + (u32)(4 * x) / 7);  /* x PERIOD / 28 */
        if (ph >= PERIOD)
            ph -= PERIOD;
        y = wave_y(w, ph);
        if (x == 0)
            start = y;
        if (x >= 0) {
            lo = y < prev ? y : prev + (y != prev);
            hi = y > prev ? y : prev - (y != prev);
            if (y - prev >= 7 || prev - y >= 7)     /* a jump: whole */
                lo = y < prev ? y : prev, hi = y > prev ? y : prev;
            FILLRECT(bmp, 32 + x, lo, 32 + x, hi, 1);
        }
        prev = y;
    }
    for (x = 32; x <= 60; x += 7)               /* the axis */
        px(bmp, x, 15, 1);
    if (mode && w != 6 && start == 15) {        /* TRIG, HOLD, ONE, HALF: the ring where it starts */
        FILLRECT(bmp, 31, 14, 33, 16, 1);
        px(bmp, 32, 15, 0);
    }
}

static void list_draw(const struct fx_effect *e, void *bmp)      /* DEST's list, over the page's right */
{
    s32 r, y, y0;
    FILLRECT(bmp, 49, 0, 127, 52, 0);
    for (r = 0; r < 7 && list_top + r < list_n; r++) {
        s32 k = list[list_top + r];
        char b[24], g[8];
        struct fx_param t;
        const struct fx_param *p = k ? param_of(e, k, &t) : 0;
        int n = 0;
        y = 45 - 7 * r;
        if (p) {
            const char *s = group(e, k, g, 6), *l = p->lname ? p->lname : p->name;
            while (*s && n < 17)
                b[n++] = *s++;
            if (n < 17)
                b[n++] = ':';
            while (*l && n < 17)
                b[n++] = *l++;
            b[n] = 0;
        } else {
            copy_n(b, "None", 17);
        }
        TEXTF(bmp, FONT5, 52, y, -1, "%s", b);
        if (list_top + r == list_sel)
            FILLRECT(bmp, 51, y - 1, 122, y + 5, -1);
    }
    for (y = 0; y <= 52; y++)                   /* the scroll bar: dotted, its thumb 7 rows */
        px(bmp, 126 - (y & 1), y, 1);
    y0 = 52 - (list_n > 1 ? list_sel * 46 / (list_n - 1) : 0);
    FILLRECT(bmp, 124, y0 - 6, 127, y0, 0);
    FILLRECT(bmp, 124, y0, 127, y0, 1);
    FILLRECT(bmp, 124, y0 - 6, 127, y0 - 6, 1);
    FILLRECT(bmp, 124, y0 - 6, 124, y0, 1);
    FILLRECT(bmp, 127, y0 - 6, 127, y0, 1);
}

static void lfo_draw(const struct fx_effect *e, void *bmp, s32 page)
{
    s32 base = page == 1 ? FX_L1 : FX_L2, i;
    char vb[8];
    for (i = 0; i < 8; i++) {
        struct fx_param t;
        s32 k = base + i, v = fx_view(k, fx_get(e, k)), x = lfo_x[i & 3], y = LFO_Y(i >> 2);
        const struct fx_param *p = param_of(e, k, &t);
        uint8_t *ui = uirec(k);
        const char *l;
        if (i == 3)
            dest_box(e, bmp, v, x, y);
        else if (i == 4)                /* WAVE, SPH (k + 1) and MODE (k + 2) */
            wave_graphic(bmp, v >> 8, fx_view(k + 1, fx_get(e, k + 1)), fx_view(k + 2, fx_get(e, k + 2)) >> 8);
        else if (i != 5 && ui && *(u32 *)(ui + 0x30))
            (*(gfx_fn *)(ui + 0x30))(ui + 0x24, v, bmp, x, y);
        else if (i != 5)
            knob(bmp, x + 9, y + 8, v >> 8, 1);
        l = label_of(e, p, k, v, vb);
        TEXTBOX(bmp, FONT_LBL, x + 9, LBL_Y(i >> 2), 1, 0, l, "%s", l);
    }
    if (list_pg == page)
        list_draw(e, bmp);
}

void fx_page_draw(const struct fx_effect *e, void *bmp, int32_t page)
{
    u8 slots[8];
    s32 i;
    if (page == 1 || page == 2) {
        lfo_draw(e, bmp, page);
        return;
    }
    for (i = 0; i < 8; i++)
        slots[i] = (u8)fx_page_slot(page, i);
    fx_draw_controls(e, bmp, slots);
}

void fx_lock_strip(const struct fx_effect *e, void *bmp, int32_t x, int32_t y)
{
    u32 m = fx_lock_mask(e, lock_pg);
    s32 s;
    for (s = 0; s < 16; s++) {
        s32 x0 = x + 4 * s;
        if (m & (1u << s))
            FILLRECT(bmp, x0, y + 2, x0 + 2, y + 4, 1);
        else
            FRAMERECT(bmp, x0, y + 2, x0 + 2, y + 4, 1);
        if (held == 16 * lock_pg + s)
            FILLRECT(bmp, x0, y, x0 + 2, y, 1);
    }
}

/* ---- project data ---------------------------------------------------------------------------------- */
#define NPACKED 25
static const u8 packed[NPACKED] = { FX_M_INL, FX_M_INR, FX_M_PANL, FX_M_PANR, FX_M_MIX, FX_M_BAL, FX_M_VOL,
                                    FX_M_WIDTH, FX_M_BYP, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const u8 width[NPACKED] = { 7, 7, 7, 7, 7, 7, 7, 7, 1, 7, 5, 7, 7, 3, 7, 3, 7, 7, 5, 7, 7, 3, 7, 3, 7 };
#define PACK_FMT 149                    /* the next 3 bits, the format: 0 IN L/IN R as (v / 100)^2, 1 the stock's */
static const u8 fine[4] = { FX_L1, FX_L1 + 7, FX_L2, FX_L2 + 7 };  /* their low bytes, bytes 19-22 */

void fx_pack(const struct fx_effect *e, uint8_t *out)
{
    s32 a = ent_of(e), i, b, at = 0;
    for (i = 0; i < FX_PACK; i++)
        out[i] = 0;
    for (i = 0; i < NPACKED; i++) {
        u32 v = a >= 0 && fx[a].base[packed[i]] != -1 ? (u16)fx[a].base[packed[i]] >> 8 : 0;
        for (b = 0; b < width[i]; b++, at++)
            if (v & (1u << b))
                out[at >> 3] |= (u8)(1u << (at & 7));
    }
    out[PACK_FMT >> 3] |= (u8)(1u << (PACK_FMT & 7));
    for (i = 0; i < 4; i++)             /* SPD's and DEP's fine steps */
        out[19 + i] = a >= 0 ? (u8)fx[a].base[fine[i]] : 0;
}

void fx_unpack(const struct fx_effect *e, const uint8_t *in)
{
    struct fx_param t;
    s32 a = ent_of(e), i, b, v, at = 0, fmt = in ? (in[PACK_FMT >> 3] >> (PACK_FMT & 7)) & 7 : 1;
    if (a < 0)
        return;
    for (i = 0; i < NPACKED; i++) {
        const struct fx_param *p = param_of(e, packed[i], &t);
        for (v = 0, b = 0; in && b < width[i]; b++, at++)
            v |= ((in[at >> 3] >> (at & 7)) & 1) << b;
        if (!p || fx[a].base[packed[i]] == -1)
            continue;
        if (!in)
            v = p->def;
        else if (!fmt && i < 2)
            v = (v * 127 + 200) / 400;  /* format 0's level, the same on the stock's law */
        if (p->kind == FX_OPT && v >= p->n)
            v = p->def;
        else if (v > 127)
            v = 127;
        v <<= 8;
        for (b = 0; in && b < 4; b++)   /* SPD's and DEP's fine steps (a block from before them: zeros) */
            if (packed[i] == fine[b])
                v |= in[19 + b];
        fx[a].base[packed[i]] = (s16)(v > vmax(packed[i]) ? vmax(packed[i]) : v);
    }
    fx[a].serial++;
}

uint32_t fx_serial(const struct fx_effect *e)
{
    s32 a = ent_of(e);
    return a < 0 ? 0 : fx[a].serial;
}

/* fxkit's own: the lock track */
static struct { u8 ver, track, pad[2]; } save;

static void fxt_loaded(int32_t found)
{
    fx_lock_track_set(found && save.ver == 1 && save.track < 4 ? save.track : 3);
    lock_pg = 0;
}

const struct core_projdata fxt_projdata = { 0x46584b54u /* "FXKT" */, sizeof(save), &save, fxt_loaded };

/* ---- the UI tick ----------------------------------------------------------------------------------- */
void fxt_tick(void *ctrl)
{
    const struct fx_effect *const *p;
    static u8 ready;
    (void)ctrl;
    if (!ready) {
        for (p = fx_effects; *p; p++)
            ent_of(*p);
        ready = 1;
    }
    while (sq_r != sq_w) {
        u8 r = sq_r;
        if (setq[r % SETQ].fx < nfx)
            fx_set(fx[setq[r % SETQ].fx].e, setq[r % SETQ].slot, setq[r % SETQ].val);
        sq_r = (u8)(r + 1);
    }
    save.ver = 1;
    save.track = lock_track;
    if (lock_pg >= fx_lock_pages())
        lock_pg = 0;
}
