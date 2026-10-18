/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiHoard's render: an echo collector. core-dn1 calls it at interrupt level for every block while the
 * page is open (the pedal shell, pedal_dsp.inc, around fx_block). The signal flow:
 *   MEMORY  the inputs (mono) go into a 3-minute memory, always while the pedal is on (with COLLECT it overdubs
 *           what was there 3 minutes ago; with ALWAYS it records while bypassed too)
 *   HEADS   two play heads read the memory: the primary at SCAN's distance back (MANUAL: the knob picks it; AUTO:
 *           it wanders off now and then and comes back), the secondary SPREAD further back; both in steps of
 *           SIZE (SPREAD's in quarters of it), so what they play stays in time. A head travelling through the
 *           memory scrubs (the cloudy sound of scanning); a long way it jumps, crossfaded, and scrubs the rest
 *   ECHO    each head feeds an echo line, SIZE long, REPEATS its feedback; the primary's sits left, the
 *           secondary's right (WIDTH). The modifier (bank A or B, number 1-3, MOD its amount) acts on what the
 *           lines play: after the line (OUT: every echo the same), in the feedback (IN: it accumulates), or with
 *           FEED the echo goes back into the memory and REPEATS is the whole loop's feedback
 *   LOOP    freezes the lines (they go round as they are); CLEAR empties the memory and the lines
 *   RAMP    moves one knob, once (up or down to where it is) or bouncing (triangle, random, square)
 * The memory is 8,640,000 frames of s16; positions in it are Q6 frames (8.6M x 64 fits an s32). The lines are
 * 2^17 frames of two s16 channels at half scale; their positions Q8. Fixed point as the shell's. */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")
#include "dhoard.h"
#include "tables.h"

#define PD_MIX  (-1)                                    /* DigiHoard mixes the dry itself: MIX */
#define PD_OUT  S_OUT
#include "pedal_dsp.inc"

#define LMASK   (HB_LFR - 1)
#define MEMFR   HB_MFR
#define DMAX    120000                                  /* SIZE's longest: 2.5 s, leaving the speed modifiers'
                                                           heads room in the lines */
#define JUMP6   (96000 * 64)                            /* a head further than 2 s from its target jumps */
#define SCRUB6  (12000 * 64)                            /* to 0.25 s short of it, and scrubs the rest */
#define VMAX6   (3 * 64)                                /* a head travels at most 3 frames a frame */

/* SIZE's divisions in time: quarters x 24, 1/32 .. a bar */
static const u16 sz_div[13] = { 3, 4, 6, 8, 12, 16, 18, 24, 32, 36, 48, 72, 96 };
/* STEPPED SPEED's steps, Q12: reverse 4x, 2x, 1x, 1/2x; forward 1/2x, 1x (noon), 3/2x, 2x, 3x, 4x */
static const s16 a1_spd[10] = { -16384, -8192, -4096, -2048, 2048, 4096, 6144, 8192, 12288, 16384 };

struct head {
    s32 d;                                              /* frames back, Q6 */
    s32 v;                                              /* its move a frame, Q6 */
    s32 xd, xg;                                         /* after a jump: where it was, that read's gain (Q12) */
};

static u32 mw, mnew, mfill;                             /* the memory: the next frame, the newest, frames held */
static u32 lw, lfill;                                   /* the lines: the next frame (masked on use), held */
static struct head hp, hs;                              /* the primary and the secondary head */
static s32 dcur, dx, dxg;                               /* SIZE now (Q8 frames); after a change the old, fading */
static u32 tp;                                          /* frames into the echo's period */
static u32 acnt;                                        /* AUTO: frames to the next period */
static int away, stay;                                  /* AUTO: away in the past, for so many periods */
static s32 atgt;                                        /* AUTO: where, frames */
static s32 gS, gmd, pan;                                /* the secondary's gain, the memory dry's (Q15), the pan */
static int mid, mnext;                                  /* the modifier playing, the one to come */
static s32 mg;                                          /* the modifier's share, Q12: fades through the plain read
                                                           when it changes */
static u32 gph[2], gln[2], gin[2];                      /* the speed modifiers' two grains: phase, length, 2^32/len */
static s32 gd0[2], gsp[2];                              /* each grain's start (Q8 frames back) and speed (Q12) */
static u32 wph, fph;                                    /* STABILITY: wow and flutter */
static s32 stl[2];                                      /* its low-pass */
static s32 tg;                                          /* TRIMMER's gate, Q12 */
static s32 dg, dtg, drate;                              /* DROPPER: the gain, its target, its speed */
static u32 dcnt, dsi;                                   /* DROPPER: frames to the next slot, the slot */
static s32 svl[2], svb[2];                              /* FILTER */
static s32 tnl[2];                                      /* TONE, in the feedback */
static s32 fbv;                                         /* FEED: what goes back into the memory */
static u32 rph;                                         /* RAMP: its phase */
static int rdone, rexp;                                 /* a one-time ramp done; the expression pedal has it */
static s32 rr, rrt, rexpv;                              /* RAMP: the position (Q15), a random one's target */
static u8 r_tgt, r_shape;
static int prev_run;
static int snap;                                        /* the first block: the heads start where they belong */
static u32 clr_t;                                       /* blocks since CLEAR, for the UI */

static void ramp_restart(void)
{
    rph = 0;
    rdone = 0;
    rr = r_shape == SH_RUP || r_shape == SH_RDN ? 32767 : 0;
}

static void mod_init(int id)
{
    int k;
    for (k = 0; k < 2; k++) {
        gd0[k] = dcur;
        gsp[k] = 4096;
        gln[k] = 4800;
        gin[k] = 0xffffffffu / 4800u;
        stl[k] = svl[k] = svb[k] = 0;
    }
    gph[0] = 0;
    gph[1] = 2400;
    tg = dg = dtg = 4096;
    drate = 6;
    dcnt = 0;
    (void)id;
}

static int mod_id(void)
{
    int b = ST.opt[O_BANK];
    if (b == BANK_OFF)
        return M_NONE;
    return 1 + (b == BANK_B ? 3 : 0) + ST.opt[O_NUM] % 3;
}

static void fx_reset(void)
{
    mw = 0;
    mnew = MEMFR - 1;
    mfill = 0;
    lw = 0;
    lfill = 0;
    hp.d = hp.v = hp.xg = hs.d = hs.v = hs.xg = 0;
    dcur = 24000 << 8;
    dxg = 0;
    tp = acnt = 0;
    away = stay = 0;
    atgt = 0;
    gS = gmd = pan = 0;
    mid = mnext = mod_id();
    mg = 4096;
    mod_init(mid);
    tnl[0] = tnl[1] = 0;
    fbv = 0;
    r_tgt = ST.opt[O_RAMP];
    r_shape = ST.opt[O_SHAPE];
    rexp = 0;
    ramp_restart();
    prev_run = -1;
    snap = 1;
    clr_t = 9999;
}

static void fx_cmd(int c)
{
    if (c == CMD_CLEAR) {                       /* both footswitches: the memory and the echoes start again */
        mfill = 0;
        lfill = 0;
        hp.xg = hs.xg = 0;
        away = 0;
        atgt = 0;
        fbv = 0;
        clr_t = 0;
    } else if (c == CMD_RAMP) {
        ramp_restart();
    } else if (c >= CMD_EXP) {
        rexp = 1;
        rexpv = (c - CMD_EXP) * 258;
    }
}

/* The knobs' curves, worked out rather than kept in tables (the RAM the mods share is small): 2^(o/256), for
 * o >= 0, from pd_exp2; a knob's exponential lo x (hi/lo)^u as 2^(a + u b) with a = 256 log2(lo) and
 * b = 256 log2(hi/lo); SCAN's square law, 0 .. 3 minutes. */
static s32 p2(s32 o)
{
    s32 sh = (o >> 8) - 12;
    return sh >= 0 ? (s32)pd_exp2[o & 255] << sh : (s32)pd_exp2[o & 255] >> -sh;
}

static s32 kexp(s32 kq, s32 a, s32 b)
{
    return p2(a + kq * b / 32512);
}

#define K_SIZE_CURVE(kq)    kexp(kq, 2875, 1444)        /* SIZE, free: 50 ms .. 2.5 s (frames) */
#define K_SPRD_CURVE(kq)    kexp(kq, 2875, 3024)        /* SPREAD: 50 ms .. 3 minutes (frames) */
#define K_RATE_CURVE(kq)    kexp(kq, 1106, 2039)        /* RAMP: 0.02 .. 5 Hz (milli-hertz) */
#define K_GLID_CURVE(kq)    kexp(kq, 2536, 1701)        /* GLIDE: 20 ms .. 2 s (frames) */

static s32 scand(s32 kq)
{
    s32 u = kq >> 7, d = u * u * 134;                   /* (u / 254)^2 x 3 minutes */
    return d > (s32)HB_MFR - 2 ? (s32)HB_MFR - 2 : d;
}

/* The feedback's limiter: straight to half of full scale, then bending towards full scale (the slope stays 1
 * at the knee: 32768 - 2^28 / (the excess + 16384)) */
static inline s32 fbclip(s32 v)
{
    s32 a = v < 0 ? -v : v;
    if (a <= 16384)
        return v;
    a = 32768 - (s32)(268435456u / (u32)(a > 1 << 22 ? 1 << 22 : a));
    if (a > 32767)
        a = 32767;
    return v < 0 ? -a : a;
}

/* The memory d6/64 frames behind its frame w (the newest), Q15; silence past what it holds. */
static inline s32 mrd(u32 w, s32 d6)
{
    const s16 *M = HB_MEM;
    u32 di = (u32)d6 >> 6, a, b;
    s32 f = d6 & 63, x0, x1;
    if (di + 2 > mfill)
        return 0;
    a = w >= di ? w - di : w + MEMFR - di;
    b = a ? a - 1 : MEMFR - 1;
    x0 = M[a];
    x1 = M[b];
    return x0 + (((x1 - x0) * f) >> 6);
}

/* The lines d8/256 frames behind frame w (the one about to be written), both channels, Q15; silence past what
 * they hold. */
static inline void lrd(u32 w, s32 d8, s32 *a, s32 *b)
{
    const s16 *L = HB_LINE;
    u32 p = (w << 8) - (u32)d8, i0 = (p >> 8) & LMASK, i1 = (i0 + 1) & LMASK;
    s32 f = (s32)(p & 255), a0, b0;
    if (((u32)d8 >> 8) + 1 > lfill) {
        *a = *b = 0;
        return;
    }
    a0 = L[2 * i0];
    b0 = L[2 * i0 + 1];
    *a = (a0 + (((L[2 * i1] - a0) * f) >> 8)) << 1;
    *b = (b0 + (((L[2 * i1 + 1] - b0) * f) >> 8)) << 1;
}

/* The echo: the lines at SIZE (and off frames, Q8, further), crossfaded from the old SIZE after a change. */
static __attribute__((noinline)) void eread(u32 w, s32 off, s32 *a, s32 *b)
{
    lrd(w, dcur + off, a, b);
    if (dxg) {
        s32 c, d;
        lrd(w, dx + off, &c, &d);
        *a += ((c - *a) * dxg) >> 12;
        *b += ((d - *b) * dxg) >> 12;
    }
}

/* A head's move for this block towards tgt (Q6), at a time constant of tau frames: far away it jumps */
static void head_move(struct head *h, s32 tgt, s32 tau)
{
    s32 diff = tgt - h->d, v, ad;
    if ((diff > JUMP6 || diff < -JUMP6) && !h->xg) {
        h->xd = h->d;
        h->xg = 4096;
        h->d = tgt - (diff > 0 ? SCRUB6 : -SCRUB6);
        diff = tgt - h->d;
    }
    v = diff / tau;
    if (v > VMAX6)
        v = VMAX6;
    if (v < -VMAX6)
        v = -VMAX6;
    if (!v && diff)
        v = diff > 0 ? 1 : -1;
    ad = diff < 0 ? -diff : diff;
    if ((v < 0 ? -v : v) * N > ad) {
        v = diff / N;
        if (!v)
            h->d = tgt;
    }
    h->v = v;
}

static __attribute__((noinline)) s32 hread(const struct head *h)
{
    s32 s = mrd(mnew, h->d);
    if (h->xg) {
        s32 o = mrd(mnew, h->xd);
        s += ((o - s) * h->xg) >> 12;
    }
    return s;
}

static inline void hstep(struct head *h)
{
    h->d += h->v;
    if (h->d < 0)
        h->d = 0;
    if (h->xg) {
        h->xg -= 4;
        if (h->xg < 0)
            h->xg = 0;
    }
}

/* AUTO's step at each period of SIZE: maybe away into the past (further and more often as u grows), and back */
static void auto_step(s32 u, s32 df)
{
    u32 maxd;
    s32 uu = u >> 7;
    if (u < 256) {
        away = 0;
        atgt = 0;
        return;
    }
    if (away) {
        if (--stay > 0)
            return;
        if (u < 32000) {                        /* home: the present again */
            away = 0;
            atgt = 0;
            return;
        }
    } else if ((s32)rnd_n(64516) >= uu * uu) {  /* the chance a period: (u / 127)^2 */
        return;
    }
    maxd = (u32)scand(u);
    if (mfill < (u32)df * 2 + 2) {
        away = 0;
        atgt = 0;
        return;
    }
    if (maxd > mfill - (u32)df - 2)
        maxd = mfill - (u32)df - 2;
    if (maxd < (u32)df) {
        away = 0;
        atgt = 0;
        return;
    }
    atgt = (s32)(1 + rnd_n(maxd / (u32)df)) * df;
    stay = 1 + (int)rnd_n(1 + (ss[S_STAY] >> 10));
    away = 1;
}

static s32 ramp_apply(s32 kq)
{
    s32 end = r_shape == SH_RUP || r_shape == SH_BBOT ? 0 : 32512;
    return kq + (((end - kq) * rr) >> 15);
}

static void ramp_block(void)
{
    u32 inc = lfo_inc(K_RATE_CURVE(sk[K_RAMP])) * N, o = rph;
    if (r_tgt != ST.opt[O_RAMP] || r_shape != ST.opt[O_SHAPE]) {
        r_tgt = ST.opt[O_RAMP];
        r_shape = ST.opt[O_SHAPE];
        rexp = 0;
        ramp_restart();
    }
    if (rexp) {
        rr = rexpv;
        return;
    }
    switch (r_shape) {
    case SH_RUP:
    case SH_RDN:                                /* from the end to the knob, once */
        if (!rdone) {
            rph += inc;
            if (rph < o)
                rdone = 1;
        }
        rr = rdone ? 0 : 32767 - (s32)(rph >> 17);
        break;
    case SH_BTOP:
    case SH_BBOT:                               /* from the knob to the end and back */
        rph += inc;
        rr = tri(rph);
        break;
    case SH_RAND:                               /* a new random place each half cycle */
        rph += inc;
        if ((rph ^ o) & 0x80000000u)
            rrt = (s32)(rnd() >> 17);
        rr += (rrt - rr) >> 3;
        break;
    default:                                    /* square: the knob, the end */
        rph += inc;
        rr += ((rph & 0x80000000u ? 32767 : 0) - rr) >> 1;
        break;
    }
}

static void fx_block(s32 *w, const s32 *x, int mode)
{
    s16 *M = HB_MEM, *L = HB_LINE;
    s32 ek[8], df, m, am, spdA, spdB, spd, gnext, fbg, fbm, lvl, dryg, wetg, inpt, keep, tna, ka;
    s32 gPL, gPR, gSL, gSR, wdep = 0, fdep = 0, sla = 16384, nlev = 0, svf = 0, svq = 5793, topen = 0, slen = 1,
        dprob = 0;
    u32 winc = lfo_inc(700), finc = lfo_inc(6500);
    int i, k, run = mode == PD_RUN, mem = ST.opt[O_MEM], collect = mem == MEM_CLCT || mem == MEM_BOTH,
        always = mem >= MEM_ALWS, wipe = ST.cfg[C_WIPE] || ST.sw[SW_WIPE], loop = ST.sw[SW_LOOP] != 0,
        fmode = ST.opt[O_MODE], rec, hin, tend = 0, hpf = 0, dpat = 0, moving;

    /* the pedal stepped off: the memory is dumped (but with COLLECT or ALWAYS); on again: the trails stop */
    if (run != prev_run) {
        if (prev_run == 1 && !collect && !always)
            mfill = 0;
        if (run && prev_run == 0 && !collect && !always)
            lfill = 0;
        if (run)
            ramp_restart();
        prev_run = run;
    }
    if (clr_t < 9999)
        clr_t++;
    inpt = kt(pd_vol, ss[S_INPT]);

    if (mode == PD_IDLE) {                      /* bypassed, no trails: ALWAYS records, nothing plays */
        if (always)
            for (i = 0; i < N; i++) {
                s32 in = ((((x[2 * i] + x[2 * i + 1]) >> 1) * inpt) >> 12);
                M[mw] = (s16)sat16(in);
                mnew = mw;
                if (++mw >= MEMFR)
                    mw = 0;
                if (mfill < MEMFR)
                    mfill++;
            }
        ST.val[V_FILL] = mfill;
        ST.flag[F_STATE] = (u8)(clr_t < 1500 ? FS_CLEARED : 0);
        return;
    }
    rec = run || always;                        /* the memory records while on (or ALWAYS) */
    hin = run;                                  /* the heads feed the echo while on */

    /* RAMP: the effective knobs */
    ramp_block();
    for (k = 0; k < 8; k++)
        ek[k] = sk[k];
    if (r_tgt != RT_OFF) {
        static const u8 rk[6] = { 0, K_RPTS, K_SIZE, K_MOD, K_SPRD, K_SCAN };
        int t = rk[r_tgt % 6];
        ek[t] = ramp_apply(sk[t]);
        ST.flag[F_RAMP] = (u8)(ek[t] >> 8);
    }

    /* SIZE: in time (its divisions), or free; a change crossfades from the old to the new (no warping) */
    {
        u32 dt = qfr ? sync_frames(ek[K_SIZE], sz_div, 13) : (u32)K_SIZE_CURVE(ek[K_SIZE]);
        s32 d8;
        while (dt > DMAX)
            dt >>= 1;
        if (dt < 480)
            dt = 480;
        d8 = (s32)(dt << 8);
        if (!dxg && d8 != dcur) {
            dx = dcur;
            dcur = d8;
            dxg = 4096;
        }
    }
    df = dcur >> 8;

    /* the modifier and its amount (MOD: noon neutral, -16384 .. +16128) */
    mnext = mod_id();
    m = ek[K_MOD] - 16384;
    am = m < 0 ? -m : m;
    spdA = a1_spd[(ek[K_MOD] * 10) >> 15];
    if (am < 300)
        spdB = 4096;
    else if (m > 0)
        spdB = exp2_q12((m * 256) / 16128);     /* 1x .. 2x */
    else
        spdB = 4096 + ((m * 3) >> 2);           /* 1x .. -2x, stopped at a third of the way */
    spd = mid == M_SSPD ? spdA : spdB;
    {                                           /* the grains: as long as SIZE (at most 0.5 s), short enough that a
                                                   head never reads past the line's end */
        s32 a = spd > 4096 ? spd - 4096 : 4096 - spd;
        u32 room = HB_LFR - 512 - (u32)df;
        gnext = df > 24000 ? 24000 : df;
        if (a) {
            u32 gm = room * 4096u / (u32)a;
            if ((u32)gnext > gm)
                gnext = (s32)gm;
        }
        if (gnext < 256)
            gnext = 256;
    }
    if (mid == M_STAB) {                        /* wow and flutter to 3 ms, a low-pass, noise clockwise */
        wdep = (am * 36864) >> 14;
        fdep = wdep >> 3;
        sla = pd_op[255 - ((am * 90) >> 14)];
        nlev = m > 0 ? m >> 5 : 0;
    } else if (mid == M_TRIM) {                 /* a step of eighths of SIZE: the end cut (left), the start (right) */
        int st = (am * 8) >> 14;
        tend = m < 0;
        if (st > 7)
            st = 7;
        topen = !st ? -1 : tend ? df * (8 - st) / 8 : df * st / 8;
    } else if (mid == M_FILT) {                 /* low-pass (left) 8 kHz .. 125 Hz; high-pass (right) 40 Hz .. 2 kHz */
        hpf = m > 0;
        svf = hpf ? hb_svf2[(am * 190) >> 14] : hb_svf2[255 - ((am * 200) >> 14)];
        svq = 5793 - ((am * 4200) >> 14);
        if (am < 300)                           /* noon: open */
            svf = 0;
    } else if (mid == M_DROP) {                 /* slots of SIZE: more, and more of them dropped, as MOD grows */
        dpat = m > 0;
        slen = df / (4 + ((am * 60) >> 14));
        if (slen < 64)
            slen = 64;
        dprob = (am * 40000) >> 14;
    }

    /* the heads: SCAN (MANUAL: the knob; AUTO: wandering; T1 held: AUTO at its most), SPREAD behind it */
    {
        s32 tP, tS, tau = K_GLID_CURVE(ss[S_GLID]);
        int mom = ST.sw[SW_SCAN];
        if (mom || ST.opt[O_SCAN] == SCAN_AUTO) {
            acnt += N;
            if (acnt >= (u32)df) {
                acnt -= (u32)df;
                auto_step(mom ? 32512 : ek[K_SCAN], df);
            }
            tP = atgt;
        } else {
            away = 0;
            tP = scand(ek[K_SCAN]) / df * df;
        }
        if (tP > (s32)MEMFR - 2)
            tP = MEMFR - 2;
        tS = tP;
        if (ek[K_SPRD] >= 256) {
            s32 raw = K_SPRD_CURVE(ek[K_SPRD]), q = df >> 2, sd;
            if (q < 1)
                q = 1;
            sd = (raw + (q >> 1)) / q * q;
            tS = tP + (sd < q ? q : sd);
            if (tS > (s32)MEMFR - 2)
                tS = MEMFR - 2;
            gS += (32767 - gS) >> 4;
        } else {
            gS -= gS >> 4;
            if (gS < 64)
                gS = 0;
        }
        if (snap) {
            hp.d = tP << 6;
            hs.d = tS << 6;
            gS = tS != tP ? 32767 : 0;
            snap = 0;
        }
        head_move(&hp, tP << 6, tau);
        head_move(&hs, tS << 6, tau);
    }
    moving = hp.v || hp.xg || (gS && (hs.v || hs.xg));

    /* the gains: REPEATS, VOLUME, MIX, MEMORY DRY, the placement, COLLECT, TONE */
    fbg = kt(pd_fb, ek[K_RPTS]);
    fbm = (fbg * 4096) / (4096 + (gS >> 3));    /* FEED with SPREAD: two paths back, each at half */
    lvl = kt(pd_vol, sk[K_VOL]) >> 1;           /* Q11 */
    ka = sk[K_MIX];
    dryg = ka <= 16384 ? 4096 : 4096 - (((ka - 16384) * 4096) / 16128);
    if (dryg < 0)
        dryg = 0;
    wetg = ka >= 16384 ? 4096 : ka >> 2;
    keep = (ss[S_KEEP] >> 8) * 258;
    tna = ss[S_TONE] >= 31744 ? 0 : pd_op[100 + (((ss[S_TONE] >> 8) * 155) >> 7)];
    {
        int md = wipe ? MD_ON : ST.cfg[C_MDRY];
        s32 t = md == MD_ON || (md == MD_SCAN && moving) ? 32767 : 0;
        gmd += t > gmd ? (t - gmd) >> 2 : (t - gmd) >> 7;
    }
    {
        s32 t = ek[K_SPRD] >= 256 ? ss[S_WIDE] : 0, a;
        pan += (t - pan) >> 4;
        a = pan << 14;                          /* 0 .. 1/8 of a turn */
        gPL = (sinq(0x60000000u - (u32)a) * 23170) >> 15;          /* cos and sin of 45 degrees -+ a, Q14 x 1.41 */
        gPR = (sinq(0x20000000u - (u32)a) * 23170) >> 15;
        gSL = (sinq(0x60000000u + (u32)a) * 23170) >> 15;
        gSR = (sinq(0x20000000u + (u32)a) * 23170) >> 15;
    }

    for (i = 0; i < N; i++) {
        s32 P = 0, S = 0, plA, plB, mdA, mdB, a, b, eA, eB, wl, wr, gl;

        /* the memory: the input (and FEED's echoes), over what was there with COLLECT */
        if (rec) {
            s32 in = ((((x[2 * i] + x[2 * i + 1]) >> 1) * inpt) >> 12);
            if (run) {
                in += fbv;
                if (collect)
                    in += (M[mw] * keep) >> 15;
            }
            if (in > 16384 || in < -16384)
                in = fbclip(in);
            M[mw] = (s16)in;
            mnew = mw;
            if (++mw >= MEMFR)
                mw = 0;
            if (mfill < MEMFR)
                mfill++;
        }
        /* the heads */
        if (hin) {
            P = hread(&hp);
            if (gS)
                S = (hread(&hs) * gS) >> 15;
        }
        hstep(&hp);
        hstep(&hs);

        /* the echo, and the modifier on what it plays */
        eread(lw, 0, &plA, &plB);
        mdA = plA;
        mdB = plB;
        switch (mid) {
        case M_SSPD:
        case M_MSPD:                            /* two grains at the speed, crossfaded: a head never crosses the
                                                   line's writing, so each echo plays at the speed */
            mdA = mdB = 0;
            for (k = 0; k < 2; k++) {
                s32 d8, ra, rb, wg;
                if (gph[k] == 0) {
                    gsp[k] = spd;
                    gd0[k] = dcur + (spd > 4096 ? ((spd - 4096) * (s32)gln[k]) >> 4 : 0);
                }
                d8 = gd0[k] + (((4096 - gsp[k]) * (s32)gph[k]) >> 4);
                lrd(lw, d8, &ra, &rb);
                wg = tri(gph[k] * gin[k]);
                mdA += (ra * wg) >> 15;
                mdB += (rb * wg) >> 15;
                if (++gph[k] >= gln[k]) {
                    gph[k] = 0;
                    if (gln[k] != (u32)gnext) {
                        gln[k] = (u32)gnext;
                        gin[k] = 0xffffffffu / gln[k];
                    }
                }
            }
            break;
        case M_STAB: {                          /* tape: the read wanders, darker, hiss clockwise */
            s32 wob = ((sinq(wph) * wdep) >> 15) + ((sinq(fph) * fdep) >> 15);
            wph += winc;
            fph += finc;
            eread(lw, wdep + fdep + wob, &a, &b);
            mdA = LP(stl[0], lim(a, TOP), sla);
            mdB = LP(stl[1], lim(b, TOP), sla);
            if (nlev) {
                s32 n = (rnd_s() * nlev) >> 14;
                mdA += n;
                mdB += n;
            }
            break;
        }
        case M_TRIM:                            /* a gate in each period of SIZE */
            gl = topen < 0 || (tend ? (s32)tp < topen : (s32)tp >= topen) ? 4096 : 0;
            tg += (gl - tg) >> 6;
            mdA = (plA * tg) >> 12;
            mdB = (plB * tg) >> 12;
            break;
        case M_FILT: {                          /* a state-variable filter, twice a frame */
            s32 in2[2], o[2];
            if (!svf)
                break;
            in2[0] = plA;
            in2[1] = plB;
            for (k = 0; k < 2; k++) {
                s32 lp = svl[k], bp = svb[k], hp = 0;
                int r;
                for (r = 0; r < 2; r++) {
                    lp += (svf * bp) >> 12;
                    hp = in2[k] - lp - ((svq * bp) >> 12);
                    bp += (svf * lim(hp, 262143)) >> 12;
                    lp = lim(lp, 131071);
                    bp = lim(bp, 131071);
                }
                svl[k] = lp;
                svb[k] = bp;
                o[k] = hpf ? lim(hp, TOP) : lp;
            }
            mdA = o[0];
            mdB = o[1];
            break;
        }
        case M_DROP:                            /* brief silences: random, or the same pattern every echo */
            if (dcnt == 0) {
                u32 r = dpat ? (dsi * 2654435761u) ^ 0x5bd1e995u : rnd();
                r ^= r >> 13;
                dcnt = (u32)slen;
                dsi++;
                if ((s32)((r >> 8) & 0xffff) < dprob) {
                    dtg = r & 1 ? 0 : 1400;
                    drate = r & 2 ? 2 : 7;
                } else {
                    dtg = 4096;
                    drate = 5;
                }
            }
            dcnt--;
            dg += (dtg - dg) >> drate;
            mdA = (plA * dg) >> 12;
            mdB = (plB * dg) >> 12;
            break;
        default:
            break;
        }
        if (mid != mnext) {                     /* a new modifier: fade to the plain echo, then to it */
            mg -= 16;
            if (mg <= 0) {
                mg = 0;
                mid = mnext;
                mod_init(mid);
            }
        } else if (mg < 4096) {
            mg += 16;
        }
        if (mg < 4096) {
            mdA = plA + (((mdA - plA) * mg) >> 12);
            mdB = plB + (((mdB - plB) * mg) >> 12);
        }
        if (++tp >= (u32)df) {                  /* the echo's period */
            tp = 0;
            dsi = 0;
            dcnt = 0;
        }

        /* the lines' input: the heads and the feedback (IN: through the modifier; FEED: none, it goes round the
           memory); LOOP: the lines go round as they are */
        if (loop) {
            a = plA;
            b = plB;
        } else {
            s32 fa = 0, fb2 = 0;
            if (fmode == MODE_IN) {
                fa = mdA;
                fb2 = mdB;
            } else if (fmode == MODE_OUT) {
                fa = plA;
                fb2 = plB;
            }
            if (tna && fmode != MODE_FEED) {
                fa = LP(tnl[0], lim(fa, TOP), tna);
                fb2 = LP(tnl[1], lim(fb2, TOP), tna);
            }
            a = P + ((fa * fbg) >> 12);
            b = S + ((fb2 * fbg) >> 12);
        }
        if (a > 16384 || a < -16384)
            a = fbclip(a);
        if (b > 16384 || b < -16384)
            b = fbclip(b);
        L[2 * (lw & LMASK)] = (s16)(a >> 1);
        L[2 * (lw & LMASK) + 1] = (s16)(b >> 1);
        lw++;
        if (lfill < HB_LFR)
            lfill++;
        if (dxg) {
            dxg -= 4;
            if (dxg < 0)
                dxg = 0;
        }

        /* the output: the echoes placed, the memory dry, VOLUME; MIX against the dry */
        eA = wipe ? 0 : mdA;
        eB = wipe ? 0 : mdB;
        if (fmode == MODE_FEED && run)
            fbv = (lim(wipe ? P + S : eA + eB, TOP) * fbm) >> 12;
        else
            fbv = 0;
        wl = ((eA * gPL) >> 14) + ((eB * gSL) >> 14);
        wr = ((eA * gPR) >> 14) + ((eB * gSR) >> 14);
        if (gmd && run) {
            s32 pm = (P * gmd) >> 15, sm = (S * gmd) >> 15;
            wl += ((pm * gPL) >> 14) + ((sm * gSL) >> 14);
            wr += ((pm * gPR) >> 14) + ((sm * gSR) >> 14);
        }
        wl = (lim(wl, 131071) * lvl) >> 11;
        wr = (lim(wr, 131071) * lvl) >> 11;
        if (run) {
            w[2 * i] = lim(((x[2 * i] * dryg) >> 12) + ((lim(wl, 131071) * wetg) >> 12), TOP);
            w[2 * i + 1] = lim(((x[2 * i + 1] * dryg) >> 12) + ((lim(wr, 131071) * wetg) >> 12), TOP);
        } else {                                /* the trails: the echoes alone */
            w[2 * i] = lim(wl, TOP);
            w[2 * i + 1] = lim(wr, TOP);
        }
    }

    ST.flag[F_STATE] = (u8)((loop ? FS_LOOP : 0) | (moving ? FS_MOVE : 0) | (away ? FS_AWAY : 0)
                            | (wipe ? FS_WIPE : 0) | (clr_t < 1500 ? FS_CLEARED : 0));
    ST.flag[F_MOD] = (u8)mid;
    ST.val[V_FILL] = mfill;
    ST.val[V_DP] = (u32)(hp.d >> 6);
    ST.val[V_DS] = gS ? (u32)(hs.d >> 6) : 0;
    ST.val[V_SIZE] = (u32)df;
}
