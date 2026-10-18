/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiTriad's render: a three-voice rhythmic pitch shifter. core-dn1 calls it at interrupt level for
 * every block while the page is open (the pedal shell, pedal_dsp.inc, around fx_block). The inputs, summed to
 * mono, go onto a line, and three voices read it back:
 *   PITCH 1-3  each voice's interval, -2 .. +2 octaves (0 is OFF), as semitones with KEY on CHROMATIC, or as
 *              steps of the SCALE in the KEY from the note a pitch detector hears in the input (diatonic)
 *   the voices each sit behind their own time division of the main TIME (TDV1-3: off, a micro
 *              delay, 1/6 .. 1 of it), or behind a RHYTHM pattern's, and pan left, centre and right
 *   MODE       what the delay does with them: SERIES (one line repeating the voices every TIME), PITCH FEEDBACK
 *              (the repeats go back through the pitch shifters, so they climb or fall), DUAL (a line a side)
 *              and DUAL + CROSS (the two sides' repeats cross: ping-pong)
 *   MICROTUNE  detunes the voices against each other; GLIDE slides between intervals; CORRECTION pulls the
 *              voices onto the scale's exact pitches (LOOSE half way, STRICT all the way)
 * Each voice is a delay-line pitch shifter: two (CLASSIC, 25 ms grains) or three (SMOOTH, 50 ms) heads with Hann
 * windows, overlapping, each reading the line at the ratio (its delay moving by 1 - ratio a frame) and starting
 * a new grain while its window is closed: a voice up starts far enough back to end its grain at the division's
 * delay, a voice down starts there. When the detector hears a pitch, each grain starts a whole number of its
 * periods from the one before, so they cross in phase (no comb on a held note); at unison all read one place.
 * The detector is YIN on the voices' input (the inputs, and PITCH FEEDBACK's repeats) decimated to 12 kHz: a
 * 192-sample frame, its 192 lags spread over 32 blocks.
 * Fixed point as the shell's; delays on the lines in frames Q12. */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#include "dtriad.h"
#include "tables.h"

#define COLD    __attribute__((optimize("Os")))     /* once a block or less: small beats fast */

#define PD_MIX  K_MIX
#define PD_OUT  S_OUT
#include "pedal_dsp.inc"

#define HMASK   (DH_HFR - 1)
#define EMASK   (DH_EFR - 1)
#define NV      3                               /* voices */
#define NH      3                               /* heads a voice, at most */
#define BASE    16                              /* the shortest delay a head reads at, frames */
#define OFFV    (-128)                          /* an interval table's OFF */
#define OFF_SH  (-128 * 4096)                   /* a voice's shift when it is OFF */
#define DW      192                             /* the detector's frame and its longest lag, at 12 kHz */
#define DTMIN   6                               /* its shortest lag: 2 kHz */
#define DLAGS   6                               /* lags a block */
#define C0_Q12  71759                           /* log2(12000 / C0) + 8 (the lag is Q8), in octaves Q12 */
#define NOTE_HYST 2458                          /* a new note when the pitch is 0.6 semitones from the old */

static const u8 scl[7][7] = {                   /* the scales, semitones above the key */
    { 0, 2, 4, 5, 7, 9, 11 },                   /* major */
    { 0, 2, 3, 5, 7, 8, 10 },                   /* minor */
    { 0, 2, 3, 5, 7, 9, 11 },                   /* melodic minor */
    { 0, 2, 3, 5, 7, 8, 11 },                   /* harmonic minor */
    { 0, 1, 4, 5, 7, 8, 11 },                   /* double harmonic */
    { 0, 4, 6, 7, 11 },                         /* lydian pentatonic */
    { 0, 3, 5, 7, 10 },                         /* minor pentatonic */
};
static const u8 scn[7] = { 7, 7, 7, 7, 7, 5, 5 };
/* RHYTHM: the three voices' divisions of TIME in 24ths (0: none); pattern 0 is the TDIV knobs' */
static const u8 rhythm[8][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 6, 12, 18 }, { 8, 16, 24 }, { 9, 18, 24 },
                                 { 8, 12, 20 }, { 4, 8, 12 }, { 6, 15, 24 } };
