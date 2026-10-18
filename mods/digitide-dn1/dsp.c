/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiTide's render: a two-channel pedal. core-dn1 calls it at interrupt level for every block while
 * the page is open (the pedal shell, pedal_dsp.inc, around fx_block). Two channels, each on or off:
 *   the wet channel    REVERB (a cluster of echoes that MODIFY smears into a reverb; TIME its size and decay),
 *                      DELAY (TIME, MODIFY the feedback, a looper at the top) or GRAB (the last moment played
 *                      back at MODIFY's speed and direction, refreshed every TIME); FREEZE holds it
 *   the looper         always listening: a ring keeps the last 15 s (at 48K); turning it on loops the last
 *                      LENGTH of it, in ENV (the input holds a slice), TAPE (speed and direction, octaves) or
 *                      STRETCH (grains: time apart from pitch); OVERDUB adds the input to the loop
 * ROUTING decides what the wet channel hears when both are on: the input, the input and the loop, or the loop.
 * CLOCK is the sample rate: both channels run at a tick rate from 2K to 48K (the frames between ticks are
 * held and smoothed; a one-pole before keeps the worst of the aliasing out). Everything the channels keep is
 * counted in ticks, so a lower CLOCK makes the loops and the echoes longer, lower and darker.
 * The block: the input to ticks -> the looper -> the wet channel (fed per ROUTING) -> the ring (what you hear,
 * while the looper listens) -> back to frames -> the mix (MIX, BALANCE, DIRECT, DRY KILL).
 * Fixed point as the shell's; the ring and the lines hold s16 at half scale. */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dtide.h"
#include "tables.h"

#define PD_MIX  -1                              /* the pedal does its own mix: w is the whole output */
#define PD_OUT  S_OUT
#include "pedal_dsp.inc"

#define WMASK   (MD_WL_FR - 1)
#define RVM     (MD_REV_LINE - 1)

/* The MIDI chart's bands, which are also the knobs' steps */
static const u8 time_band[5] = { 13, 37, 62, 85, 111 };            /* TIME (CC 14), LENGTH (CC 16): six bands */
static const u16 time_div[6] = { 3, 6, 8, 12, 18, 24 };            /* 1/32 1/16 1/8T 1/8 1/8D 1/4, quarters x 24 */
static const u16 len_div[6] = { 6, 12, 24, 48, 96, 192 };          /* x/32 .. x1, x two bars */
static const u8 sync_div[6] = { 16, 8, 6, 4, 3, 2 };               /* SYNC WET: TIME as parts of the loop */
static const u8 clk_band[9] = { 5, 16, 29, 41, 54, 70, 84, 96, 110 };  /* CLOCK (CC 18): 2K .. 32K, then 48K */
static const u8 tape_band[7] = { 5, 24, 43, 62, 85, 98, 120 };     /* TAPE's speed (CC 19) */
static const s16 tape_spd[8] = { -16384, -8192, -4096, -2048, 2048, 4096, 8192, 16384 };   /* Q12 */
static const u8 str_band[8] = { 16, 31, 47, 61, 81, 97, 112, 127 }; /* STRETCH's (CC 19) */
static const s16 str_spd[9] = { -4096, -2731, -2048, -1024, 0, 1024, 2048, 2731, 4096 };   /* its progress, Q12 */

static int band(int v, const u8 *t, int n)
{
    int i = 0;
    while (i < n && v >= t[i])
        i++;
    return i;
}

static inline s32 g12(s32 a, s32 b, s32 g)      /* from a to b by g, Q12 */
{
    return a + (((b - a) * g) >> 12);
}

/* A knob's exponential curve, worked out rather than tabled (the RAM the mods share is small): base times
 * 2^(oct/256 octaves over the knob's travel), kq the knob in 8.8; base up to 32767. */
static u32 expk(u32 base, s32 kq, s32 oct)
{
    return (base * ((u32)exp2_q12(kq * oct / 32512) >> 4)) >> 8;
}

static u32 fade_q15(s32 kq)                     /* FADE: the loop's level a pass while overdubbing, 1 - (1 - u)^2 */
{
    s32 d = 32512 - kq;
    return d <= 0 ? 32768u : (u32)(32768 - ((d * d) >> 15));
}

/* ---- the clock, the times ----------------------------------------------------------------------------- */
static u32 ramp_ph;
static int clk_step;

static u32 clock_rate(void)                     /* the ticks a frame, Q16: CLOCK, its steps or smooth, and RAMP's
                                                   bounce (from the knob down to the bottom and back) */
{
    s32 kq = sk[K_CLK];
    if (ss[S_RAMP] >= 256) {
        ramp_ph += lfo_inc(kt32(pd_rate, ss[S_RAMP])) * N;
        kq -= (kq * tri(ramp_ph)) >> 15;
    }
    clk_step = band(kq >> 8, clk_band, 9);
    return ST.cfg[C_SMOOTH] ? expk(2731, kq, 1174) : (u32)md_clk_step[clk_step];
}

static u32 len_ticks(void);
static int lon;                                 /* the looper plays (its latch, as the render took it) */
static u32 cap;                                 /* the loop's length, ticks */

static u32 wet_time(int grab)                   /* TIME, in ticks: a division of the tempo, the knob's own time, or
                                                   (SYNC WET) a part of the loop */
{
    int b = band(sk[K_TIME] >> 8, time_band, 5);
    u32 t;
    if (ST.opt[O_SYNC] == SY_WET)
        t = (lon ? cap : len_ticks()) / sync_div[b];
    else if (qfr)
        t = qfr * time_div[b] / 24u;
    else
        t = grab ? expk(1200, sk[K_TIME], 1280) : expk(960, sk[K_TIME], 1512);
    if (t < 64)
        t = 64;
    if (t > (grab ? 40000u : MD_WL_FR - 256))
        t = grab ? 40000u : MD_WL_FR - 256;
    return t;
}

static u32 len_ticks(void)                      /* LENGTH, in ticks: a division of two bars, the knob's own time, or
                                                   (SYNC LOOP) four of the wet's TIME; LOOP BUFFER HALF halves it */
{
    u32 t;
    if (ST.opt[O_SYNC] == SY_LOOP)
        t = 4 * wet_time(0);
    else if (qfr)
        t = qfr * len_div[band(sk[K_LEN] >> 8, time_band, 5)] / 24u;
    else
        t = expk(1920, sk[K_LEN], 2039);
    if (ST.cfg[C_HALF])
        t >>= 1;
    if (t < 256)
        t = 256;
    if (t > MD_RT - 8192)
        t = MD_RT - 8192;
    return t;
}

/* ---- the ring and the looper --------------------------------------------------------------------------- */
static u32 rw;                                  /* the ring's write head, ticks */
static u32 l_a;                                 /* the loop's first tick in the ring */
static s32 lg;                                  /* the loop's gain, Q12: it fades in and out */
static s32 q8, p8;                              /* TAPE's head; ENV's and STRETCH's, Q8 ticks into the loop */
static u32 rec;                                 /* the overdub's head, ticks into the loop */
static u32 gph, glen, ginv;                     /* STRETCH's grains: the phase, the length, 2^32 / the length */
static s32 gs8[2];                              /* each grain's start */
static s32 envv, sa8, eq8, exg;                 /* ENV: the input's level, the held slice, its head, the fade */
static int trig;
static u32 pan_ph;                              /* SPREAD's pans */

/* The ring at off8 (Q8 ticks, negative before the loop) from the loop's start: L and R into l, r (Q15). */
static inline void rr(s32 off8, s32 *l, s32 *r)
{
    const s16 *T = MD_RING;
    s32 k = (s32)l_a + (off8 >> 8), k1, f = off8 & 255, a0, b0;
    if (k < 0)
        k += (s32)MD_RT;
    else if (k >= (s32)MD_RT)
        k -= (s32)MD_RT;
    k1 = k + 1 == (s32)MD_RT ? 0 : k + 1;
    a0 = T[2 * k];
    b0 = T[2 * k + 1];
    *l = (a0 + (((T[2 * k1] - a0) * f) >> 8)) << 1;
    *r = (b0 + (((T[2 * k1 + 1] - b0) * f) >> 8)) << 1;
}

/* A region of the ring that loops (the loop, or a slice of it: b8, len8) at q8 in it: over its last xf ticks it
 * fades into what came before its start, which leads into its first tick, so the seam is smooth whichever way
 * the head goes. */
static __attribute__((noinline)) void rseam(s32 b8, s32 len8, s32 xf, s32 xinv, s32 q, s32 *l, s32 *r)
{
    s32 e = (len8 - q) >> 8;
    rr(b8 + q, l, r);
    if (e < xf) {
        s32 l2, r2, g = e * xinv;
        rr(b8 + q - len8, &l2, &r2);
        *l = g12(l2, *l, g);
        *r = g12(r2, *r, g);
    }
}

static inline void pan(s32 *l, s32 *r, s32 p)    /* p -32767 .. 32767: a balance, both at 1 in the middle */
{
    if (p > 0)
        *l = (*l * (32767 - p)) >> 15;
    else
        *r = (*r * (32767 + p)) >> 15;
}

static void catch_loop(void)                    /* the looper on: the last LENGTH of the ring is the loop */
{
    cap = len_ticks();
    l_a = rw >= cap ? rw - cap : rw + MD_RT - cap;
    q8 = p8 = 0;
    rec = 0;
    gph = 0;
    glen = 0;
    trig = 0;
    exg = 0;
    lon = 1;
}

static __attribute__((noinline)) void looper(const s32 *xi, s32 *lo, int nt, int mode)
{
    s32 gt, xf, xinv, len8, l, r;
    int lm = ST.opt[O_LOOP], i, dub, spr = ST.opt[O_SPRD] == SP_BOTH || ST.opt[O_SPRD] == SP_LOOP;
    u32 la, fade = 32768;
    s16 *W = MD_RING;

    if (ST.sw[SW_LOOP] && !lon)
        catch_loop();
    else if (!ST.sw[SW_LOOP] && lon)
        lon = 0;
    gt = lon && mode == PD_RUN ? 4096 : 0;
    dub = lon && mode == PD_RUN && ST.sw[SW_DUB];
    ST.flag[F_LOOP] = (u8)(lon ? 1 + dub : 0);
    if (!lg && !gt) {                           /* silent: nothing to play */
        ST.flag[F_ENV] = 0;
        ST.val[V_LEN] = len_ticks();
        for (i = 0; i < 2 * nt; i++)
            lo[i] = 0;
        return;
    }
    la = cap;
    if (lm == LOOP_TAPE) {                      /* TAPE: LENGTH shortens the loop from its start */
        u32 t = len_ticks();
        if (t < la)
            la = t;
    }
    len8 = (s32)(la << 8);
    xf = (s32)expk(48, ss[S_XFAD], 1512);
    if (xf > (s32)la >> 2)
        xf = (s32)la >> 2;
    if (xf < 4)
        xf = 4;
    xinv = 4096 / xf;
    if (dub)
        fade = ST.opt[O_NDUB] ? 0 : fade_q15(ss[S_FADE]);
    ST.val[V_LEN] = la;

    if (lm == LOOP_TAPE) {                      /* a tape: the speed in octaves, either way */
        s32 sp = tape_spd[band(sk[K_LMOD] >> 8, tape_band, 7)] >> 4, q = q8;
        if (q >= len8)
            q %= len8;
        for (i = 0; i < nt; i++) {
            q += sp;
            if (q >= len8)
                q -= len8;
            else if (q < 0)
                q += len8;
            rseam(0, len8, xf, xinv, q, &l, &r);
            if (spr) {                          /* SPREAD: the left plays a copy backwards */
                s32 l2, r2;
                rseam(0, len8, xf, xinv, len8 - 1 - q, &l2, &r2);
                l = l2;
            }
            lo[2 * i] = l;
            lo[2 * i + 1] = r;
        }
        q8 = q;
        ST.flag[F_ENV] = 0;
    } else if (lm == LOOP_STR) {                /* STRETCH: two grains from a head that moves at MODIFY's rate */
        s32 sp = str_spd[band(sk[K_LMOD] >> 8, str_band, 8)] >> 4, p = p8, dir = sp < 0 ? -1 : 1;
        u32 G = expk(192, sk[K_LEN], 2039), ph = gph, gl = glen, gi = ginv, pp = pan_ph, pinc;
        if (G > la)
            G = la;
        if (G < 64)
            G = 64;
        if (!gl) {
            gl = G;
            gi = 0xffffffffu / gl;
        }
        if (p >= len8)
            p %= len8;
        pinc = (u32)(40 + absv(sp)) * 1000u;    /* SPREAD: a slow pan, faster as it stretches less */
        for (i = 0; i < nt; i++) {
            int k;
            l = r = 0;
            p += sp;
            if (p >= len8)
                p -= len8;
            else if (p < 0)
                p += len8;
            for (k = 0; k < 2; k++) {
                u32 h = ph + (k ? gl >> 1 : 0);
                s32 pos, a, b, wg;
                if (h >= gl)
                    h -= gl;
                if (h == 0)
                    gs8[k] = p;
                pos = gs8[k] + dir * (s32)(h << 8);
                if (pos >= len8)
                    pos -= len8;
                else if (pos < 0)
                    pos += len8;
                rseam(0, len8, xf, xinv, pos, &a, &b);
                wg = tri(h * gi) >> 3;
                l += (a * wg) >> 12;
                r += (b * wg) >> 12;
            }
            if (++ph >= gl) {
                ph = 0;
                if (gl != G) {
                    gl = G;
                    gi = 0xffffffffu / gl;
                }
            }
            if (spr) {
                pp += pinc;
                pan(&l, &r, sinq(pp));
            }
            lo[2 * i] = l;
            lo[2 * i + 1] = r;
        }
        p8 = p, gph = ph, glen = gl, ginv = gi, pan_ph = pp;
        ST.flag[F_ENV] = 0;
    } else {                                    /* ENV: sound at the input holds the slice the loop is in */
        u32 S = expk(192, sk[K_LEN], 2039), pp = pan_ph, pinc;
        s32 thr = sk[K_LMOD] < 128 ? 99999 : (32768 * exp2_q12(-255 - sk[K_LMOD] * 2296 / 32512)) >> 12, p = p8, s8, xs, xsinv, ev = envv, sa = sa8, eq = eq8, eg = exg;
        s32 step;
        if (S > la)
            S = la;
        if (S < 64)
            S = 64;
        s8 = (s32)(S << 8);
        xs = xf > (s32)S >> 2 ? (s32)S >> 2 : xf;
        if (xs < 4)
            xs = 4;
        xsinv = 4096 / xs;
        step = 4096 / (xs < 32 ? 32 : xs);
        pinc = 0x7fffffffu / S;                 /* SPREAD: a pan across two slices */
        if (p >= len8)
            p %= len8;
        if (eq >= s8)
            eq %= s8;
        for (i = 0; i < nt; i++) {
            s32 a = absv(xi[2 * i]), b = absv(xi[2 * i + 1]);
            if (b > a)
                a = b;
            ev = a > ev ? a : ev - (ev >> 10);
            if (!trig && ev > thr) {
                trig = 1;
                sa = (p / s8) * s8;
                if (sa + s8 > len8)
                    sa = len8 - s8;
                eq = p - sa;
            } else if (trig && ev < thr - (thr >> 2)) {
                trig = 0;
            }
            if (trig) {
                eq += 256;
                if (eq >= s8)
                    eq -= s8;
                if (eg < 4096)
                    eg = eg + step > 4096 ? 4096 : eg + step;
            } else {
                p += 256;
                if (p >= len8)
                    p -= len8;
                if (eg > 0)
                    eg = eg - step < 0 ? 0 : eg - step;
            }
            l = r = 0;
            if (eg < 4096)
                rseam(0, len8, xf, xinv, p, &l, &r);
            if (eg > 0) {
                s32 l2, r2;
                rseam(sa, s8, xs, xsinv, eq, &l2, &r2);
                l = g12(l, l2, eg);
                r = g12(r, r2, eg);
            }
            if (spr && trig) {                  /* SPREAD: the held slice pans */
                pp += pinc;
                pan(&l, &r, sinq(pp));
            }
            lo[2 * i] = l;
            lo[2 * i + 1] = r;
        }
        p8 = p, envv = ev, sa8 = sa, eq8 = eq, exg = eg, pan_ph = pp;
        ST.flag[F_ENV] = (u8)trig;
    }

    /* the loop's fade in and out; OVERDUB: the clean input onto the loop at its own head, the loop at FADE */
    {
        s32 g = lg;
        u32 rc = rec, k;
        if (rc >= la)
            rc %= la;
        for (i = 0; i < nt; i++) {
            if (g != gt)
                g += g < gt ? (gt - g < 17 ? gt - g : 17) : (g - gt < 17 ? gt - g : -17);
            lo[2 * i] = (lo[2 * i] * g) >> 12;
            lo[2 * i + 1] = (lo[2 * i + 1] * g) >> 12;
            if (dub) {
                k = l_a + rc;
                if (k >= MD_RT)
                    k -= MD_RT;
                if (fade < 32768) {
                    W[2 * k] = (s16)sat16(((W[2 * k] * (s32)fade) >> 15) + (xi[2 * i] >> 1));
                    W[2 * k + 1] = (s16)sat16(((W[2 * k + 1] * (s32)fade) >> 15) + (xi[2 * i + 1] >> 1));
                } else {
                    W[2 * k] = (s16)sat16(W[2 * k] + (xi[2 * i] >> 1));
                    W[2 * k + 1] = (s16)sat16(W[2 * k + 1] + (xi[2 * i + 1] >> 1));
                }
            }
            if (++rc >= la)
                rc = 0;
        }
        lg = g;
        rec = rc;
        ST.val[V_GRAIN] = expk(192, sk[K_LEN], 2039);
    }
}

/* ---- the wet channel ----------------------------------------------------------------------------------- */
/* REVERB: an eight-line feedback delay network (DigiPrism's SPACE) whose
 * input allpasses MODIFY turns from nothing (a cluster of echoes, as a multi-tap delay) to dense (a reverb);
 * TIME sets the lines' size and their feedback together. L's input into the even lines, R's into the odd. */
static const u16 rv_base8[8] = { 1571, 1861, 2039, 2237, 2473, 2671, 2927, 3163 };
static const u16 ap_len[4] = { 142, 379, 149, 367 };   /* L's two allpasses, R's two */
static u32 rv_w, ap_w[4], rv_ph;
static s32 rv_lp[8], rv_m[2];

static inline s32 rdm(const s16 *L, u32 w, u32 d12)    /* a reverb line, d12 (Q12 ticks) behind w */
{
    u32 p = (w << 12) - d12, a = (p >> 12) & RVM, b = (a + 1) & RVM;
    s32 f = (s32)(p & 0xfff), x0 = L[a];
    return x0 + (((L[b] - x0) * f) >> 12);
}

static inline s32 allpass(s16 *b, u32 *w, u32 n, s32 x, s32 g)     /* a smear allpass, coefficient g (Q15) */
{
    u32 q = *w;
    s32 v = b[q], u = x - ((v * g) >> 15);
    b[q] = (s16)sat16(u);
    *w = q + 1 < n ? q + 1 : 0;
    return v + ((u * g) >> 15);
}

static __attribute__((noinline)) void reverb(s32 *io, int nt, int frz)
{
    s16 *R = MD_REV, *A = MD_REV + 8 * MD_REV_LINE;
    u32 len[8], rw_ = rv_w;
    s32 mult = kt(md_rmul, sk[K_TIME]), fb = frz ? 32768 : kt(md_rfb, sk[K_TIME]);
    s32 lpc = frz ? 32767 : kt(md_damp, ss[S_DAMP]), g = (sk[K_WMOD] * 5) >> 3;
    s32 depth = ss[S_WOBL] + (sk[K_WMOD] >> 2), m0, m1, s0, s1, lp[8], t;
    int i, k;
    for (k = 0; k < 8; k++) {
        len[k] = ((u32)rv_base8[k] * (u32)mult) >> 8;
        lp[k] = rv_lp[k];
    }
    len[0] <<= 12;
    len[1] <<= 12;
    rv_ph += 30000u * (u32)nt;                  /* lines 0 and 1 wander, a quarter cycle apart (Q12 ticks) */
    t = (sinq(rv_ph) * (depth >> 3)) >> 12;
    m0 = rv_m[0];
    s0 = nt ? (t - m0) / nt : 0;
    rv_m[0] = t;
    t = (sinq(rv_ph + 0x40000000u) * (depth >> 3)) >> 12;
    m1 = rv_m[1];
    s1 = nt ? (t - m1) / nt : 0;
    rv_m[1] = t;
    for (i = 0; i < nt; i++) {
        s32 xl = frz ? 0 : io[2 * i] >> 1, xr = frz ? 0 : io[2 * i + 1] >> 1, y[8], s, yl, yr;
        xl = allpass(A, &ap_w[0], ap_len[0], xl, g);            /* the smear: two allpasses a side */
        xl = allpass(A + 1024, &ap_w[1], ap_len[1], xl, g);
        xr = allpass(A + 2048, &ap_w[2], ap_len[2], xr, g);
        xr = allpass(A + 3072, &ap_w[3], ap_len[3], xr, g);
        y[0] = rdm(R, rw_, len[0] + (u32)m0);
        y[1] = rdm(R + MD_REV_LINE, rw_, len[1] + (u32)m1);
        m0 += s0;
        m1 += s1;
        for (k = 2; k < 8; k++)
            y[k] = R[MD_REV_LINE * k + ((rw_ - len[k]) & RVM)];
        s = 0;
        for (k = 0; k < 8; k++) {
            lp[k] += ((y[k] - lp[k]) * lpc + 16384) >> 15;     /* rounded: no offset left in the tail */
            y[k] = lp[k];
            s += y[k];
        }
        s >>= 2;                                /* Householder: y - 2/8 sum y */
        for (k = 0; k < 8; k++)                 /* L's input into the even lines, R's into the odd */
            R[MD_REV_LINE * k + rw_] = (s16)sat16(((((y[k] - s) >> 1) * fb) >> 14) + ((k & 1) ? xr : xl));
        rw_ = (rw_ + 1) & RVM;
        yl = lim(y[0] + y[2] + y[4] + y[6], TOP);
        yr = lim(y[1] + y[3] + y[5] + y[7], TOP);
        io[2 * i] = yl;
        io[2 * i + 1] = yr;
    }
    for (k = 0; k < 8; k++)
        rv_lp[k] = lp[k];
    rv_w = rw_;
}

/* DELAY: a clean delay. A new TIME crossfades to a second head (no pitch bend); MODIFY is the feedback (1 at
 * the top: a looper); WOBL a little wow; SPREAD ping-pong (the sides cross at each repeat). */
static u32 dl_w, dl_cur, dl_old, dl_ph;
static s32 dl_xg, dl_l, dl_r, dl_m;

static __attribute__((noinline)) void delay(s32 *io, int nt, int frz, int spr)
{
    s16 *L = MD_WLINE;
    u32 tgt = wet_time(0) << 12, w = dl_w;
    s32 fb = frz ? 4096 : kt(pd_fb, sk[K_WMOD]), lpc = frz ? 4096 : kt(md_damp, ss[S_DAMP]) >> 3;
    s32 xg = dl_xg, pl = dl_l, pr = dl_r, m, ms, t;
    int i;
    if (!xg && (tgt > dl_cur + (48u << 12) || tgt + (48u << 12) < dl_cur)) {
        dl_old = dl_cur;                        /* a new time: the old head fades out over 20 ms */
        dl_cur = tgt;
        xg = 4096;
    } else if (!xg) {
        dl_cur = tgt;
    }
    dl_ph += 2700u * (u32)nt;                   /* the wow: 0.6 Hz at 48K */
    t = ((sinq(dl_ph) + 32768) * (ss[S_WOBL] >> 7)) >> 7;      /* 0 .. 32 ticks, Q12 */
    m = dl_m;
    ms = nt ? (t - m) / nt : 0;
    dl_m = t;
    for (i = 0; i < nt; i++) {
        s32 yl, yr, wl, wr;
        u32 d = dl_cur + (u32)m;
        yl = rdl(L, WMASK, w, d, 0);
        yr = rdl(L, WMASK, w, d, 1);
        if (xg) {
            d = dl_old + (u32)m;
            yl = g12(yl, rdl(L, WMASK, w, d, 0), xg);
            yr = g12(yr, rdl(L, WMASK, w, d, 1), xg);
            xg = xg > 4 ? xg - 4 : 0;
        }
        m += ms;
        pl += ((yl - pl) * lpc) >> 12;
        pr += ((yr - pr) * lpc) >> 12;
        wl = frz ? 0 : io[2 * i];
        wr = frz ? 0 : io[2 * i + 1];
        if (spr) {
            wl += (pr * fb) >> 12;
            wr += (pl * fb) >> 12;
        } else {
            wl += (pl * fb) >> 12;
            wr += (pr * fb) >> 12;
        }
        L[2 * w] = (s16)sat16(wl >> 1);
        L[2 * w + 1] = (s16)sat16(wr >> 1);
        w = (w + 1) & WMASK;
        io[2 * i] = pl;
        io[2 * i + 1] = pr;
    }
    dl_w = w, dl_xg = xg, dl_l = pl, dl_r = pr;
}

/* GRAB: an auto-sampler. The input goes on the line; a head plays it back at MODIFY's speed (the middle 1x,
 * right faster in semitones to 2x, left backwards, faster to 2x), and every TIME it grabs back to the present,
 * the old head fading out. Frozen, the line stops and the heads repeat the same moment: a tone. */
static u32 sl_w, sl_cnt, sl_pan;
static s32 sl_h8, sl_o8, sl_xg;

static inline s32 rdq(const s16 *L, u32 p8, int c)     /* the line at p8 (Q8 ticks) */
{
    u32 a = (p8 >> 8) & WMASK, b = (a + 1) & WMASK;
    s32 f = (s32)(p8 & 255), x0 = L[2 * a + c];
    return (x0 + (((L[2 * b + c] - x0) * f) >> 8)) << 1;
}

static __attribute__((noinline)) void grab(s32 *io, int nt, int frz, int spr)
{
    s16 *L = MD_WLINE;
    u32 T = wet_time(1), w = sl_w, cnt = sl_cnt, pp = sl_pan, pinc = 0x7fffffffu / T;
    s32 v = sk[K_WMOD] >> 8, r8, h8 = sl_h8, o8 = sl_o8, xg = sl_xg;
    int i;
    if (v >= 64)
        r8 = exp2_q12(((v - 64) * 12 / 63) * 256 / 12) >> 4;
    else
        r8 = -(exp2_q12(((63 - v) * 12 / 63) * 256 / 12) >> 4);
    if (cnt >= T)
        cnt = 0;
    for (i = 0; i < nt; i++) {
        s32 l, r;
        if (cnt == 0) {                         /* a new sample: the head jumps, the old one fades */
            u32 lag = r8 > 256 ? 64 + ((T * (u32)(r8 - 256)) >> 8) : 64;
            o8 = h8;
            xg = 4096;
            h8 = (s32)((w - lag) << 8);
        }
        if (++cnt >= T)
            cnt = 0;
        l = rdq(L, (u32)h8, 0);
        r = rdq(L, (u32)h8, 1);
        h8 += r8;
        if (xg) {
            l = g12(l, rdq(L, (u32)o8, 0), xg);
            r = g12(r, rdq(L, (u32)o8, 1), xg);
            o8 += r8;
            xg = xg > 17 ? xg - 17 : 0;
        }
        if (!frz) {
            L[2 * w] = (s16)sat16(io[2 * i] >> 1);
            L[2 * w + 1] = (s16)sat16(io[2 * i + 1] >> 1);
            w = (w + 1) & WMASK;
        }
        if (spr) {                              /* SPREAD: a pan, a cycle every two samples */
            pp += pinc;
            pan(&l, &r, sinq(pp));
        }
        io[2 * i] = l;
        io[2 * i + 1] = r;
    }
    sl_w = w, sl_cnt = cnt, sl_pan = pp, sl_h8 = h8, sl_o8 = o8, sl_xg = xg;
}

/* ---- the block ------------------------------------------------------------------------------------------ */
static u32 ph16;                                /* the clock's phase, Q16 of a tick */
static s32 aal, aar;                            /* the anti-alias one-poles */
static s32 hw0, hw1, hl0, hl1;                  /* the last tick's wet and loop, held over the frames */
static s32 sw0, sw1, sl0, sl1, tw0, tw1;        /* their smoothing (CLOCK) and the wet's TONE */
static s32 wig;                                 /* the wet's input gain, Q12 (it fades in and out) */
static s32 gwo, dg, bg;                         /* the wet's output gain; the dry's and the bus's, Q12 */

static void fx_reset(void)
{
    int k;
    lon = 0;
    lg = 0;
    rw = 0;
    cap = 0;
    trig = 0;
    envv = 0;
    exg = 0;
    glen = 0;
    ph16 = 0;
    aal = aar = 0;
    hw0 = hw1 = hl0 = hl1 = sw0 = sw1 = sl0 = sl1 = tw0 = tw1 = 0;
    wig = 0;
    gwo = 0;
    dg = 4096;
    bg = 0;
    for (k = 0; k < 8; k++)
        rv_lp[k] = 0;
    for (k = 0; k < 4; k++)
        ap_w[k] = 0;
    rv_m[0] = rv_m[1] = 0;
    rv_w = 0;
    dl_w = 0;
    dl_cur = dl_old = 4800u << 12;
    dl_xg = dl_l = dl_r = dl_m = 0;
    sl_w = sl_cnt = 0;
    sl_h8 = sl_o8 = sl_xg = 0;
}

static void fx_cmd(int c)
{
    (void)c;
}

static void fx_block(s32 *w, const s32 *x, int mode)
{
    static s32 xi[2 * N], we[2 * N], lo[2 * N];
    static u8 fi[N];
    u32 rate = clock_rate(), ph = ph16;
    s32 xq = (s32)(((rate >> 4) * 102944u) >> 12), aa = 4096 * xq / (65536 + xq);     /* Q12: about a quarter of the clock */
    int nt = 0, i, t, full = rate >= 65536;
    int won = ST.sw[SW_WET], frz = won && ST.sw[SW_FRZ], wm = ST.opt[O_WET];
    int spw = ST.opt[O_SPRD] == SP_BOTH || ST.opt[O_SPRD] == SP_WET;
    s32 md = kt(pd_mixd, sk[K_MIX]), mw = kt(pd_mixw, sk[K_MIX]);
    s32 bv = sk[K_BAL] >> 8, gw = bv <= 64 ? 4096 : (127 - bv) * 4096 / 63, gl = bv >= 64 ? 4096 : bv * 64;

    ST.val[V_RATE] = (rate * 375u) >> 9;
    ST.flag[F_CLK] = (u8)clk_step;
    ST.flag[F_WET] = (u8)(won ? 1 + frz : 0);
    ST.val[V_TIME] = wet_time(wm == WET_GRAB);

    /* the input to ticks: at 48K every frame; below, a one-pole first, and a tick when the phase wraps */
    if (full) {
        for (i = 0; i < 2 * N; i++)
            xi[i] = x[i];
        for (i = 0; i < N; i++)
            fi[i] = (u8)i;
        nt = N;
    } else {
        s32 a0 = aal, a1 = aar;
        for (i = 0; i < N; i++) {
            a0 += ((x[2 * i] - a0) * aa) >> 12;
            a1 += ((x[2 * i + 1] - a1) * aa) >> 12;
            ph += rate;
            if (ph >= 65536) {
                ph -= 65536;
                xi[2 * nt] = a0;
                xi[2 * nt + 1] = a1;
                fi[nt++] = (u8)i;
            }
        }
        aal = a0, aar = a1;
    }
    ph16 = ph;

    if (mode == PD_IDLE) {                      /* bypassed, no trails: the ring listens, nothing plays */
        if (lon && ST.sw[SW_LOOP])
            return;
        lon = 0;
        lg = 0;
        for (t = 0; t < nt; t++) {
            MD_RING[2 * rw] = (s16)sat16(xi[2 * t] >> 1);
            MD_RING[2 * rw + 1] = (s16)sat16(xi[2 * t + 1] >> 1);
            if (++rw >= MD_RT)
                rw = 0;
        }
        return;
    }

    looper(xi, lo, nt, mode);

    /* the wet channel's input, per ROUTING when both play, faded in and out */
    {
        int rt = lon ? ST.opt[O_ROUT] : R_IN;
        s32 g = wig, gt = won && mode == PD_RUN ? 4096 : 0;
        for (t = 0; t < nt; t++) {
            s32 a = rt == R_ML ? 0 : xi[2 * t], b = rt == R_ML ? 0 : xi[2 * t + 1];
            if (rt != R_IN) {
                a += lo[2 * t];
                b += lo[2 * t + 1];
            }
            if (g != gt)
                g += g < gt ? 43 : -43;
            if (g < 0 || g > 4096)
                g = g < 0 ? 0 : 4096;
            we[2 * t] = (lim(a, TOP) * g) >> 12;
            we[2 * t + 1] = (lim(b, TOP) * g) >> 12;
        }
        wig = g;
    }
    if (wm == WET_REV)
        reverb(we, nt, frz);
    else if (wm == WET_DLY)
        delay(we, nt, frz, spw);
    else
        grab(we, nt, frz, spw);
    if (spw) {                                  /* WIDTH, with SPREAD on the wet: 0 .. 2x its sides */
        s32 wd = ss[S_WIDTH] >> 6;              /* 256: 1x */
        for (t = 0; t < 2 * nt; t += 2) {
            s32 m = (we[t] + we[t + 1]) >> 1, s = (((we[t] - we[t + 1]) >> 1) * wd) >> 8;
            we[t] = lim(m + s, TOP);
            we[t + 1] = lim(m - s, TOP);
        }
    }

    /* the ring, while the looper listens: what the pedal plays (the input, and the wet channel as MIX has it) */
    if (!lon) {
        s16 *W = MD_RING;
        u32 k = rw;
        for (t = 0; t < nt; t++) {
            s32 a = xi[2 * t], b = xi[2 * t + 1];
            if (mode == PD_RUN && won) {
                a = ((a * md) >> 12) + ((we[2 * t] * mw) >> 12);
                b = ((b * md) >> 12) + ((we[2 * t + 1] * mw) >> 12);
            } else if (mode == PD_RUN) {
                a += we[2 * t];
                b += we[2 * t + 1];
            }
            W[2 * k] = (s16)sat16(a >> 1);
            W[2 * k + 1] = (s16)sat16(b >> 1);
            if (++k >= MD_RT)
                k = 0;
        }
        rw = k;
    }

    /* back to frames, and the mix */
    {
        s32 ta = sk[K_TONE] >= 32000 ? 0 : pd_op[125 + ((sk[K_TONE] * 130) >> 15)] >> 2;   /* TONE: 600 Hz .. open */
        s32 a0 = sw0, a1 = sw1, c0 = sl0, c1 = sl1, d0 = tw0, d1 = tw1;
        s32 w0 = hw0, w1 = hw1, l0 = hl0, l1 = hl1;
        s32 gwt = (won || ST.trails) ? gw : 0, dgt, bgt, gwd, dgd, bgd;
        int both_off = !won && !lon && !lg, thru = won && lon && ST.opt[O_ROUT] != R_IN;
        s32 dr = thru ? ss[S_DIRECT] >> 3 : 4096;
        dgt = ST.opt[O_DRYK] ? 0 : both_off || mode != PD_RUN ? 4096 : md;
        bgt = both_off || mode != PD_RUN ? 4096 : mw;
        gwt = gwo + lim(gwt - gwo, 256);       /* the gains move by 1/16 a block at most: 11 ms off to on */
        dgt = dg + lim(dgt - dg, 256);
        bgt = bg + lim(bgt - bg, 256);
        gwd = (gwt - gwo) / N;
        dgd = (dgt - dg) / N;
        bgd = (bgt - bg) / N;
        for (i = 0, t = 0; i < N; i++) {
            s32 wl, wr, ll, lr;
            if (t < nt && fi[t] == i) {
                w0 = we[2 * t];
                w1 = we[2 * t + 1];
                l0 = lo[2 * t];
                l1 = lo[2 * t + 1];
                t++;
            }
            if (!full) {                        /* the ticks' steps smoothed, at the clock's corner */
                a0 += ((w0 - a0) * aa) >> 12;
                a1 += ((w1 - a1) * aa) >> 12;
                c0 += ((l0 - c0) * aa) >> 12;
                c1 += ((l1 - c1) * aa) >> 12;
            } else {
                a0 = w0, a1 = w1, c0 = l0, c1 = l1;
            }
            if (ta) {                           /* TONE: the wet darker */
                d0 += ((a0 - d0) * ta) >> 12;
                d1 += ((a1 - d1) * ta) >> 12;
            } else {
                d0 = a0, d1 = a1;
            }
            gwo += gwd;
            dg += dgd;
            bg += bgd;
            wl = (d0 * gwo) >> 12;
            wr = (d1 * gwo) >> 12;
            ll = (((c0 * gl) >> 12) * dr) >> 12;
            lr = (((c1 * gl) >> 12) * dr) >> 12;
            if (mode != PD_RUN) {               /* TAILS: the wet's tail alone (the shell adds the dry) */
                w[2 * i] = lim(wl, TOP);
                w[2 * i + 1] = lim(wr, TOP);
            } else {
                w[2 * i] = lim(((x[2 * i] * dg) >> 12) + (((wl + ll) * bg) >> 12), TOP);
                w[2 * i + 1] = lim(((x[2 * i + 1] * dg) >> 12) + (((wr + lr) * bg) >> 12), TOP);
            }
        }
        gwo = gwt, dg = dgt, bg = bgt;
        sw0 = a0, sw1 = a1, sl0 = c0, sl1 = c1, tw0 = d0, tw1 = d1;
        hw0 = w0, hw1 = w1, hl0 = l0, hl1 = l1;
    }
}
