/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiTensor's render: a Tensor-style time-warp pedal. core-dn1 calls it at interrupt level for every block while
 * the page is open (the pedal shell, pedal_dsp.inc, around fx_block). The inputs go onto a tape, 10.9 s around,
 * always (even bypassed, so a HOLD can take what was just played), and a head plays the tape back:
 *   SPEED  -2x .. +2x (64 stops it; 96 is 1x): faster runs into the present and jumps back a window, slower
 *          (or backwards) falls behind and jumps forward one: the TIME window, free or in time with the tempo
 *   PITCH  in STRETCH mode, two grains move the pitch on their own, -24 .. +24 semitones, so SPEED is time
 *          alone; in TAPE mode the pitch follows the speed, as a tape's
 *   HOLD   (T1) loops a window, the last one (LAST) or the next (NEXT), at the speed, the pitch and DIR's
 *          direction (ALT: back and forth); the tape stops while it loops
 *   TAPE STOP (T3) slows the tape to a stop over GLIDE, and starts it again
 *   RANDOM at each beat (or window) maybe a random speed, pitch, reverse or stutter for one beat
 *   SLICE  an SL-2-style gate on the wet, one of eight 16-step patterns; TONE a tilt to dark or thin; WIDTH
 * Fixed point as the shell's; positions on the tape in frames Q8 (the tape is 2^19 frames: 2^27 in Q8). */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dtensor.h"
#include "tables.h"

#define PD_MIX  K_BLEND
#define PD_OUT  S_OUT
#include "pedal_dsp.inc"

#define FMASK   (DT_FR - 1)
#define PMASK   ((DT_FR << 8) - 1)                      /* a position, Q8 */

/* the tape, frame p (masked), interpolated at f/256: L and R into l, r (Q15) */
static inline void rd(u32 p8, s32 *l, s32 *r)
{
    const s16 *T = DT_TAPE;
    u32 a = (p8 >> 8) & FMASK, b = (a + 1) & FMASK;
    s32 f = (s32)(p8 & 255), l0 = T[2 * a], r0 = T[2 * a + 1];
    *l = (l0 + (((T[2 * b] - l0) * f) >> 8)) << 1;
    *r = (r0 + (((T[2 * b + 1] - r0) * f) >> 8)) << 1;
}

static const u16 win_div[10] = { 3, 4, 6, 8, 12, 18, 24, 48, 96, 192 };    /* 1/32 .. 2 bars, quarters x 24 */
static const u16 pat[8] = { 0xAAAA, 0xEEEE, 0x9249, 0xB6DB, 0xF0F0, 0xCCCC, 0xA5A5, 0x8888 };  /* SLICE's steps */
static const s16 ev_semi[6] = { -12, -7, -5, 5, 7, 12 };
static const s16 ev_spd[5] = { 2048, 3072, 6144, 8192, 1024 };   /* x0.5 x0.75 x1.5 x2 x0.25, Q12 */

static u32 wpos;                                /* the write head, frames */
static s32 lag8;                                /* the play head behind it, Q8 frames (the live head) */
static s32 spd;                                 /* the speed now, Q12, signed */
static u32 xf_pos8;                             /* the head fading out after a jump: where, Q8 */
static s32 xf_g;                                /* its gain, Q15 */
static u32 g_pos[2];                            /* STRETCH's grains: positions Q8 */
static s32 g_inc[2];                            /* their steps, Q8 a frame, signed */
static u32 g_ph, g_len, g_inv;                  /* the grains' phase (frames), length and 2^32 / length */
static int hstate;                              /* H_* */
static u32 h_start, h_len, h_cnt;               /* the loop: its first frame, its length, NEXT's count */
static s32 h_ph8;                               /* the head in the loop, Q8 frames from its start */
static int h_stut;                              /* the loop is a random stutter's */
static int alt;                                 /* ALT's direction now: 1 backwards */
static int stopping;                            /* TAPE STOP */
static u32 evc;                                 /* frames to the next event point */
static int ev, ev_semis;                        /* the random event now, and its pitch or speed */
static s32 ev_mul;
static u32 sl_ph;                               /* SLICE's frames into the bar */
static s32 sl_g;                                /* its gain, Q15 */
static s32 tn0, tn1;                            /* TONE's one-poles */
static u32 last_q;                              /* the timeline's quarter, for QUANT */
static int armed;                               /* a HOLD waiting for the beat */