static const u16 time_div[7] = { 6, 8, 12, 18, 24, 36, 48 };     /* TIME in time: 1/16 .. 1/2, quarters x 24 */
static const s16 pan_l[3][NV] = { { 4096, 2896, 0 }, { 3784, 2896, 1567 }, { 2896, 2896, 2896 } };
static const s16 pan_r[3][NV] = { { 0, 2896, 4096 }, { 1567, 2896, 3784 }, { 2896, 2896, 2896 } };

static u32 wpos;                                /* the lines' write head, frames */
static u32 vph[NV];                             /* each voice's heads' phase (head h a third or a half on) */
static s32 vhd[NV][NH];                         /* each head's delay, frames Q12 */
static s32 vsh[NV];                             /* each voice's shift now, semitones Q12 */
static s32 vbase[NV];                           /* its time division's delay now, frames Q12 */
static s32 vg[NV];                              /* its gain now, Q12 */
static s32 glen;                                /* the grain now, frames */
static int nheads;
static s32 et;                                  /* the echo's delay now, frames Q12 */
static s32 lp0, lp1;                            /* the repeats' one-poles */
static s32 dl0, dl1;                            /* the detector's decimation filters */
static u32 dpos;
static u32 dsum;
static int dtau;                                /* the next lag; 0 between frames */
static int have_note, cur_n, unvoiced;          /* the note held (semitones above C0), frames heard without one */
static s32 cur_err;                             /* the pitch against it, semitones Q12 */
static s32 period;                              /* the pitch's period at 48 kHz, frames Q8; 0 unknown */
static s32 sw_g, sw_slow;                       /* SWELL: its gain (Q15) and the input's slow envelope */
static u32 sw_last;
static int fresh;                               /* the first block after a reset: no slews */

static inline s32 rdm(const s16 *L, u32 mask, u32 w, u32 d12)      /* the mono line at d12 behind frame w */
{
    u32 p = (w << 12) - d12, a = (p >> 12) & mask, b = (a + 1) & mask;
    s32 f = (s32)(p & 0xfff), x0 = L[a];
    return (x0 + (((L[b] - x0) * f) >> 12)) << 1;
}

static inline s32 hann(u32 p)                   /* the window at phase p, Q15: 0 at 0, 1 half way */
{
    return (32767 - sinq(p + 0x40000000u)) >> 1;
}

/* One head over the block, its window ramped from frame to frame (a grain is 200 frames or more), into va[] (or
 * onto it, add): from phase p (its step inc) and delay hd (Q12; it moves by dd a frame). Where its phase wraps
 * (its window closed) it starts a new grain at the division's delay b (moving by db a frame) plus rp; with a
 * pitch heard (P12, its period, Q12) a whole number of periods from ref (the head that started last, moving as
 * this one does), so the two cross in phase and every grain lines up with the one before it. -> its delay. */
static s32 head(s32 *va, int add, u32 p, u32 inc, s32 hd, s32 ref, s32 b, s32 db, s32 rp, s32 dd, s32 P12)
{
    const s16 *H = DH_HLINE;
    u32 w = wpos, n = (0xffffffffu - p) / inc + 1;          /* frames to the wrap: it starts at frame n - 1 */
    s32 wa = hann(p + inc), wb = hann(p + N * inc), dw = (wb - wa) / (N - 1), wg = wa;
    int i, at = n <= N ? (int)n - 1 : -1;
    for (i = 0; i < N; i++) {
        s32 x;
        if (i == at) {
            s32 st = b + db * (i + 1) + rp;
            hd = st;
            if (P12) {
                s32 d = (ref + dd * i - st) % P12;
                hd = st + (d < 0 ? d + P12 : d);
            }
        }
        hd += dd;
        if (hd < BASE << 12)
            hd = BASE << 12;
        x = (rdm(H, HMASK, w + (u32)i, (u32)hd) * wg) >> 15;
        va[i] = add ? va[i] + x : x;
        wg += dw;
    }
    return hd;
}

