/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sound-check: a test tone for core-a4mk2 0.5's mod parameters on the Analog
 * Four MKII (OS 1.56), so that each modulation source can be heard on a mod
 * parameter beside a stock parameter set up the same way.
 *
 * mod.json declares two lockable "mod" fields of the synth tracks:
 *   LVL  the tone's level, 0..127 (0, a new sound's value, is silent);
 *   PIT  its pitch, 0..48 semitones above A2 (110 Hz), so 0..4 octaves.
 * They are knobs A and B of SOUND CHECK, behind the AMP key on a synth track
 * (after stock's AMP page, and after LOCK CHECK when lock-check is in the
 * build). With a trig held a knob sets that trig's p-lock. LVL also has a CC
 * (from the lockfile); both have their NRPN (MSB 0, LSB the code).
 *
 * Every audio block (ev_render_out) it reads both on each synth voice with
 * core_param_values_voice, so the voice's p-locks, Param Slide, its routes
 * (velocity, bend, wheel, breath, aftertouch), the macros, CONTROL IN and
 * its LFO1, LFO2, EnvF and Env2 Dest A and B move the tone as they move a
 * stock parameter of that voice. Each voice whose LVL is above 0 plays a
 * sine at its PIT; the four are added to the block's effects' return.
 *
 * Where the sound goes. The A4's voices are analog; the audio interrupt
 * (0x401013be) runs the send effects. Its DAC block is half of the TX ring
 * at 0x80002000 (two halves of 32 frames, 8 words a frame, 48 kHz): at its
 * entry stock takes the half eDMA channel 54 is not reading (its source
 * address 0xfc0456c0 below 0x80002400: the second half), and 0x400fed12
 * writes the effects' return there as words 4 (left) and 5 (right), signed
 * 24 bits, clamped. The outputs add the return to the voices' dry mix. So
 * the tone is the return's: the effects' levels on the FX track do not
 * change it, the main volume does. ev_render_in notes the half as stock
 * finds it, and ev_render_out, after stock has written the block, adds the
 * tone to its words 4 and 5.
 *
 * sc_seen (magic SNCK) keeps, for the emulator, the count of blocks, the
 * half last written and each voice's LVL and PIT as read (8.8 steps).
 */
#include "analog-four-mk2/store.h"
#include "analog-four-mk2/lock.h"
#include "analog-four-mk2/pages.h"

STORE_FIELD(sc_lvl);
STORE_FIELD(sc_pit);

#define ID_LVL 0x210
#define ID_PIT 0x211
#define LVL_MAX 127
#define PIT_MAX 48

const struct core_lock_param sc_param_lvl = {STORE(sc_lvl), ID_LVL, 0, LVL_MAX, "LVL", "Sound Check Level",
                                             CORE_LOCK_CATEGORY("SCHK")};
const struct core_lock_param sc_param_pit = {STORE(sc_pit), ID_PIT, 0, PIT_MAX, "PIT", "Sound Check Pitch",
                                             CORE_LOCK_CATEGORY("SCHK")};

/* ---- the page: the current track's sound of the current kit ---- */

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

static void *sound(void)
{
    unsigned char *k = kit();
    int t;
    if (!k)
        return 0;
    t = CURRENT_TRACK(APP_TRACKS(APP()));
    return (unsigned)t < 4 ? k + 0x1c + 0x152 * t : 0;
}

static unsigned field(int32_t id)
{
    return id == ID_LVL ? STORE(sc_lvl) : STORE(sc_pit);
}

static int sc_get(int32_t id, int32_t *value)
{
    void *s = sound();
    if (!s)
        return 0;
    *value = core_store_get(field(id), s, 0) << 8;
    return 1;
}

static int sc_delta(void *view, int32_t id, int32_t delta, int32_t func)
{
    void *s = sound();
    int v, top = id == ID_LVL ? LVL_MAX : PIT_MAX;
    (void)view;
    (void)func;
    if (!s)
        return 0;
    v = (int)core_store_get(field(id), s, 0) + (delta > 0 ? (delta + 255) / 256 : -((255 - delta) / 256));
    core_store_set(field(id), s, 0, v < 0 ? 0 : v > top ? top : v);
    return 1;
}

static int sc_text(struct core_text *t)
{
    int v = t->value >> 8, n = 0;
    char *b = t->buf;
    if (v < 0)
        v = 0;
    if (v >= 100)
        b[n++] = '0' + v / 100;
    if (v >= 10)
        b[n++] = '0' + v / 10 % 10;
    b[n++] = '0' + v % 10;
    b[n] = 0;
    return CORE_TEXT_DONE;
}

const struct core_param sc_params = {ID_LVL, ID_PIT, sc_get, sc_delta, sc_text};
const struct core_page sc_page = {52, "SOUND CHECK", {ID_LVL, ID_PIT}};
const struct core_view_page sc_view = {CORE_TRACK_SYNTH, CORE_KEY_AMP, 52, 0};