static void fx_reset(void)
{
    wpos = 0;
    lag8 = 256;
    spd = 4096;
    xf_g = 0;
    g_ph = g_len = 0;
    hstate = H_OFF;
    h_stut = 0;
    alt = stopping = armed = 0;
    evc = 0;
    ev = 0;
    sl_ph = 0;
    sl_g = 32767;
    tn0 = tn1 = 0;
    ST.flag[F_HOLD] = H_OFF;
}

static void hold_off(void)
{
    if (hstate == H_LOOP) {                     /* the loop's head fades out; the live head starts behind the
                                                   present by as much as the loop's head was into it */
        xf_pos8 = (h_start << 8) + (u32)h_ph8;
        xf_g = 32767;
        lag8 = 256;
    }
    hstate = H_OFF;
    h_stut = 0;
    armed = 0;
}

static void hold_start(u32 win)
{
    if (ST.opt[O_CAPT] == CAPT_NEXT && !h_stut) {
        hstate = H_CAPTURE;
        h_start = wpos;
        h_len = win;
        h_cnt = 0;
    } else {
        hstate = H_LOOP;
        h_start = wpos - win;
        h_len = win;
        h_ph8 = 0;
        xf_pos8 = (wpos << 8) - (u32)lag8;      /* the live head fades out */
        xf_g = 32767;
    }
}

static void fx_cmd(int c)
{
    if (c == CMD_HOLD_ON) {
        if (hstate == H_OFF || h_stut) {
            h_stut = 0;
            if (ST.opt[O_QUANT] && qfr)
                armed = 1;
            else
                hold_start(ST.val[V_WIN] ? ST.val[V_WIN] : 24000);
        }
    } else if (c == CMD_HOLD_OFF) {
        hold_off();
    } else if (c == CMD_STOP_ON) {
        stopping = 1;
    } else if (c == CMD_STOP_OFF) {
        stopping = 0;
    }
}

static u32 window(void)                         /* TIME: in time, 1/32 .. 2 bars; free, 30 ms .. 4 s */
{
    u32 w = qfr ? sync_frames(sk[K_TIME], win_div, 10) : (u32)kt32(dt_time, sk[K_TIME]);
    if (w < 480)
        w = 480;
    if (w > DT_FR / 2)
        w = DT_FR / 2;
    return w;
}

/* An event point: each beat (RSYNC), or each window. RANDOM may start an event there; one ends there. */
static void event_point(u32 win)
{
    int on = ST.sw[SW_RAND] || ST.held[SW_RAND];
    u32 p = ST.held[SW_RAND] ? 128 : (u32)ST.knob[K_RANDOM];
    if (h_stut)
        hold_off();
    ev = 0;
    if (ST.opt[O_DIR] == DIR_ALT)
        alt = !alt;
    if (!on || rnd_n(128) >= p)
        return;
    {
        u32 w0 = ST.sec[S_RSPD], w1 = ST.sec[S_RPCH], w2 = ST.sec[S_RREV], w3 = ST.sec[S_RSTU], tot, r;
        if (!(w0 | w1 | w2 | w3))
            w0 = w1 = w2 = w3 = 1;
        tot = w0 + w1 + w2 + w3;
        r = rnd_n(tot);
        if (r < w0) {
            ev = 1;
            ev_mul = ev_spd[rnd_n(5)];
        } else if (r < w0 + w1) {
            ev = 2;
            ev_semis = ev_semi[rnd_n(6)];
        } else if (r < w0 + w1 + w2) {
            ev = 3;
        } else if (hstate == H_OFF) {
            ev = 4;
            h_stut = 1;
            hold_start(win >> rnd_n(3));       /* a window, a half or a quarter, over and over */
        }
    }
}