static inline s32 soft(s32 v)                   /* the repeats' limit: straight to 0.75, then a quarter as steep */
{
    s32 a = absv(v);
    if (a <= 24576)
        return v;
    a = 24576 + ((a - 24576) >> 2);
    if (a > 40960)
        a = 40960;
    return v < 0 ? -a : a;
}

static u32 ratio_q16(s32 sh);

static COLD void heads_init(int v)                   /* each head where its grain has it now, at the voice's ratio */
{
    s32 rise = (((s32)ratio_q16(vsh[v]) - 65536) * glen) >> 4, at = vbase[v] + (rise > 0 ? rise : 0);
    int h;
    for (h = 0; h < NH; h++) {
        u32 p = vph[v] + (nheads == 3 ? 0x55555555u : 0x80000000u) * (u32)h;
        vhd[v][h] = at - (rise >> 8) * (s32)(p >> 24);
    }
}

static COLD void fx_reset(void)
{
    int v;
    wpos = 0;
    nheads = 0;
    glen = 2400;
    for (v = 0; v < NV; v++) {
        vph[v] = 0;
        vsh[v] = 0;
        vbase[v] = BASE << 12;
        vg[v] = 0;
    }
    et = 24000 << 12;
    lp0 = lp1 = dl0 = dl1 = 0;
    dpos = 0;
    dtau = 0;
    have_note = 0;
    cur_n = 0;
    cur_err = 0;
    unvoiced = 0;
    period = 0;
    sw_g = 32767;
    sw_slow = 0;
    sw_last = 0u - 1000u;
    fresh = 1;
    ST.flag[F_NOTE] = 0;
    ST.flag[F_VOICED] = 0;
}

static void fx_cmd(int c)
{
    (void)c;
}

/* ---- the pitch detector ---------------------------------------------------------------------------------- */
static COLD s32 log2q(u32 v)                         /* log2(v), Q12 */
{
    int b = 0;
    u32 m, fq;
    s32 i, f;
    while ((v >> b) > 1)
        b++;
    m = b >= 16 ? v >> (b - 16) : v << (16 - b);
    fq = m - 65536u;
    i = (s32)(fq >> 10);
    f = (s32)(fq & 1023);
    return b * 4096 + dh_log2[i] + (((dh_log2[i + 1] - dh_log2[i]) * f) >> 10);
}

static COLD void detect_pick(void)                   /* the frame's lags done: the period, the note */
{
    const u16 *dn = DH_DN;
    int t, best = -1;
    u32 mn = 0xffffffffu;
    for (t = DTMIN; t < DW; t++)                /* the first dip under 0.15, to its bottom */
        if (dn[t] < 154) {
            while (t + 1 < DW && dn[t + 1] < dn[t])
                t++;
            best = t;
            break;
        }
    if (best < 0) {                             /* else the deepest, if it is under 0.35 */
        for (t = DTMIN; t < DW; t++)
            if (dn[t] < mn) {
                mn = dn[t];
                best = t;
            }
        if (mn >= 358)
            best = -1;
    }
    if (best < 0) {
        if (unvoiced < 1000)
            unvoiced++;
        if (unvoiced > 2) {
            ST.flag[F_VOICED] = 0;
            period = 0;
        }
        return;
    }
    {
        s32 a = dn[best - 1], b = dn[best], c = dn[best + 1], den = a - 2 * b + c, off = 0, tq, nq;
        if (den > 0) {
            off = ((a - c) * 128) / den;
            off = lim(off, 128);
        }
        tq = best * 256 + off;                  /* the lag, Q8 */
        nq = (C0_Q12 - log2q((u32)tq)) * 12;    /* semitones above C0, Q12 */
        if (nq < 12 * 4096 || nq > 108 * 4096)  /* outside C1 .. C9: not a note */
            return;
        if (!have_note || nq > cur_n * 4096 + NOTE_HYST || nq < cur_n * 4096 - NOTE_HYST)
            cur_n = (nq + 2048) >> 12;
        have_note = 1;
        cur_err = nq - cur_n * 4096;
        unvoiced = 0;
        if (b < 154)
            period = tq * 4;
        ST.flag[F_VOICED] = 1;
        ST.flag[F_NOTE] = (u8)(cur_n % 12 + 1);
        ST.val[V_PITCH] = (u32)nq;
    }
}

