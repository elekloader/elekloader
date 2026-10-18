/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The pedal shell's render (pedal.h): included by a pedal's dsp.c after its header and tables.h; the pedal
 * then defines
 *     static void fx_reset(void);                       the effect's state, from PD(dsp_reset)
 *     static void fx_block(s32 *w, const s32 *x, int mode);
 *     static void fx_cmd(int c);                        a one-shot command from the UI
 * and before the include defines PD_MIX (the knob that mixes the dry and the wet, -1: none, w is the whole output) and PD_OUT
 * (the secondary that sets the output level, -1: none). Each block:
 *   in -> x (the dry, Q15 in an int32; L to both sides with MONO) -> w = x -> fx_block -> w (the wet)
 *   -> MIX (equal power) -> OUTPUT LEVEL -> a soft clip over 0.8 of full scale -> out
 * fx_block's mode: PD_RUN (the pedal is on), PD_TAILS (bypassed with TRAILS: x silent for the effect, its tail
 * is added to the dry at unity) or PD_IDLE (bypassed, no trails: the effect may keep listening, its w is not
 * heard). Fixed point as DigiPrism's: samples Q15 in an int32 (32768 full scale), +-65535 between stages; knobs
 * 0-127, smoothed in 8.8 (sk, ss). */

static void fx_reset(void);
static void fx_block(s32 *w, const s32 *x, int mode);
static void fx_cmd(int c);

#define N       32
#ifndef DTCN0
#define DTCN0   (*(volatile u32 *)0xFC07000Cu)  /* DMA timer 0's counter: the bus clock */
#endif
#define TOP     65535                           /* a sample's bound between stages */
enum { PD_RUN, PD_TAILS, PD_IDLE };

struct pd_state PD(st);
struct core_audio_owner PD(owner) = { 0, PD(render), CORE_AUDIO_MUTE_VOICES };
#define ST PD(st)

