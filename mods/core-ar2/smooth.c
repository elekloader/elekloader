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
int core_lock_value(int track, unsigned field);
int core_lock_on_track(const struct core_lock_param *p, int track);
int core_scene_value(int track, unsigned field);
int core_perf_offset(int track, unsigned field);
int core_lfo_offset(int track, unsigned field);
int core_mod_offset(int track, unsigned field);
const unsigned char *core_audio_sound(int track);
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
static unsigned char slot_of[CODES];          /* slot + 1, 0: none (jumps, or no room) */
static unsigned char flags_made;

static void make(void)
{
    const unsigned *c;
    unsigned code, n = 0;
    for (c = core_lock_codes; c[0]; c += 2)
        if ((code = c[1] & 0xff) >= FIRST_CODE && code <= LAST_CODE && !(c[1] & SMOOTH_OFF) && n < SLOTS)
            slot_of[code - FIRST_CODE] = ++n;
    flags_made = 1;
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

int core_param_value_ex(int track, unsigned field, unsigned flags)
{
    int code = core_lock_code(field), v, min, max, off, lfo, mod;
    const struct core_lock_param *p = core_lock_by_code(code);
    const unsigned char *s;
    unsigned slot;
    if (!core_lock_on_track(p, track))
        return NONE;                          /* a kit parameter on the FX track, a sound's on a drum track */
    if (!flags_made)
        make();
    min = core_lock_min(p);
    max = core_lock_max(p);
    if (!(s = core_audio_sound(track)))       /* the sound, or on the FX track the kit */
        return NONE;
    /* the target, in whole steps */
    v = core_store_get(field, (void *)s, 0);
    if ((off = core_lock_value(track, field)) >= 0)
        v = off;
    if ((off = core_scene_value(track, field)) >= 0)
        v = off;
    v = v < min ? min : v > max ? max : v;
    /* smoothed, in 8.8 (the state keeps 16.16) */
    if ((slot = slot_of[code - FIRST_CODE]) != 0)
        v = (step(&states[track][slot - 1], v * 65536) + 0x80) >> 8;
    else
        v *= 256;
    /* the modulations, after the smoother as stock adds them */
    v += core_perf_offset(track, field);
    if ((mod = core_mod_offset(track, field)) != 0)
        v += flags & MOD_RAW ? mod : mod * (max - min) / 127;
    if ((lfo = core_lfo_offset(track, field)) != 0)
        v += flags & LFO_RAW ? lfo : lfo * (max - min) / 127;
    min *= 256;
    max *= 256;
    return v < min ? min : v > max ? max : v;
}

int core_param_value(int track, unsigned field)
{
    return core_param_value_ex(track, field, 0);
}

int core_param_int(int track, unsigned field)
{
    int v = core_param_value(track, field);
    return v == NONE ? v : (v + 0x80) >> 8;
}