/* m (a block, mono) at 12 kHz into the ring; then the frame's next lags (a new frame when the last is done). */
static void detect(const s32 *m)
{
    s16 *dbuf = DH_DBUF, *fr = DH_FRAME;
    u16 *dn = DH_DN;
    s32 a = pd_op[178], s0 = dl0, s1 = dl1;     /* two one-poles at 2.5 kHz before the decimation */
    u32 p = dpos;
    int i;
    for (i = 0; i < N; i++) {
        s32 y0, y;
        y0 = LP(s0, m[i], a);
        y = LP(s1, y0, a);
        if ((i & 3) == 3)
            dbuf[p++ & 511] = (s16)sat16(y);
    }
    dl0 = s0, dl1 = s1, dpos = p;
    if (!dtau) {                                /* a new frame: the latest 384 samples, scaled to +-2047 */
        s32 pk = 0, g;
        for (i = 0; i < 2 * DW; i++) {
            s32 v = dbuf[(p - 2 * DW + (u32)i) & 511];
            fr[i] = (s16)v;
            if (absv(v) > pk)
                pk = absv(v);
        }
        if (pk < 48) {                          /* -57 dBFS: silence */
            if (unvoiced < 1000)
                unvoiced++;
            if (unvoiced > 2) {
                ST.flag[F_VOICED] = 0;
                period = 0;
            }
            return;
        }
        g = (2047 << 12) / pk;
        for (i = 0; i < 2 * DW; i++)
            fr[i] = (s16)((fr[i] * g) >> 12);
        dsum = 0;
        dn[0] = 1024;
        dtau = 1;
        return;
    }
    {
        int t, end = dtau + DLAGS;
        if (end > DW + 1)
            end = DW + 1;
        for (t = dtau; t < end; t++) {
            const s16 *pa = fr, *pb = fr + t;
            u32 d = 0, m;
            int j;
            for (j = 0; j < DW; j++) {
                s32 e = pa[j] - pb[j];
                d += (u32)(e * e);
            }
            d >>= 8;                            /* the sum over 192 lags fits a word */
            dsum += d;
            m = dsum / (u32)t;
            while (d > 0x1fffffu) {
                d >>= 1;
                m >>= 1;
            }
            d = (d << 10) / (m ? m : 1);
            dn[t] = (u16)(d > 65535 ? 65535 : d);
        }
        dtau = end;
        if (dtau > DW) {
            dtau = 0;
            detect_pick();
        }
    }
}

/* ---- the voices' intervals ------------------------------------------------------------------------------ */
static COLD s32 target_shift(int v, int key, int harm, s32 mt)     /* semitones Q12; OFF_SH for off */
{
    int kv = ST.knob[K_P1 + v], s;
    if (key >= 12 || !harm) {
        s = dh_chrom[kv];
        if (s == OFFV)
            return OFF_SH;
        if (!harm)
            return 0;
        return (s << 12) + mt;
    } else {
        int sc = ST.opt[O_SCALE] % 7, ns = scn[sc], steps = ns == 7 ? dh_dia[kv] : dh_pent[kv];
        int n = have_note ? cur_n : key + 48, r, d = 0, D, o = 0, corr = ST.opt[O_CORR];
        s32 sh;
        if (steps == OFFV)
            return OFF_SH;
        r = ((n - key) % 12 + 12) % 12;
        while (d + 1 < ns && scl[sc][d + 1] <= r)
            d++;
        D = d + steps;
        while (D < 0) {
            D += ns;
            o--;
        }
        while (D >= ns) {
            D -= ns;
            o++;
        }
        sh = (s32)(scl[sc][D] + 12 * o - scl[sc][d]) << 12;
        if (have_note && corr)                  /* CORRECTION: onto the scale's pitch, half or all the way */
            sh -= corr == CORR_STRICT ? cur_err : cur_err >> 1;
        return sh + mt;
    }
}

