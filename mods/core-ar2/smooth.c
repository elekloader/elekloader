/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.9: the smoothed value of a lockable mod parameter
 * (README, "The smoothed value"). Every address is OS 1.74's.
 *
 * core_param_value(track, field) is the value stock would play for a
 * lockable parameter (lock.c), in 8.8 steps of the field, in stock's order
 * (the audio interrupt, 0x40120d26 onwards):
 *
 *   1. the target: the sound's value in the kit the audio side plays (on
 *      the FX track, track 12, a kit parameter's value in that kit); the
 *      playing trig's p-lock (core_lock_value) in its place; the active
 *      scene's lock (core_scene_value) in place of that;
 *   2. smoothed with stock's own one-pole, s = 0.030 t + 0.970 s a block
 *      (0x03d7 and 0x7c29 of 0x8000, 0x40120d26): 90% of a step in 50 ms;
 *   3. then, unsmoothed, the performances (core_perf_offset), the velocity
 *      and aftertouch routes (core_mod_offset, routes.c) and the LFO
 *      (core_lfo_offset), the last two scaled from a 0..127 knob's steps to
 *      the field's range (core_param_value_ex with CORE_PARAM_MOD_RAW or
 *      CORE_PARAM_LFO_RAW adds one as it is), and clamped to min..max.
 *
 * A field declared "smooth": false (SMOOTH_OFF in core_lock_codes) skips 2:
 * a type or a mode jumps.
 *
 * Smoothing is lazy. Core keeps, per track and smoothed parameter, the
 * smoothed value and the block it was last stepped to; a call steps it k
 * blocks at once, s = t + (s - t) 0.97^k, from a table of 0.97^k. That is
 * stock's result exactly while the target holds between two calls, so a mod
 * that reads its parameter every block sees stock's ramp, and a parameter no
 * mod reads costs nothing. The block count is core's ev_render_in (load.s).
 *
 * Audio side only: the audio interrupt runs these, and nothing else may
 * step the state.
 *
 * 0.10: a read costs about a quarter of 0.9's (README, "The smoothed value"),
 * reading the audio side's state once a block, as stock does (snapshot()).
 * The field's code is a hash probe (lock.c), and everything else a read needs
 * of the code is made once (info, below): the range, the smoother's slot, the
 * LFO and route flags, and where the field's bits are in a sound or kit, which
 * it reads without core_store_get's interrupt mask (the audio interrupt is
 * not interrupted by a field's writer, which masks interrupts itself). The
 * p-lock, the LFO and the scene's gate are read in place; the scene's list,
 * the performances and the routes are asked by the code (scenes.c, routes.c).
 */

#include <stdint.h>

#define FIRST_CODE 42
#define LAST_CODE 127
#define CODES (LAST_CODE + 1 - FIRST_CODE)
#define TRACKS 13
#define SMOOTH_OFF 0x200                      /* in core_lock_codes: the field jumps */
#define SLOTS 32                              /* smoothed parameters core keeps a state for */
#define NONE ((int)0x80000000)                /* core_param_value: no such parameter here */

struct core_lock_param;
const struct core_lock_param *core_lock_by_code(unsigned code);
int core_lock_min(const struct core_lock_param *p);
int core_lock_max(const struct core_lock_param *p);
int core_lock_code(unsigned field);
int core_lock_on_track(const struct core_lock_param *p, int track);
/* (0.10) the five sources by the code, the code and its parameter looked up
 * once here rather than once in each (lock.c, rows.c, scenes.c, routes.c) */
int core_lock_value_code(int track, int code);
int core_scene_value_in(const unsigned char *kit, int track, int code, const struct core_lock_param *p);
int core_perf_offset_in(const unsigned char *kit, int track, int code, const struct core_lock_param *p);
int core_lfo_offset_code(int track, unsigned code);
int core_mod_offset_code(int track, unsigned code);
const unsigned char *core_audio_kit(void);
unsigned core_store_get(unsigned field, void *item, int index);

extern const unsigned core_lock_codes[];      /* {field, code | flags}..., {0, 0} */
extern volatile unsigned core_block;          /* load.s: blocks rendered */

/* 0.97003^k (stock's 0x7c29 / 0x8000), Q15, k = 0..255 */
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

/* a smoothed parameter's state on a track: the value in 16.16 and the block
 * it was stepped to (0: not yet read) */
struct state {
    int32_t s;
    uint32_t block;
};

static struct state states[TRACKS][SLOTS];

/* (0.10) what a read needs of each code, made once, so that a read looks
 * nothing up but the field's code: the parameter and its range, the smoother's
 * slot (+1, 0: none: it jumps, or no room), whether the LFO and the routes may
 * point at it, and where its bits are in a live sound or kit: up to 4 bytes
 * (their offsets from the sound or kit), then a shift and a mask, then the
 * init to XOR (core_store_get's own decoding, without its scope switch) */
#define SOUND_HOME 0x6d
#define KIT_HOME 0x796
#define SC_SOUND 1
#define SC_KIT 2
#define I_LFO 1
#define I_ROUTE 2
#define I_KIT 4                               /* a kit field: the FX track's (track 12) */

