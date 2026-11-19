/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.5: the value read, the lock service's third part (README, "The
 * value read"): core-ar2 0.10's smooth.c with the Analog Four MKII's stages.
 * Every address is OS 1.56's.
 *
 * core_param_values(track, handles, n, flags, out) gives each lockable
 * parameter the value stock would play, as stock's audio interrupt
 * (0x401013be) computes its own parameters once a block, stage by stage,
 * with a clamp to 0..0x7fff after each term:
 *
 *   1. the target: the field's value in the sound the voice was pushed with
 *      (part 2's core_voice_item; on FX and CV the kit), the playing trig's
 *      p-lock (rows.c, core_lock_now) in its place, in stock's engine scale:
 *      the field's min..max as 0..0x7f00, as a stock coarse parameter is (a
 *      0..127 field is exactly 8.8);
 *   2. stock's one-pole (0x40102f0c), s = 0.97 s + 0.03 target a block, the
 *      Rytm's constants, lazily as smooth.c steps it; reset to the target at
 *      a trig's event accept when that trig or the one before it was locked,
 *      since stock jumps into a lock and back out of it (trace item 7);
 *   3. Param Slide: + delta x phase / limit, delta the lock's (rows.c), phase
 *      and limit the voice's (0x80002d54, 0x80002d3c), the phase as stock's
 *      ramp used it in the block;
 *   4. the routes (0x400fff7a): each noted route, source x depth / 32768;
 *   5. the macros and CONTROL IN (0x40103314): each noted target on the
 *      voice's track, the macro's value x depth / 32768;
 *   6. LFO2, Env2, LFO1, EnvF (0x4007cbf2, 0x400ff230), each Dest on the
 *      code: LFO ((depth - 0x4000) x out) >> 30, envelope >> 28.
 *
 * and back to 8.8 steps of the field, clamped to min..max.
 *
 * Every input of stages 3 to 6 is audio-side state stock computes once a
 * block and holds for the rest of it. Core copies it at ev_render_out
 * (snapshot(), order 1, before any mod's handler), so a read in that block's
 * ev_render_out sees stock's values of the block, and a read in the next
 * block's ev_render_in the same, one block (0.67 ms) late, as on the Rytm.
 * The snapshot also notes per voice whether anything modulates one of its
 * mod parameters, so a read of a voice with none skips stages 4 to 6.
 *
 * Voices. A track's read uses the voice its last trig went to (rows.c's
 * record of the accepts: on a poly track the last-played voice), and
 * core_param_values_voice reads one voice: 0..3 the synth voices, 4 the FX
 * track's, 5 the CV track's, the index stock's LFOs and envelopes use.
 *
 * A handle is the field's code and its kind (code | kind << 8, from
 * core_param_handle): a build's own numbering, never kept in a project.
 *
 * Audio side only: the audio interrupt runs these (ev_render_in,
 * ev_render_out, a mod's engine hook), and nothing else may step the state.
 */

#include <stdint.h>

#define FIRST_CODE 0x70
#define LAST_CODE 0x7f
#define CODES 16
#define KINDS 3
#define VOICES 6
#define SMOOTH_OFF 0x200                      /* in core_lock_codes: the field jumps */
#define ROUTE 0x400                           /* "mod": true */
#define NONE ((int)0x80000000)
#define ENGINE_FLAG 4                         /* core_param_value_ex: the engine value, 0..0x7fff */
#define TOP 0x7f00                            /* a field's max in the engine's scale */

#define SC_SOUND 1
#define SC_KIT 2
#define KIT_HOME 0x950
#define LIVE_KITS 0x411e9936u
#define LIVE_KIT_SIZE 0x960
#define KIT_SOUNDS 0x1c
#define SOUND_SIZE 0x152
#define FX_BLOCK 0x564                        /* the FX and CV pushes' blocks in a kit */
#define CV_BLOCK 0x668
#define WORKING_KIT_INDEX ((const volatile unsigned char *)0x4045c974 + 0x199330)

/* the audio side (SRAM) */
#define COPY ((const volatile uint16_t *)0x80005af0)          /* the engine's values this block */
#define VEL ((const volatile uint16_t *)0x4287c4e8)           /* [voice] */
#define VEL_BIPOLAR ((const volatile uint8_t *)0x800042dc)    /* [voice] */
#define ROUTE_TRACK ((const volatile int32_t *)0x80006008)    /* [voice], the route apply's */
#define VOICE_TRACK ((const volatile int32_t *)0x80005ff0)    /* [synth voice], the macro apply's */
#define BEND ((const volatile int16_t *)0x80006a68)           /* [track] */
#define WHEEL ((const volatile int16_t *)0x80006a5a)
#define BREATH ((const volatile int16_t *)0x80006a4c)
#define AFTERTOUCH ((const volatile int16_t *)0x80006a76)
#define SLIDE_PHASE ((const volatile uint32_t *)0x80002d54)   /* [voice] */
#define SLIDE_LIMIT ((const volatile uint32_t *)0x80002d3c)
#define SLIDE_RATE (*(const volatile uint32_t *)0x40214818)     /* the phase grows by twice it a block */
#define SLIDE_SCALE ((const volatile int32_t *)0x80002d24)    /* [voice], set at the slide trig */
#define LFO_OUT(n, p) (*(const volatile int32_t *)(0x80006b74 + 0x60 * (n) + 0x30 * (p)))
#define ENV_OUT(n, e) (*(const volatile int32_t *)(0x80003b24 + 0x54 * (n) + 0x1c * (e)))
#define LFO_DEPTH(n, p, k) (0xca + 0x10 * (n) + 2 * (p) + (k))   /* copy slots */
#define ENV_DEPTH(n, e, k) (0xd0 + 0x10 * (n) + 2 * (e) + (k))
#define MACRO_BIPOLAR ((const volatile uint8_t *)0x80003e38 + 0x498)  /* [macro] */
#define CONTROL_IN_OFF (*(const volatile uint32_t *)0x80004804) /* CONTROL IN 1, 2 read 0 */
#define MACROS 12

struct core_lock_param {
    unsigned field;
    int32_t id;
    int16_t min, max;
    const char *short_name;
    const char *name;
    const char *category;
};

struct core_dst {
    uint8_t src, n, code, pad;
    int16_t depth;
};

const struct core_lock_param *core_lock_by_code(int kind, unsigned code);
unsigned core_lock_flags(int kind, unsigned code);
int core_lock_code(unsigned field);
int core_lock_kind(unsigned field);
int core_lock_kind_of_track(int track);
int core_lock_max(const struct core_lock_param *p);
unsigned core_store_get(unsigned field, void *item, int index);
const struct core_dst *core_dst_voice(int voice, int *routes, int *dests);
const struct core_dst *core_dst_macros(int *n);

extern const unsigned core_lock_codes[];      /* {field, code | flags}..., {0, 0} */
extern volatile unsigned core_block;          /* load.s: blocks rendered */
extern volatile unsigned short core_lock_now[VOICES][CODES];  /* rows.c: lock + 1 */
extern volatile short core_slide_delta[VOICES][CODES];
extern volatile signed char core_lock_voice_track[VOICES];
extern volatile unsigned core_lock_accepts[VOICES];
extern void *volatile core_voice_item[VOICES];                /* dests.c */

/* 0.97003^k (stock's 0x7c29 / 0x8000), Q15, k = 0..255 (core-ar2's) */
static const uint16_t decay[256] = {
    32768, 31785, 30831, 29907, 29009, 28139, 27295, 26476, 25682, 24912, 24164, 23439, 22736, 22054, 21393, 20751,
    20128, 19524, 18939, 18371, 17820, 17285, 16766, 16263, 15776, 15302, 14843, 14398, 13966, 13547, 13141, 12746,
    12364, 11993, 11633, 11284, 10946, 10618, 10299, 9990, 9690, 9400, 9118, 8844, 8579, 8322, 8072, 7830,
    7595, 7367, 7146, 6932, 6724, 6522, 6326, 6137, 5952, 5774, 5601, 5433, 5270, 5112, 4958, 4810,
    4665, 4525, 4390, 4258, 4130, 4006, 3886, 3769, 3656, 3547, 3440, 3337, 3237, 3140, 3046, 2954,
    2866, 2780, 2696, 2615, 2537, 2461, 2387, 2315, 2246, 2179, 2113, 2050, 1988, 1929, 1871, 1815,
    1760, 1708, 1656, 1607, 1558, 1512, 1466, 1422, 1380, 1338, 1298, 1259, 1221, 1185, 1149, 1115,
    1081, 1049, 1017, 987, 957, 929, 901, 874, 847, 822, 797, 773, 750, 728, 706, 685,
    664, 644, 625, 606, 588, 570, 553, 537, 521, 505, 490, 475, 461, 447, 434, 421,
    408, 396, 384, 372, 361, 350, 340, 330, 320, 310, 301, 292, 283, 275, 266, 258,
    251, 243, 236, 229, 222, 215, 209, 202, 196, 191, 185, 179, 174, 169, 164, 159,
    154, 149, 145, 141, 136, 132, 128, 124, 121, 117, 114, 110, 107, 104, 101, 97,
    95, 92, 89, 86, 84, 81, 79, 76, 74, 72, 70, 68, 66, 64, 62, 60,
    58, 56, 55, 53, 51, 50, 48, 47, 46, 44, 43, 42, 40, 39, 38, 37,
    36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 26, 25, 24, 23, 23,
    22, 21, 21, 20, 19, 19, 18, 18, 17, 17, 16, 16, 15, 15, 14, 14,
};

/* ---- what a read needs of each code, made once ---- */

struct info {
    const struct core_lock_param *p;
    unsigned field;
    int16_t min, max;
    int32_t range;                            /* max - min, at least 1 */
    unsigned char smooth, route, n, rshift;   /* n: the field's bytes (0: core_store_get) */
    uint16_t off[4];                          /* from the sound or the kit */
    uint32_t mask, init;                      /* (0.6) the field's init, which its bits keep XORed */
};

extern const unsigned core_field_inits[];

static struct info info[KINDS][CODES];
static volatile unsigned char made;

static unsigned home_byte(unsigned scope, unsigned k)
{
    return scope == SC_SOUND ? (k < 2 ? 0xd4 + k : 0xde + k) : KIT_HOME + k;   /* store.c's */
}

static void make(void)
{
    unsigned short sr;
    int kind, c;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    if (!made) {
        for (kind = 0; kind < KINDS; kind++)
            for (c = 0; c < CODES; c++) {
                struct info *in = &info[kind][c];
                unsigned f, scope, w, o, i, fl;
                if (!(in->p = core_lock_by_code(kind, FIRST_CODE + c)))
                    continue;
                fl = core_lock_flags(kind, FIRST_CODE + c);
                in->field = f = in->p->field;
                in->min = in->p->min;
                in->max = core_lock_max(in->p);
                in->range = in->max > in->min ? in->max - in->min : 1;
                in->smooth = !(fl & SMOOTH_OFF);
                in->route = (fl & ROUTE) != 0;
                scope = f >> 28;
                w = f >> 22 & 63;
                o = f & 0xffff;
                in->init = (f >> 16 & 63) ? core_field_inits[(f >> 16 & 63) - 1] : 0;   /* store.c's field_init */
                in->n = ((o & 7) + w + 7) >> 3;
                if ((scope != SC_SOUND && scope != SC_KIT) || in->n > 4 || !w) {
                    in->n = 0;
                    continue;
                }
                for (i = 0; i < in->n; i++)
                    in->off[i] = home_byte(scope, (o >> 3) + i);
                in->rshift = 8 * in->n - (o & 7) - w;
                in->mask = w < 32 ? (1u << w) - 1 : 0xffffffffu;
            }
        made = 1;
    }
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

/* ---- the snapshot, at ev_render_out ---- */

struct snap {
    int16_t src[5];                           /* velocity, bend, wheel, breath, aftertouch */
    unsigned char any, track;                 /* something modulates a mod parameter; the macro track */
    uint32_t phase, limit;
    int32_t factor;                           /* the ramp's factor this block, stock's s16 */
    int32_t lfo[2], env[2];
    int16_t depth[8];                         /* each Dest place's depth - 0x4000 */
};

static struct snap snaps[VOICES];
static uint32_t phase_after[VOICES];          /* the phase stock left at the last block's end */
static int32_t macro_value[MACROS];
static uint32_t snap_block;                   /* the block it was taken in, 0 none */
volatile unsigned core_value_snaps;           /* snapshots taken (checks) */

/* a Dest place's stage: 0..3 LFO2, Env2, LFO1, EnvF (stock's order), and
 * the LFO or envelope's number in it */
static const unsigned char synth_stage[8] = {3, 3, 1, 1, 2, 2, 0, 0};   /* EnvF, Env2, LFO1, LFO2 */
static const unsigned char fx_stage[4] = {2, 2, 0, 0};                  /* LFO1, LFO2 */

static int place_stage(int v, int place)
{
    return v == 4 ? (place < 4 ? fx_stage[place] : -1) : synth_stage[place];
}

static int stage_depth_slot(int v, int stage, int k)
{
    switch (stage) {
    case 0: return LFO_DEPTH(v, 1, k);
    case 1: return ENV_DEPTH(v, 1, k);
    case 2: return LFO_DEPTH(v, 0, k);
    default: return ENV_DEPTH(v, 0, k);
    }
}

/* floor(a x b / 2^31), a unsigned, b signed, in 32-bit multiplies (the
 * EMAC's fractional macl, truncating) */
static int32_t mul_hi31(uint32_t a, int32_t b)
{
    uint32_t al = a & 0xffff, ah = a >> 16, bl = (uint32_t)b & 0xffff, lo, lo2;
    int32_t bh = b >> 16, hi, mid2;
    uint32_t mid1 = ah * bl;
    lo = al * bl;
    hi = (int32_t)ah * bh;
    lo2 = lo + (mid1 << 16);
    hi += (int32_t)(mid1 >> 16) + (lo2 < lo);
    mid2 = (int32_t)al * bh;
    lo = lo2 + ((uint32_t)mid2 << 16);
    hi += (mid2 >> 16) + (lo < lo2);
    return (int32_t)((uint32_t)hi << 1 | lo >> 31);
}

/* the ramp's factor as stock computes it (0x4010306a): (phase / 2) x the
 * voice's scale, fractional, times 0x74822, its high word */
static int32_t slide_factor(uint32_t phase, int32_t scale)
{
    return (int16_t)((uint32_t)mul_hi31(phase >> 1, scale) * 0x74822u >> 16);
}

void core_value_snapshot(void)
{
    int v, i, n, routes, dests, t;
    const struct core_dst *e, *m = core_dst_macros(&n);
    unsigned char mtrack[VOICES] = {0};
    if (!made)
        make();
    for (i = 0; i < n; i++) {                 /* the tracks a macro target is on */
        t = m[i].n;
        if ((unsigned)t < VOICES)
            mtrack[t] = 1;
    }
    if (n)
        for (i = 0; i < MACROS; i++) {        /* the macro apply's values (0x40103314) */
            int32_t x = COPY[i];
            if (MACRO_BIPOLAR[i])
                x = (x - 0x4000) * 2;
            macro_value[i] = i > 9 && CONTROL_IN_OFF ? 0 : x;
        }
    for (v = 0; v < VOICES; v++) {
        struct snap *s = &snaps[v];
        e = core_dst_voice(v, &routes, &dests);
        s->track = v < 4 ? (unsigned char)VOICE_TRACK[v] : v;
        s->any = routes || dests || (s->track < VOICES && mtrack[s->track]);
        {
            /* the phase stock's ramp used this block: it adds to the phase
             * after using it (0x4010306a), and a slide trig's event resets
             * it to 0 first (0x4010301c) */
            uint32_t now = SLIDE_PHASE[v], li = SLIDE_LIMIT[v], was = phase_after[v], next = was + 2 * SLIDE_RATE;
            s->phase = now == (next < li ? next : li) ? was : 0;
            s->limit = li;
            s->factor = li ? slide_factor(s->phase, SLIDE_SCALE[v]) : 0;
            phase_after[v] = now;
        }
        if (!s->any)
            continue;
        if (routes) {
            int32_t x = VEL[v];
            t = ROUTE_TRACK[v];
            if (VEL_BIPOLAR[v])
                x = (x - 0x4000) * 2;
            s->src[0] = (int16_t)x;
            if ((unsigned)t < VOICES) {
                s->src[1] = (int16_t)(BEND[t] * 2 - 0x8000);
                s->src[2] = WHEEL[t];
                s->src[3] = BREATH[t];
                s->src[4] = AFTERTOUCH[t];
            }
        }
        if (dests) {
            s->lfo[0] = LFO_OUT(v, 0);
            s->lfo[1] = LFO_OUT(v, 1);
            if (v != 4) {                     /* FX has no envelopes */
                s->env[0] = ENV_OUT(v, 0);
                s->env[1] = ENV_OUT(v, 1);
            }
            for (i = 0; i < dests; i++) {
                int place = e[routes + i].src - 5, st;
                if ((unsigned)place < 8 && (st = place_stage(v, place)) >= 0)
                    s->depth[place] = (int16_t)(COPY[stage_depth_slot(v, st, place & 1)] - 0x4000);
            }
        }
    }
    snap_block = core_block ? core_block : 1;
    core_value_snaps++;
}

/* ---- the smoother ---- */

struct state {
    int32_t s;                                /* the engine value << 8 */
    uint32_t block;                           /* stepped to (0: never) */
    uint32_t accept;                          /* the voice's accept it last saw, bit 31: that trig was locked */
};

static struct state states[VOICES][CODES];

/* d x f / 0x8000 for |d| < 2^31 and f <= 0x8000 */
static int32_t scale(int32_t d, unsigned f)
{
    uint32_t a = d < 0 ? -(uint32_t)d : (uint32_t)d;
    a = (a >> 15) * f + ((a & 0x7fff) * f >> 15);
    return d < 0 ? -(int32_t)a : (int32_t)a;
}

/* the state stepped to now toward t (engine << 8); a new trig that is
 * locked, or follows a locked one, jumps */
static int32_t step(struct state *st, int32_t t, uint32_t accept, int locked)
{
    uint32_t now = core_block, k = now - st->block, seen = accept & 0x7fffffff;
    int32_t d;
    int jump = !st->block || !now;
    if ((st->accept & 0x7fffffff) != seen) {
        if (locked || st->accept >> 31)
            jump = 1;
        st->accept = seen | (locked ? 0x80000000u : 0);
    }
    if (jump) {
        st->s = t;
        st->block = now ? now : 1;
        return t;
    }
    d = st->s - t;
    for (; k >= 256 && d; k -= 255)
        d = scale(d, decay[255]);
    if (d)
        d = scale(d, decay[k]);
    st->s = t + d;
    st->block = now;
    return st->s;
}

/* ---- the read ---- */

static int32_t clamp(int32_t x)
{
    return x < 0 ? 0 : x > 0x7fff ? 0x7fff : x;
}

/* floor(a x b / 2^sh) for |a| <= 2^15 and sh 16..31, in 32-bit multiplies */
static int32_t mul_shift(int32_t a, int32_t b, int sh)
{
    int32_t hi = b >> 16, lo = b & 0xffff;
    return (a * hi + ((a * lo) >> 16)) >> (sh - 16);
}

static __attribute__((noinline)) int32_t modulations(int v, int code, int32_t x)
{
    const struct snap *s = &snaps[v];
    const struct core_dst *e, *m;
    int routes, dests, i, n, st;
    e = core_dst_voice(v, &routes, &dests);
    for (i = 0; i < routes; i++)              /* 4: the routes */
        if (e[i].code == code && e[i].src < 5)
            x = clamp(x + ((s->src[e[i].src] * e[i].depth) >> 15));
    m = core_dst_macros(&n);
    for (i = 0; i < n; i++)                   /* 5: the macros and CONTROL IN on the voice's track */
        if (m[i].code == code && m[i].n == s->track && m[i].src < MACROS)
            x = clamp(x + ((macro_value[m[i].src] * m[i].depth) >> 15));
    for (st = 0; st < 4; st++)                /* 6: LFO2, Env2, LFO1, EnvF */
        for (i = routes; i < routes + dests; i++) {
            int place = e[i].src - 5;
            if (e[i].code != code || (unsigned)place >= 8 || place_stage(v, place) != st)
                continue;
            x = clamp(x + (st == 0   ? mul_shift(s->depth[place], s->lfo[1], 30)
                           : st == 1 ? mul_shift(s->depth[place], s->env[1], 28)
                           : st == 2 ? mul_shift(s->depth[place], s->lfo[0], 30)
                                     : mul_shift(s->depth[place], s->env[0], 28)));
        }
    return x;
}

/* the item the voice plays: its pushed sound on a synth voice, the kit on
 * FX and CV (from the pushed block, else the current kit) */
static const unsigned char *item_of(int v)
{
    unsigned a = (unsigned)core_voice_item[v], k;
    if (v < 4 && a)
        return (const unsigned char *)a;
    if (v >= 4 && a >= LIVE_KITS + (v == 4 ? FX_BLOCK : CV_BLOCK)) {
        a -= v == 4 ? FX_BLOCK : CV_BLOCK;
        if ((a - LIVE_KITS) % LIVE_KIT_SIZE == 0 && (a - LIVE_KITS) / LIVE_KIT_SIZE < 128)
            return (const unsigned char *)a;
    }
    k = (unsigned)WORKING_KIT_INDEX[0] << 24 | (unsigned)WORKING_KIT_INDEX[1] << 16 |
        (unsigned)WORKING_KIT_INDEX[2] << 8 | WORKING_KIT_INDEX[3];
    if (k >= 128)
        return 0;
    a = LIVE_KITS + LIVE_KIT_SIZE * k;
    return (const unsigned char *)(v < 4 ? a + KIT_SOUNDS + SOUND_SIZE * (v < 4 ? VOICE_TRACK[v] & 3 : 0) : a);
}

static __attribute__((noinline)) unsigned slow_get(const struct info *in, const unsigned char *s)
{
    return core_store_get(in->field, (void *)s, 0);
}

static int one(const struct info *in, int v, int code, const unsigned char *s, unsigned flags)
{
    uint32_t b;
    int32_t x, lock, d;
    int c = code - FIRST_CODE;
    const struct snap *sn = &snaps[v];
    /* 1: the target, in the field's steps, then the engine's scale */
    switch (in->n) {
    case 1: b = s[in->off[0]]; break;
    case 2: b = s[in->off[0]] << 8 | s[in->off[1]]; break;
    case 3: b = s[in->off[0]] << 16 | s[in->off[1]] << 8 | s[in->off[2]]; break;
    case 4: b = (uint32_t)s[in->off[0]] << 24 | s[in->off[1]] << 16 | s[in->off[2]] << 8 | s[in->off[3]]; break;
    default: b = 0;
    }
    x = in->n ? (int32_t)((b >> in->rshift & in->mask) ^ in->init) : (int32_t)slow_get(in, s);
    if ((lock = core_lock_now[v][c]) != 0)
        x = lock - 1;
    x = x < in->min ? in->min : x > in->max ? in->max : x;
    x = (x - in->min) * TOP / in->range;
    /* 2: smoothed */
    if (in->smooth)
        x = (step(&states[v][c], x << 8, core_lock_accepts[v], lock != 0) + 0x80) >> 8;
    /* 3: Param Slide's ramp, as stock's (0x401030c6): copy - factor x delta,
     * fractional, the delta in the engine's scale */
    if ((d = core_slide_delta[v][c]) != 0 && sn->limit && snap_block)
        x = clamp(x + ((-sn->factor * (d * TOP / in->range)) >> 15));
    /* 4..6 */
    if (sn->any && in->route && snap_block)
        x = modulations(v, code, x);
    if (flags & ENGINE_FLAG)
        return x;
    x = in->min * 256 + x * in->range / 127;
    return x < in->min * 256 ? in->min * 256 : x > in->max * 256 ? in->max * 256 : x;
}

/* the handle's info on the voice's kind, or 0 */
static const struct info *on(int v, int handle)
{
    int kind = v < 4 ? 0 : v == 4 ? 1 : 2, code = handle & 0xff;
    const struct info *in;
    if ((handle >> 8) != kind || code < FIRST_CODE || code > LAST_CODE)
        return 0;
    in = &info[kind][code - FIRST_CODE];
    return in->p ? in : 0;
}

/* the voice a track's read uses: the one its last trig went to */
int core_param_voice(int track)
{
    int v, best = -1;
    unsigned n = 0;
    if (track == 4 || track == 5)
        return track;
    if ((unsigned)track >= 4)
        return -1;
    for (v = 0; v < 4; v++)
        if (core_lock_voice_track[v] == track && (best < 0 || (int)(core_lock_accepts[v] - n) > 0)) {
            best = v;
            n = core_lock_accepts[v];
        }
    if (best < 0)                             /* nothing played yet: a voice on the track, else its own */
        for (v = 0; v < 4 && best < 0; v++)
            if (VOICE_TRACK[v] == track)
                best = v;
    return best < 0 ? track : best;
}

void core_param_values_voice(int voice, const int *handles, int n, unsigned flags, int *out)
{
    const struct info *in;
    const unsigned char *s = 0;
    int i;
    if (!made)
        make();
    if ((unsigned)voice < VOICES)
        s = item_of(voice);
    for (i = 0; i < n; i++)
        out[i] = s && (in = on(voice, handles[i])) ? one(in, voice, handles[i] & 0xff, s, flags) : NONE;
}

void core_param_values(int track, const int *handles, int n, unsigned flags, int *out)
{
    core_param_values_voice(core_param_voice(track), handles, n, flags, out);
}

int core_param_handle(unsigned field)
{
    int code = core_lock_code(field), kind = core_lock_kind(field);
    return code && kind >= 0 ? code | kind << 8 : 0;
}

int core_param_value_h(int track, int handle, unsigned flags)
{
    int v;
    core_param_values(track, &handle, 1, flags, &v);
    return v;
}

int core_param_value_ex(int track, unsigned field, unsigned flags)
{
    return core_param_value_h(track, core_param_handle(field), flags);
}

int core_param_value(int track, unsigned field)
{
    return core_param_value_h(track, core_param_handle(field), 0);
}

int core_param_int(int track, unsigned field)
{
    int v = core_param_value(track, field);
    return v == NONE ? v : (v + 0x80) >> 8;
}