static COLD u32 ratio_q16(s32 sh)                    /* 2^(sh / 12 semitones), Q16 */
{
    s32 o = (sh + 12 * 4096 * 4) / 12;          /* octaves Q12, kept positive (sh above -48 semitones) */
    s32 e = o & 4095, i = e >> 4, f = e & 15, sft = (o >> 12) - 4;
    u32 r = (u32)(pd_exp2[i] + (((i < 255 ? pd_exp2[i + 1] : 8192) - pd_exp2[i]) * f >> 4)) << 4;
    return sft >= 0 ? r << sft : r >> -sft;
}

/* ---- the time ------------------------------------------------------------------------------------------- */
static COLD u32 main_time(int dual, int half)        /* TIME: in time, 1/16 .. 1/2; free, 20 .. 520 ms; halved to fit */
{
    u32 q, mx = (dual ? 12480u : 24960u) << half;
    if (qfr) {
        int i = (sk[K_TIME] * 7) >> 15;
        q = qfr * time_div[i > 6 ? 6 : i] / 24u;
    } else {
        q = (u32)(exp2_q12((sk[K_TIME] * 1203) >> 15) * 960) >> 12;     /* 20 ms x 26^knob: to 520 ms */
    }
    q <<= half;
    while (q > mx)
        q >>= 1;
    return q < 240 ? 240 : q;
}