static void tone(s32 *w)                        /* TONE: below 64 a low-pass down to 300 Hz, above a high-pass up to
                                                   3 kHz */
{
    s32 k = sk[K_TONE] - 16384, a, t0 = tn0, t1 = tn1;
    int i;
    if (k > -512 && k < 512)
        return;
    if (k < 0) {
        a = pd_op[255 - (((-k) * 104) >> 14)];  /* 20 kHz .. 300 Hz */
        for (i = 0; i < 2 * N; i += 2) {
            w[i] = LP(t0, w[i], a);
            w[i + 1] = LP(t1, w[i + 1], a);
        }
    } else {
        a = pd_op[(k * 166) >> 14];             /* 20 Hz .. 3 kHz */
        for (i = 0; i < 2 * N; i += 2) {
            w[i] -= LP(t0, w[i], a);
            w[i + 1] -= LP(t1, w[i + 1], a);
        }
    }
    tn0 = t0, tn1 = t1;
}

static void fx_block(s32 *w, const s32 *x, int mode)
{
    s16 *T = DT_TAPE;
    u32 win = window(), xfn, glen;
    s32 tgt, sp0, dsp, ratio = 4096, gain, xfstep;
    int i, stretch = ST.opt[O_MODE] == MODE_STRETCH, rev;
    const s32 *src = mode == PD_RUN ? w : mode == PD_IDLE ? x : 0;

    ST.val[V_WIN] = win;

    /* HOLD's beat (QUANT): the Digitone's quarter on its timeline, or the tempo's own count */
    if (armed) {
        u32 q = ST.clock == CL_TEMPO ? fw_timeline / 21600000u : ST.blocks * N / (qfr ? qfr : 1);
        if (q != last_q || !qfr) {
            armed = 0;
            hold_start(win);
        }
        last_q = q;
    } else {
        last_q = ST.clock == CL_TEMPO ? fw_timeline / 21600000u : ST.blocks * N / (qfr ? qfr : 1);
    }
    evc += N;
    if (evc >= (ST.opt[O_RSYNC] && qfr ? qfr : win)) {
        evc = 0;
        event_point(win);
    }

    /* the speed: the knob, the direction, the event, TAPE STOP; glided */
    tgt = (sk[K_SPEED] - 16384) >> 1;           /* Q12: 0 -2x, 64 stop, 96 1x, 127 2x */
    rev = ST.sw[SW_REV] ^ (ST.opt[O_DIR] == DIR_REV) ^ (ST.opt[O_DIR] == DIR_ALT && alt && hstate != H_LOOP)
          ^ (ev == 3);
    if (rev)
        tgt = -tgt;
    if (ev == 1)
        tgt = (tgt * ev_mul) >> 12;
    {
        s32 gk = sk[K_GLIDE] >> 8, tau = 1 + ((gk * gk) >> 2);
        if (stopping) {                         /* a tape slows over 0.3 s at least; STRETCH freezes at once */
            if (stretch)
                tau = 8;
            else if (tau < 450)
                tau = 450;
            tgt = 0;
        }
        sp0 = spd;
        spd += (tgt - spd) / tau;
        if (spd == sp0 && spd != tgt)
            spd += tgt > spd ? 1 : -1;
        dsp = (spd - sp0) / N;
    }
    ST.flag[F_STOP] = (u8)(stopping || (spd > -200 && spd < 200));
    ST.flag[F_REV] = (u8)(spd < 0);
    ST.flag[F_EVENT] = (u8)ev;
    ST.val[V_SPEED] = (u32)spd;

    if (stretch) {
        s32 semi = (((sk[K_PITCH] - 16384) * 24) + (sk[K_PITCH] >= 16384 ? 8192 : -8192)) / 16384 + (ev == 2 ? ev_semis : 0);
        ratio = exp2_q12(semi * 256 / 12);
    }
    glen = (u32)kt32(dt_grain, ss[S_GRAIN]);
    xfn = (u32)kt32(dt_xfade, ss[S_XFADE]);
    xfstep = 32767 / (s32)xfn + 1;

    /* the tape: the input (or silence, for the trails) goes on while it is not looping */
    if (hstate != H_LOOP) {
        for (i = 0; i < N; i++) {
            u32 a = (wpos + i) & FMASK;
            T[2 * a] = src ? (s16)sat16(src[2 * i] >> 1) : 0;
            T[2 * a + 1] = src ? (s16)sat16(src[2 * i + 1] >> 1) : 0;
        }
        if (hstate == H_CAPTURE) {
            h_cnt += N;
            if (h_cnt >= h_len) {               /* NEXT: the window is in; it loops from here */
                hstate = H_LOOP;
                h_ph8 = 0;
                xf_pos8 = (wpos << 8) - (u32)lag8;
                xf_g = 32767;
            }
        }
    }
    ST.flag[F_HOLD] = (u8)(armed ? H_ARMED : hstate);

    if (mode == PD_IDLE) {                      /* bypassed without trails: the tape listens, nothing plays */
        wpos += N;
        return;
    }

    /* the heads: a frame at a time */
    if (!g_len) {
        g_len = glen;
        g_inv = 0xffffffffu / g_len;
    }
    gain = 32767;
    for (i = 0; i < N; i++) {
        s32 l = 0, r = 0, s8, sp = sp0 + dsp * (i + 1);
        u32 now8 = (wpos + (u32)i) << 8, ph[2];
        s8 = sp >> 4;                           /* Q8 */
        ph[0] = g_ph;
        ph[1] = g_ph + (g_len >> 1);
        if (ph[1] >= g_len)
            ph[1] -= g_len;
        if (!stretch) {                         /* a tape: the pitch is the speed's; quiet near a stop */
            s32 a = sp < 0 ? -sp : sp;
            gain = a >= 410 ? 32767 : a * 80;
        }
        if (hstate == H_LOOP) {
            s32 len8 = (s32)(h_len << 8), e, eg;
            u32 base8 = h_start << 8;
            if (ST.opt[O_DIR] == DIR_ALT) {     /* back and forth */
                if (alt)
                    s8 = -s8;
                h_ph8 += s8;
                if (h_ph8 >= len8) {
                    h_ph8 = 2 * len8 - h_ph8 - 1;
                    alt = !alt;
                } else if (h_ph8 < 0) {
                    h_ph8 = -h_ph8;
                    alt = !alt;
                }
            } else {
                h_ph8 += s8;
                while (h_ph8 >= len8)
                    h_ph8 -= len8;
                while (h_ph8 < 0)
                    h_ph8 += len8;
            }
            if (!stretch) {
                rd((base8 + (u32)h_ph8) & PMASK, &l, &r);
                e = h_ph8 >> 8;                 /* the seam: faded over XFADE at both ends */
                if (e > (s32)h_len - e)
                    e = (s32)h_len - e;
                eg = e >= (s32)xfn ? 32767 : e * xfstep;
                l = (l * eg) >> 15;
                r = (r * eg) >> 15;
            } else {
                int k;
                for (k = 0; k < 2; k++) {       /* the grains wrap inside the loop */
                    s32 gl, gr, wg, rel;
                    if (ph[k] == 0) {
                        g_pos[k] = (u32)h_ph8;
                        g_inc[k] = (ratio >> 4) * (s8 < 0 ? -1 : 1);
                    }
                    rel = (s32)g_pos[k];
                    while (rel >= len8) rel -= len8;
                    while (rel < 0) rel += len8;
                    g_pos[k] = (u32)(rel + g_inc[k]);
                    rd((base8 + (u32)rel) & PMASK, &gl, &gr);
                    wg = tri(ph[k] * g_inv);
                    l += (gl * wg) >> 15;
                    r += (gr * wg) >> 15;
                }
            }
        } else {
            lag8 += 256 - s8;
            if (!stopping) {                    /* past the window: a jump, the old head fading out */
                s32 w8 = (s32)(win << 8);
                if (lag8 > w8 || lag8 < 256) {
                    if (!stretch) {
                        xf_pos8 = now8 - (u32)lag8;
                        xf_g = 32767;
                    }
                    lag8 += lag8 > w8 ? -w8 : w8;
                    if (lag8 < 256)
                        lag8 = 256;
                }
            } else if (lag8 > (s32)((DT_FR - 4096) << 8)) {
                lag8 = (s32)((DT_FR - 4096) << 8);
            }
            if (!stretch) {
                rd((now8 - (u32)lag8) & PMASK, &l, &r);
            } else {
                int k;
                for (k = 0; k < 2; k++) {
                    s32 gl, gr, wg, d;
                    if (ph[k] == 0) {           /* a grain starts at the head; it moves at the pitch, and starts
                                                   far enough back that it never reads past the present */
                        s32 ri = (ratio >> 4) * (s8 < 0 ? -1 : 1);
                        d = ri - s8;
                        g_inc[k] = ri;
                        g_pos[k] = now8 - (u32)lag8 - (d > 0 ? (u32)(d * (s32)g_len) : 0);
                    }
                    rd(g_pos[k] & PMASK, &gl, &gr);
                    g_pos[k] += (u32)g_inc[k];
                    wg = tri(ph[k] * g_inv);
                    l += (gl * wg) >> 15;
                    r += (gr * wg) >> 15;
                }
            }
        }
        if (++g_ph >= g_len) {                  /* the grains' cycle: a new length from here */
            g_ph = 0;
            if (g_len != glen) {
                g_len = glen;
                g_inv = 0xffffffffu / g_len;
            }
        }
        if (xf_g > 0) {                         /* the head that jumped away, fading */
            s32 fl, fr;
            rd(xf_pos8 & PMASK, &fl, &fr);
            xf_pos8 += (u32)s8;
            l = ((l * (32767 - xf_g)) >> 15) + ((fl * xf_g) >> 15);
            r = ((r * (32767 - xf_g)) >> 15) + ((fr * xf_g) >> 15);
            xf_g -= xfstep;
        }
        w[2 * i] = (l * gain) >> 15;
        w[2 * i + 1] = (r * gain) >> 15;
    }
    wpos += hstate == H_LOOP ? 0 : N;

    tone(w);

    {                                           /* WIDTH: the wet's sides, 0 .. 2x */
        s32 wd = ss[S_WIDTH] >> 6;              /* 256 = 1x */
        if (wd < 252 || wd > 260)
            for (i = 0; i < 2 * N; i += 2) {
                s32 m = (w[i] + w[i + 1]) >> 1, s = ((w[i] - w[i + 1]) >> 1) * wd >> 8;
                w[i] = lim(m + s, TOP);
                w[i + 1] = lim(m - s, TOP);
            }
    }

    {                                           /* SLICE: the pattern's 16 steps a bar (or four windows) */
        s32 depth = sk[K_SLICE] >> 7;           /* 0 .. 255 */
        u32 bar = qfr ? qfr * 4 : win * 4, step = bar / 16;
        if (depth > 2 && step) {
            u16 bits = pat[ST.opt[O_PATN] & 7];
            for (i = 0; i < N; i++) {
                u32 s = sl_ph / step;
                s32 tg = (bits >> (15 - (s & 15))) & 1 ? 32767 : 32767 - depth * 128;
                sl_g += (tg - sl_g) >> 5;
                w[2 * i] = (w[2 * i] * sl_g) >> 15;
                w[2 * i + 1] = (w[2 * i + 1] * sl_g) >> 15;
                if (++sl_ph >= bar)
                    sl_ph = 0;
            }
        } else {
            sl_ph = (sl_ph + N) % (bar ? bar : 1);
        }
    }
}