struct info {                                 /* 32 bytes, so an index is a shift */
    const struct core_lock_param *p;
    unsigned field;
    int16_t min, max;
    unsigned char slot, flags, n, rshift;
    uint16_t off[4];
    uint32_t mask, init;
};

static struct info info[CODES];
static volatile unsigned char flags_made;

extern const unsigned core_field_inits[];
const struct core_lock_param *core_lock_route(unsigned code);
const struct core_lock_param *core_lock_lfo(unsigned code);
const struct core_lock_param *core_lock_fx_lfo(unsigned code);

/* the byte k of a field's bits in a live sound or kit (store.c's field_byte):
 * a sound's 3 spare bytes, then the 4 route amounts' low bytes, 3 apart */
static unsigned home_byte(unsigned scope, unsigned k)
{
    return scope == SC_SOUND ? SOUND_HOME + (k < 3 ? k : 3 * k - 5) : KIT_HOME + k;
}

static void make(void)
{
    const unsigned *c;
    unsigned code, n = 0, f, scope, w, o, i, k;
    unsigned short sr;
    struct info *in;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    if (!flags_made) {
        for (c = core_lock_codes; c[0]; c += 2) {
            if ((code = c[1] & 0xff) < FIRST_CODE || code > LAST_CODE)
                continue;
            in = &info[code - FIRST_CODE];
            if (!(in->p = core_lock_by_code(code)))
                continue;
            in->min = core_lock_min(in->p);
            in->max = core_lock_max(in->p);
            if (!(c[1] & SMOOTH_OFF) && n < SLOTS)
                in->slot = ++n;
            in->flags = (core_lock_lfo(code) || core_lock_fx_lfo(code) ? I_LFO : 0) |
                        (core_lock_route(code) ? I_ROUTE : 0);
            in->field = f = c[0];
            scope = f >> 28;
            if (core_lock_on_track(in->p, 12))  /* lock.c's own test: a kit field */
                in->flags |= I_KIT;
            w = f >> 22 & 63;
            o = f & 0xffff;
            k = f >> 16 & 63;
            in->init = k ? core_field_inits[k - 1] : 0;
            in->n = ((o & 7) + w + 7) >> 3;
            if ((scope != SC_SOUND && scope != SC_KIT) || in->n > 4) {
                in->n = 0;                    /* read through core_store_get */
                continue;
            }
            for (i = 0; i < in->n; i++)
                in->off[i] = home_byte(scope, (o >> 3) + i);
            in->rshift = 8 * in->n - (o & 7) - w;
            in->mask = w < 32 ? (1u << w) - 1 : 0xffffffffu;
        }
        flags_made = 1;
    }
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

extern volatile unsigned short core_lock_now[TRACKS][CODES];  /* rows.c: the playing trig's lock + 1 */
extern volatile unsigned char core_lfo_code[TRACKS];          /* lock.c: the LFO's mod code */
extern volatile int16_t core_lfo_value[TRACKS];
#define ACTIVE_SCENE 2430                     /* scenes.c: in a live kit, the active scene, -1: none */
#define AUDIO_KIT (*(const volatile unsigned *)0x800067b8)   /* scenes.c: the kit the audio side plays */
#define LIVE_KITS 0x412558e3u
#define LIVE_KIT_SIZE 2531
#define PERF_OFF (*(const volatile unsigned *)0x8000ed74)    /* scenes.c: stock skips the performances */
#define PRESSURE32 ((const volatile uint32_t *)0x8000f774)   /* the 12 performances' pressures, 2 a word */

/* the audio side's state a read needs, taken once a block on the block's
 * first read, as stock reads it once a block for its own parameters: the
 * kit the audio side plays, and whether the performances count (not
 * switched off, and a pad's pressure above 0) */
static uint32_t snap_block;
static const unsigned char *snap_kit;
static unsigned char snap_perf;

/* and per track, on its first read in a block, whether any of its 8
 * velocity and aftertouch routes points at a mod parameter (routes.c's
 * core_routes, which the engine push writes once a block) */
#define ROUTES 8
extern volatile uint32_t core_routes[];
static uint32_t route_block[TRACKS];
static unsigned char route_any[TRACKS];

static int any_route(int track)
{
    uint32_t now = core_block, any = 0;
    const volatile uint32_t *e = core_routes + ROUTES * track;
    int i;
    if (route_block[track] == now && now)
        return route_any[track];
    for (i = 0; i < ROUTES; i++)
        any |= e[i] & 0xffff;
    route_any[track] = any != 0;
    route_block[track] = now;
    return any != 0;
}

static void snapshot(void)
{
    uint32_t now = core_block, k;
    if (snap_block == now && now)
        return;
    k = AUDIO_KIT;
    snap_kit = k < 128 ? (const unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k : 0;
    snap_perf = !PERF_OFF && (PRESSURE32[0] | PRESSURE32[1] | PRESSURE32[2] | PRESSURE32[3] | PRESSURE32[4] |
                              PRESSURE32[5]);
    snap_block = now;
}

/* d x f / 0x8000 for |d| < 2^31 and f <= 0x8000, in 32-bit multiplies */
static int32_t scale(int32_t d, unsigned f)
{
    uint32_t a = d < 0 ? -(uint32_t)d : (uint32_t)d;
    a = (a >> 15) * f + ((a & 0x7fff) * f >> 15);
    return d < 0 ? -(int32_t)a : (int32_t)a;
}

/* the state stepped to now toward t (16.16) */
static int32_t step(struct state *st, int32_t t)
{
    uint32_t now = core_block, k = now - st->block;
    int32_t d;
    if (!st->block || !now) {                 /* never read (or the count wrapped to 0): start at t */
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

#define LFO_RAW 1                             /* core_param_value_ex: the LFO's offset as it is */
#define MOD_RAW 2                             /* and the velocity and aftertouch routes' */

/* the parts a read seldom needs, kept out of the read so that it saves
 * fewer registers */
static __attribute__((noinline)) int modulations(const unsigned char *kit, int track, int code,
                                                  const struct info *in, unsigned flags)
{
    int v = 0, mod, lfo;
    if (snap_perf)                            /* a pad pressed: core_perf_offset's own gates, this block */
        v += core_perf_offset_in(kit, track, code, in->p);
    if (in->flags & I_ROUTE && any_route(track) && (mod = core_mod_offset_code(track, code)) != 0)
        v += flags & MOD_RAW ? mod : mod * (in->max - in->min) / 127;
    if (in->flags & I_LFO && core_lfo_code[track] == code && (lfo = core_lfo_value[track]) != 0)
        v += flags & LFO_RAW ? lfo : lfo * (in->max - in->min) / 127;
    return v;
}

static __attribute__((noinline)) int scene_or(const unsigned char *kit, int track, int code,
                                               const struct info *in, int v)
{
    int off = core_scene_value_in(kit, track, code, in->p);
    return off >= 0 ? off : v;
}

static __attribute__((noinline)) unsigned slow_get(const struct info *in, const unsigned char *s)
{
    return core_store_get(in->field, (void *)s, 0);
}

/* the read, by the code */
static int value(int track, int code, unsigned flags)
{
    const struct info *in;
    const unsigned char *kit, *s;
    uint32_t b;
    int v, off, min, max;
    if (code < FIRST_CODE || code > LAST_CODE)
        return NONE;
    if (!flags_made)
        make();
    in = &info[code - FIRST_CODE];
    /* a kit parameter on the FX track, a sound's on a drum track (core_lock_on_track) */
    if (!in->p || (unsigned)track >= 13 || !(in->flags & I_KIT) != (track != 12))
        return NONE;
    snapshot();
    if (!(kit = snap_kit))                    /* the sound, or on the FX track the kit (core_audio_sound) */
        return NONE;
    s = track < 12 ? kit + 42 + 152 * track : kit;
    /* the target, in whole steps: the field's bytes, as core_store_get reads them */
    switch (in->n) {
    case 1: b = s[in->off[0]]; break;
    case 2: b = s[in->off[0]] << 8 | s[in->off[1]]; break;
    case 3: b = s[in->off[0]] << 16 | s[in->off[1]] << 8 | s[in->off[2]]; break;
    case 4: b = (uint32_t)s[in->off[0]] << 24 | s[in->off[1]] << 16 | s[in->off[2]] << 8 | s[in->off[3]]; break;
    default: b = 0;
    }
    v = in->n ? (int)((b >> in->rshift & in->mask) ^ in->init) : (int)slow_get(in, s);
    if ((off = core_lock_now[track][code - FIRST_CODE]) != 0)
        v = off - 1;                          /* the playing trig's p-lock (core_lock_value) */
    if ((signed char)kit[ACTIVE_SCENE] >= 0)
        v = scene_or(kit, track, code, in, v);
    min = in->min;
    max = in->max;
    v = v < min ? min : v > max ? max : v;
    /* smoothed, in 8.8 (the state keeps 16.16) */
    if (in->slot)
        v = (step(&states[track][in->slot - 1], v * 65536) + 0x80) >> 8;
    else
        v *= 256;
    /* the modulations, after the smoother as stock adds them */
    v += modulations(kit, track, code, in, flags);
    min *= 256;
    max *= 256;
    return v < min ? min : v > max ? max : v;
}

int core_param_value_ex(int track, unsigned field, unsigned flags)
{
    return value(track, core_lock_code(field), flags);
}

int core_param_value(int track, unsigned field)
{
    return value(track, core_lock_code(field), 0);
}

/* (0.10) a field's handle, looked up once (its lock code, 0 for a field with
 * none), and the read by the handle, which skips the field's lookup */
int core_param_handle(unsigned field)
{
    return core_lock_code(field);
}

int core_param_value_h(int track, int handle, unsigned flags)
{
    return value(track, handle, flags);
}

int core_param_int(int track, unsigned field)
{
    int v = core_param_value(track, field);
    return v == NONE ? v : (v + 0x80) >> 8;
}