/* ---- the block ------------------------------------------------------------------------------------------ */
static void fx_block(s32 *w, const s32 *x, int mode)
{
    static s32 yl[N], yr[N], um[N], va[N];
    s32 vdb[NV];
    s16 *H = DH_HLINE, *E = DH_ELINE;
    int i, v, md = ST.opt[O_MODE], dual = md >= MODE_DUALX, half = ST.opt[O_HALF] & 1;
    int key = dh_key[ST.knob[K_KEY]], harm = !(ST.sw[SW_HARM] ^ ST.held[SW_HARM]);
    int frz = (ST.sw[SW_FREEZE] ^ ST.held[SW_FREEZE]) && mode != PD_IDLE;
    int swell = ST.sw[SW_SWELL] ^ ST.held[SW_SWELL], spread = ST.opt[O_SPREAD] % 3, nact = 0;
    u32 q = main_time(dual, half);
    s32 fb, fbn, a_lp, et0, det, mt, ga;

    ST.val[V_TIME] = q;
    ST.flag[F_FREEZE] = (u8)frz;
    ST.flag[F_HARM] = (u8)harm;

    if (mode == PD_IDLE) {                      /* bypassed without trails: the line listens, nothing plays */
        for (i = 0; i < N; i++) {
            um[i] = (x[2 * i] + x[2 * i + 1]) >> 1;
            H[(wpos + i) & HMASK] = (s16)sat16(um[i] >> 1);
        }
        detect(um);
        for (i = 0; i < N; i++) {
            u32 a = (wpos + i) & EMASK;
            E[2 * a] = E[2 * a + 1] = 0;
        }
        wpos += N;
        return;
    }

    /* the grain: CLASSIC two heads over 25 ms, SMOOTH three over 50 */
    if (nheads != (ST.opt[O_ALGO] == ALGO_SMOOTH ? 3 : 2)) {
        nheads = ST.opt[O_ALGO] == ALGO_SMOOTH ? 3 : 2;
        glen = nheads == 3 ? 2400 : 1200;
        for (v = 0; v < NV; v++)
            heads_init(v);
    }

    /* the voices' targets: interval, delay, gain */
    mt = (sk[K_MTUN] * 1229) >> 15;             /* MICROTUNE: up to 30 cents */
    ga = ST.sec[S_GLIDE] ? (21845 << 12) / (10 * exp2_q12((ST.sec[S_GLIDE] * 3945) >> 8)) : 32767;
                                                /* GLIDE: a block's step, Q15: a time constant of 10 ms x 200^knob */
    if (ga > 32767)
        ga = 32767;
    if (ST.opt[O_SMOOTH] && ga > 862)           /* the PITCH knobs smoothed: 25 ms at least */
        ga = 862;
    for (v = 0; v < NV; v++) {
        s32 tsh = target_shift(v, key, harm, v == 0 ? mt : v == 1 ? -mt : mt >> 1), gt, d;
        int dv = ST.opt[O_RHYTHM] ? rhythm[ST.opt[O_RHYTHM] & 7][v] : dh_tdiv[ST.sec[S_TD1 + v]];
        s32 bt = (s32)(dv > 0 ? q * (u32)dv / 24u : dv < 0 ? (u32)(-dv * 144) << half : 0) + BASE;
        if (tsh == OFF_SH) {
            gt = 0;
        } else {
            gt = (kt(pd_vol, ss[S_LEV1 + v]) * 3) >> 2;
            if (ga >= 32767 || fresh)
                vsh[v] = tsh;
            else {
                d = tsh - vsh[v];
                if (d > -8 && d < 8)
                    vsh[v] = tsh;
                else
                    vsh[v] += ((d >> 3) * ga) >> 12;
            }
        }
        vg[v] += lim(gt - vg[v], 256);          /* on and off over 10 ms */
        bt <<= 12;                              /* the division's delay: moved at half a frame a frame at most */
        if (fresh || (!vg[v] && !gt)) {         /* (at once, while nobody hears it) */
            vbase[v] = bt;
            heads_init(v);
        }
        vdb[v] = lim(bt - vbase[v], 2048 * N) / N;
        if (gt)
            nact++;
    }
    ST.val[V_SHIFT] = (u32)vsh[0];

    fb = kt(pd_fb, sk[K_FDBK]);
    if (fb > 4014)
        fb = 4014;                              /* 0.98 at the top */
    fbn = nact ? fb * 2 / nact : 0;             /* PITCH FEEDBACK: through every voice, so shared */
    if (fbn > 4014)
        fbn = 4014;
    a_lp = pd_op[half ? 190 : 216];             /* the repeats darken: 7 kHz, 3.5 kHz at half speed */
    if (fresh)
        et = (s32)(q << 12);
    fresh = 0;
    et0 = et;
    if (!frz)
        et += lim((s32)(q << 12) - et, 2048 * N);
    det = (et - et0) / N;

    /* SWELL: each attack starts the input from silence, back over 400 ms */
    {
        s32 pk = 0, sg0 = sw_g, sg1, dg;
        for (i = 0; i < 2 * N; i += 4)
            if (absv(x[i]) > pk)
                pk = absv(x[i]);
        if (swell && mode == PD_RUN && pk > 2 * sw_slow + 300 && pk > 600 && ST.blocks - sw_last > 90) {
            sw_g = 0;
            sw_last = ST.blocks;
        }
        sw_slow += (pk - sw_slow) >> 4;
        if (!swell)
            sw_g = 32767;
        else if (sw_g < 32767)
            sw_g += 55;
        if (sw_g > 32767)
            sw_g = 32767;
        ST.flag[F_SWELL] = (u8)(swell && sw_g < 32767);
        sg1 = (s32)(((u32)sw_g * (u32)sw_g) >> 15);
        sg0 = (s32)(((u32)sg0 * (u32)sg0) >> 15);
        dg = (sg1 - sg0) / N;

        /* the voices' input: the inputs (silent for the trails), PITCH FEEDBACK's repeats, swelled */
        {
            s32 l0 = lp0;
            for (i = 0; i < N; i++) {
                s32 in = mode == PD_RUN ? (x[2 * i] + x[2 * i + 1]) >> 1 : 0, g = sg0 + dg * (i + 1);
                if (g < 32767)
                    in = (in * g) >> 15;
                if (md == MODE_PFB && !frz) {
                    s32 e = rdl(E, EMASK, wpos + i, (u32)(et0 + det * (i + 1)), 0);
                    e = LP(l0, e, a_lp);
                    in += (soft(e) * fbn) >> 12;
                }
                H[(wpos + i) & HMASK] = (s16)sat16(in >> 1);
                um[i] = in;
            }
            if (md == MODE_PFB)
                lp0 = l0;
        }
    }

    detect(um);                                 /* the detector hears what the voices shift */

    /* the voices: their heads on the line, panned */
    for (i = 0; i < N; i++)
        yl[i] = yr[i] = 0;
    for (v = 0; v < NV; v++) {
        s32 g0 = vg[v], db = vdb[v], b = vbase[v], P12 = period << 4, rise, rp, dd;
        s32 pl = pan_l[spread][v], pr = pan_r[spread][v], h0, h1, h2;
        u32 ph = vph[v], off = nheads == 3 ? 0x55555555u : 0x80000000u, inc, r;
        s32 lv;
        if (!g0) {                              /* off: silent */
            vbase[v] += db * N;
            continue;
        }
        r = ratio_q16(vsh[v]);
        lv = r > 131072 ? (s32)(((u32)glen << 17) / r) : glen;     /* over an octave up, shorter grains */
        inc = 0xffffffffu / (u32)lv;
        rise = (((s32)r - 65536) * lv) >> 4;    /* a grain's fall in delay (a rise, down), Q12 */
        rp = rise > 0 ? rise : 0;               /* up: a grain starts that far back, so it ends at the division */
        dd = ((65536 - (s32)r) >> 4) + db;      /* a head's delay's change a frame, Q12 */
        h0 = vhd[v][0], h1 = vhd[v][1], h2 = vhd[v][2];
        vhd[v][0] = head(va, 0, ph, inc, h0, h1, b, db, rp, dd, P12);
        vhd[v][1] = head(va, 1, ph + off, inc, h1, nheads == 3 ? h2 : h0, b, db, rp, dd, P12);
        if (nheads == 3)
            vhd[v][2] = head(va, 1, ph + 2 * off, inc, h2, h0, b, db, rp, dd, P12);
        if (nheads == 3)
            g0 = (g0 * 21845) >> 15;            /* three Hann windows sum to 1.5 */
        for (i = 0; i < N; i++) {
            s32 s = (va[i] * g0) >> 12;
            yl[i] += (s * pl) >> 12;
            yr[i] += (s * pr) >> 12;
        }
        ph += inc * N;
        b += db * N;
        vph[v] = ph;
        vbase[v] = b;
    }

    /* the delay: the repeats every TIME, as MODE has them */
    {
        s32 l0 = lp0, l1 = lp1;
        for (i = 0; i < N; i++) {
            u32 a = (wpos + i) & EMASK, d12 = (u32)(et0 + det * (i + 1));
            s32 ol, orr;
            if (md == MODE_PFB || md == MODE_SERIES) {      /* one line, the sides' sum */
                s32 e = rdl(E, EMASK, wpos + i, d12, 0), z;
                if (frz) {                      /* FREEZE: the line goes round as it is */
                    z = e;
                    ol = orr = e;
                } else if (md == MODE_PFB) {
                    z = (yl[i] + yr[i]) >> 1;   /* the repeats went in at the voices' input */
                    ol = yl[i];
                    orr = yr[i];
                } else {
                    s32 f = (soft(LP(l0, e, a_lp)) * fb) >> 12;
                    z = ((yl[i] + yr[i]) >> 1) + f;
                    ol = yl[i] + f;
                    orr = yr[i] + f;
                }
                E[2 * a] = (s16)sat16(z >> 1);
            } else {                            /* DUAL: a line a side; CROSS: each side's repeats go to the other */
                s32 el = rdl(E, EMASK, wpos + i, d12, 0), er = rdl(E, EMASK, wpos + i, d12, 1), fl, fr2;
                if (frz) {
                    ol = md == MODE_DUALX ? er : el;
                    orr = md == MODE_DUALX ? el : er;
                } else {
                    fl = (soft(LP(l0, el, a_lp)) * fb) >> 12;
                    fr2 = (soft(LP(l1, er, a_lp)) * fb) >> 12;
                    ol = yl[i] + (md == MODE_DUALX ? fr2 : fl);
                    orr = yr[i] + (md == MODE_DUALX ? fl : fr2);
                }
                E[2 * a] = (s16)sat16(ol >> 1);
                E[2 * a + 1] = (s16)sat16(orr >> 1);
            }
            w[2 * i] = lim(ol, TOP);
            w[2 * i + 1] = lim(orr, TOP);
        }
        if (md != MODE_PFB)
            lp0 = l0;
        lp1 = l1;
    }
    wpos += N;
}