/* ---- the tone ---- */

/* sin(2 pi i / 256) in Q15, i = 0..256 (the last for the interpolation) */
static const int16_t sine[257] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739,
    9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
    25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521,
    32609, 32678, 32728, 32757, 32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
    32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571, 30273, 29956, 29621, 29268,
    28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151,
    15446, 14732, 14010, 13279, 12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
    6393, 5602, 4808, 4011, 3212, 2410, 1608, 804, 0, -804, -1608, -2410,
    -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
    -20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956, -30273, -30571, -30852, -31113,
    -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580,
    -31356, -31113, -30852, -30571, -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
    -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011,
    -3212, -2410, -1608, -804, 0
};

/* 2^(i / 12) in Q14, i = 0..12 */
static const uint16_t semis[13] = {
    16384, 17358, 18390, 19484, 20643, 21870, 23170, 24548, 26008, 27554, 29193, 30929, 32768
};

#define TX_RING 0x80002000u             /* two halves of 32 frames x 8 words */
#define TX_SADDR (*(const volatile uint32_t *)0xfc0456c0)   /* eDMA 54's source */
#define FRAMES 32
#define BASE_INC 38446                  /* 110 Hz at 48 kHz: 2^32 x 110 / 48000, / 256 */
#define TOP 0x7fffff                    /* the DAC word's range, 24 bits */

struct seen {
    char magic[4];
    int32_t blocks;
    int32_t half;                       /* the TX half last written (0, 1) */
    int32_t lvl[4], pit[4];             /* voices 0..3 as read, 8.8 steps (-1: none) */
};
volatile struct seen sc_seen = {{'S', 'N', 'C', 'K'}, 0, 0, {-1, -1, -1, -1}, {-1, -1, -1, -1}};

static int half;
static int handles[2];
static uint32_t phase[4];
static int gain[4];                     /* the last block's level, 8.8 */

void sc_render_in(void)
{
    half = TX_SADDR < TX_RING + 0x400 ? 1 : 0;   /* as stock's entry finds it */
}

/* a sine of the phase, Q15, interpolated */
static int sine_of(uint32_t p)
{
    int i = p >> 24, f = (p >> 16) & 0xff;
    return sine[i] + (((sine[i + 1] - sine[i]) * f) >> 8);
}

/* the phase step a frame for a pitch in 8.8 semitones above A2 */
static uint32_t step_of(int pit)
{
    int s = pit >> 8, f = pit & 0xff, r = s % 12;
    int ratio = semis[r] + (((semis[r + 1] - semis[r]) * f) >> 8);   /* Q14 */
    return ((uint32_t)(BASE_INC * ratio) >> 6) << (s / 12);
}

void sc_render_out(void)
{
    int v, n, out[2], any = 0;
    int add[FRAMES];
    volatile int32_t *w = (volatile int32_t *)(TX_RING + ((uint32_t)half << 10)) + 4;
    if (!handles[0]) {
        handles[0] = core_param_handle(STORE(sc_lvl));
        handles[1] = core_param_handle(STORE(sc_pit));
    }
    for (n = 0; n < FRAMES; n++)
        add[n] = 0;
    for (v = 0; v < 4; v++) {
        int lvl, pit, g0, dg;
        uint32_t p, st;
        core_param_values_voice(v, handles, 2, 0, out);
        lvl = out[0] == CORE_PARAM_NONE ? 0 : out[0];
        pit = out[1] == CORE_PARAM_NONE ? 0 : out[1];
        sc_seen.lvl[v] = out[0] == CORE_PARAM_NONE ? -1 : lvl;
        sc_seen.pit[v] = out[1] == CORE_PARAM_NONE ? -1 : pit;
        g0 = gain[v];
        gain[v] = lvl;
        if (!g0 && !lvl)
            continue;                   /* silent: the phase waits */
        any = 1;
        dg = lvl - g0;                  /* the level ramps over the block */
        p = phase[v];
        st = step_of(pit);
        for (n = 0; n < FRAMES; n++) {
            int g = g0 + ((dg * (n + 1)) >> 5);         /* 8.8, to 127; FRAMES = 32 */
            add[n] += (sine_of(p) * g) >> 9;            /* 127: about 2^21, -12 dB */
            p += st;
        }
        phase[v] = p;
    }
    if (any)
        for (n = 0; n < FRAMES; n++, w += 8) {
            int l = w[0] + add[n], r = w[1] + add[n];
            w[0] = l > TOP ? TOP : l < -TOP - 1 ? -TOP - 1 : l;
            w[1] = r > TOP ? TOP : r < -TOP - 1 ? -TOP - 1 : r;
        }
    sc_seen.half = half;
    sc_seen.blocks++;
}