/* ---- helpers ---------------------------------------------------------------------------------------- */
static inline s32 lim(s32 v, s32 l) { return v > l ? l : v < -l ? -l : v; }
static inline s32 sat16(s32 v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline s32 absv(s32 v) { return v < 0 ? -v : v; }

static u32 seed = 0x2545f491u;
static inline u32 rnd(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static inline s32 rnd_s(void) { return (s32)(rnd() >> 17) - 16384; }          /* -16384 .. 16383 */
static inline u32 rnd_n(u32 n) { return ((rnd() >> 16) * n) >> 16; }        /* 0 .. n-1, n < 65536 */

/* A knob's curve (129 entries) at kq, the knob in 8.8 */
static inline s32 kt(const s16 *t, s32 kq) { s32 i = kq >> 8, f = kq & 255; return t[i] + (((t[i + 1] - t[i]) * f) >> 8); }
static inline s32 kt32(const s32 *t, s32 kq) { s32 i = kq >> 8, f = kq & 255; return t[i] + (((t[i + 1] - t[i]) * f) >> 8); }

static inline s32 sinq(u32 ph)                  /* Q15, a whole turn in 2^32 */
{
    s32 i = (s32)(ph >> 24), f = (s32)((ph >> 16) & 255);
    return pd_sin[i] + (((pd_sin[i + 1] - pd_sin[i]) * f) >> 8);
}

static inline s32 shape(const s16 *t, s32 v)    /* a waveshaper over -4.0 .. 4.0 of full scale, Q15 out */
{
    u32 u;
    s32 i, f;
    if (v > 131071) v = 131071;
    else if (v < -131072) v = -131072;
    u = (u32)(v + 131072);
    i = (s32)(u >> 8);
    f = (s32)(u & 255);
    return t[i] + (((t[i + 1] - t[i]) * f) >> 8);
}

/* A one-pole low-pass on a local state st (the output x 256); a is Q14 (pd_op), x within +-65535. */
#define LP(st, x, a)    ((st) += (((x) - ((st) >> 8)) * (a)) >> 6, (st) >> 8)

static s32 exp2_q12(s32 o)                      /* 4096 x 2^(o/256) */
{
    s32 r = pd_exp2[o & 255], sh = o >> 8;
    return sh >= 0 ? r << sh : r >> -sh;
}

/* A line of s16 frames, L,R, at half scale, mask + 1 frames: the sample at d12 (frames, Q12) behind frame w. */
static inline s32 rdl(const s16 *L, u32 mask, u32 w, u32 d12, int c)
{
    u32 p = (w << 12) - d12, a = (p >> 12) & mask, b = (a + 1) & mask;
    s32 f = (s32)(p & 0xfff), x0 = L[2 * a + c], x1 = L[2 * b + c];
    return (x0 + (((x1 - x0) * f) >> 12)) << 1;
}

static inline u32 lfo_inc(s32 mhz) { return (u32)((mhz * 5726) >> 6); }      /* a frame's phase step */

static inline s32 tri(u32 p)                    /* a triangle, 0 at 0, 32767 at half way */
{
    return (s32)((p < 0x80000000u ? p : ~p) >> 16);
}

/* ---- the clock and the knobs ------------------------------------------------------------------------ */
static s32 sk[8], ss[8];                        /* the knobs and the secondaries, 8.8, smoothed */
static u32 qfr;                                 /* frames a quarter note, 0 when the clock is free */

u32 PD(tempo)(void)
{
    u32 t;
    if (ST.clock == CL_TEMPO)
        t = (u32)fw_tempo;
    else if (ST.clock == CL_TAP)
        t = ST.tap;
    else
        return 0;
    return t < 2400 || t > 48000 ? 0 : t;       /* 20-400 BPM */
}

/* A synced time: the knob picks one of n subdivisions, in quarter notes x 24 (qfr must be set). */
static u32 sync_frames(s32 kq, const u16 *div24, int n)
{
    int i = (kq * n) >> 15;
    if (i >= n)
        i = n - 1;
    if (i < 0)
        i = 0;
    return qfr * div24[i] / 24u;
}

static void knobs_block(void)
{
    int k;
    for (k = 0; k < 8; k++) {
        sk[k] += ((ST.knob[k] << 8) - sk[k]) >> 2;
        ss[k] += ((ST.sec[k] << 8) - ss[k]) >> 2;
    }
}

/* ---- the CPU guard ------------------------------------------------------------------------------- */
/* The whole render, from its entry to its exit, against the time from one block to the next, on DMA timer 0.
 * Over 93% for 300 blocks running (0.2 s) the unit has no time left for its UI: the guard bypasses the pedal,
 * at once (no trails), and says so. The emulator has no DMA timer 0: there the guard sees nothing. */
static u32 r_in, r_prev, r_over;

void PD(rin)(void)
{
    r_prev = r_in;
    r_in = DTCN0;
}

void PD(rout)(void)
{
    u32 per = r_in - r_prev, busy = DTCN0 - r_in, l;
    if (per < 1000 || per > 400000)
        return;
    l = busy * 1000u / per;
    ST.load = (u16)l;
    if (l > 930 && PD(owner).on && !ST.bypass) {
        if (++r_over >= 300) {
            ST.overload = 1;
            ST.bypass = 1;
            r_over = 0;
        }
    } else if (r_over) {
        r_over--;
    }
}

/* ---- the block ------------------------------------------------------------------------------------ */
static u32 t_prev, quiet;

void PD(dsp_reset)(void)
{
    int k;
    for (k = 0; k < 8; k++) {
        sk[k] = ST.knob[k] << 8;
        ss[k] = ST.sec[k] << 8;
    }
    ST.cr = ST.cw;
    fx_reset();
}

void PD(render)(int32_t *out, const int32_t *in)
{
    static s32 x[2 * N], w[2 * N];
    u32 t0 = DTCN0, tempo = PD(tempo)();
    s32 pk0 = 0, pk1 = 0, tail = 0, ov, md, mw;
    int i, byp = ST.bypass, mode;

    ST.blocks++;
    qfr = tempo ? 345600000u / tempo : 0;
    knobs_block();
    while (ST.cr != ST.cw) {
        fx_cmd(ST.cmdq[ST.cr % PD_NCMD]);
        ST.cr++;
    }
    for (i = 0; i < 2 * N; i++)
        x[i] = in[i] >> 16;
    if (ST.mono)
        for (i = 0; i < 2 * N; i += 2)
            x[i + 1] = x[i];
    mode = !byp ? PD_RUN : ST.trails && !ST.overload ? PD_TAILS : PD_IDLE;
    for (i = 0; i < 2 * N; i++)
        w[i] = mode == PD_RUN ? x[i] : 0;
    fx_block(w, x, mode);

    ov = PD_OUT >= 0 ? kt(pd_vol, ss[PD_OUT >= 0 ? PD_OUT : 0]) : 4096;
    if (mode != PD_RUN) {                       /* the dry, and the tail at unity */
        for (i = 0; i < 2 * N; i++) {
            s32 t = mode == PD_TAILS ? w[i] : 0, y;
            if (absv(t) > tail)
                tail = absv(t);
            y = sat16(x[i] + t);
            out[i] = y << 16;
        }
    } else {
        if (PD_MIX >= 0) {
            md = kt(pd_mixd, sk[PD_MIX >= 0 ? PD_MIX : 0]);
            mw = kt(pd_mixw, sk[PD_MIX >= 0 ? PD_MIX : 0]);
        } else {
            md = 0;
            mw = 4096;
        }
        for (i = 0; i < 2 * N; i++) {
            s32 y = md ? ((x[i] * md) >> 12) + ((w[i] * mw) >> 12) : (mw == 4096 ? w[i] : (w[i] * mw) >> 12);
            if (ov < 4092 || ov > 4100)
                y = (y * ov) >> 12;
            if (y > 26214 || y < -26214)        /* the soft clip's knee: straight below it */
                y = shape(sh_out, lim(y, 131071));
            out[i] = y << 16;
        }
    }
    for (i = 0; i < 2 * N; i += 8) {            /* the meter: every fourth frame */
        s32 y0 = absv(out[i] >> 16), y1 = absv(out[i + 1] >> 16);
        if (y0 > pk0) pk0 = y0;
        if (y1 > pk1) pk1 = y1;
    }
    if (pk0 > ST.meter[0])
        ST.meter[0] = (u16)pk0;
    if (pk1 > ST.meter[1])
        ST.meter[1] = (u16)pk1;

    /* bypassed: once the tail has been quiet half a second, the render has nothing more to add */
    if (mode == PD_RUN)
        quiet = 0;
    else if (mode == PD_TAILS && tail > 16)
        quiet = 0;
    else if (quiet < 750)
        quiet++;
    ST.active = (u8)(mode == PD_RUN || quiet < 750);

    {                                           /* the pedal's share of a block */
        u32 t1 = DTCN0, per = t0 - t_prev;
        if (per)
            ST.cpu = (u16)((t1 - t0) * 1000u / per);
        t_prev = t0;
    }
}
