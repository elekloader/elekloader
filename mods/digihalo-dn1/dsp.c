/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiHalo's render: a reverb pedal, twelve machines. core-dn1 calls it at interrupt level for every
 * block while the page is open (the pedal shell, pedal_dsp.inc, around fx_block). The machines share the parts:
 *   the pre-delay line   2.7 s stereo; PRE-DELAY 0-1.5 s (or the tempo's divisions), read between frames so a
 *                        turn glides; TAPE's tape and NONLINEAR's taps are this line too
 *   the diffusers        up to six allpasses a side, their sizes and strength the machine's (and DIFFUSION's)
 *   SWIRL's stages       three long allpasses a side with feedback around them (SWIRL)
 *   the tank             a feedback delay network: eight lines, a Householder matrix, each line's gain set from
 *                        DECAY's RT60 and its length, a one-pole damping from TONE in each, MOD moving four of
 *                        the lines a quarter cycle apart; L into the even lines, R into the odd
 *   the shimmer          two pitch voices on the tank's input, its output or both (SHIMMER)
 *   the springs          one to three lines, each through 14 stretched allpasses (the chirp), DWELL before them
 *   the after-stage      TONE's low-pass (or SWIRL's resonant one, or VOWEL's formants), HALL's MID,
 *                        LOW END's shelf, WIDTH, MIX against the dry (50/50 at three quarters), DRY, CAB
 * HOLD (T1, CC 97) sets every feedback to one and the damping off: FREEZE also stops the input reaching it,
 * INFINITE lets it add; a guard on the tank's peak keeps INFINITE from running away. A machine change mutes the
 * wet for a moment; without SPILLOVER the buffers are cleared meanwhile, 16 KB a block.
 * Fixed point as the shell's; the lines hold their samples at half scale. */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")   /* RAM is short: small code, but the tank */
#include "dhalo.h"
#include "tables.h"

#define PD_MIX  (-1)                            /* DigiHalo mixes the dry itself (MIX's law, DRY, SWELL, panning) */
#define PD_OUT  S_BOOST
#include "pedal_dsp.inc"

#define HOT __attribute__((optimize("O2")))     /* the per-frame loops: fast, the rest small */

#define PDM     (SK_PD_FR - 1)
#define TLM     (SK_TL - 1)
#define SPM     (SK_SL - 1)
#define PLM     (SK_PL - 1)

/* ---- the machines' voicing ----------------------------------------------------------------------------- */
static const u16 rt_min[MC_N] = { 200, 500, 500, 800, 500, 500, 1000, 500, 500, 200, 50, 133 };   /* DECAY, ms */
static const u16 rt_lr[MC_N] = { 1701, 1362, 1362, 933, 1362, 1362, 1445, 1362, 1362, 744, 1362, 407 };
                                                /* log2(max / min) x 256 */
static const u16 tk_size[MC_N][2] = {           /* the tank's size, Q8, by SIZE */
    { 115, 192 }, { 320, 512 }, { 141, 230 }, { 0, 0 }, { 307, 307 }, { 358, 358 }, { 666, 666 }, { 307, 307 },
    { 358, 358 }, { 154, 154 }, { 256, 256 }, { 128, 128 } };
static const u8 n_ap[MC_N] = { 3, 3, 3, 0, 3, 3, 5, 3, 3, 2, 2, 2 };   /* diffusers a side */
static const u16 ap_size[MC_N] = { 154, 256, 128, 0, 256, 256, 512, 256, 256, 205, 128, 128 };  /* Q8 */
static const u16 ap_coef[MC_N] = { 0, 20000, 23593, 0, 19661, 19661, 0, 19661, 19661, 0, 0, 16384 };  /* Q15; 0:
                                                   the machine's DIFFUSION */
static const u8 mod_max[MC_N] = { 3, 6, 5, 2, 8, 10, 14, 24, 8, 0, 4, 0 };     /* MOD's depth at the top, frames */
static const u16 mod_mhz[MC_N] = { 300, 400, 900, 500, 500, 300, 700, 250, 450, 600, 0, 700 };  /* its rate */
static const u16 out_g[MC_N] = { 1300, 1840, 1380, 1300, 2300, 1400, 1800, 700, 1700, 2900, 5200, 3600 };
                                                /* the wet's gain, Q12 */
static const u16 tk_base[8] = { 1571, 1861, 2039, 2237, 2473, 2671, 2927, 3163 };   /* the tank's lines, frames */
static const u16 ap_base[12] = { 142, 107, 379, 277, 613, 887, 149, 101, 367, 283, 647, 911 };  /* L's six, R's */
static const u16 bl_base[6] = { 1201, 1559, 1907, 1283, 1601, 1979 };                          /* SWIRL's */
static const u16 er_t[2][4] = { { 149, 307, 487, 691 }, { 173, 353, 521, 739 } };
static const s16 er_g[4] = { 29491, 24576, 20972, 17039 };
static const u16 er_mul[MC_N][2] = { { 256, 435 }, { 614, 922 } };   /* ROOM's and HALL's reflections: Q8 */
static const u16 pre_div[9] = { 3, 4, 6, 8, 12, 18, 24, 36, 48 };   /* 1/32 .. 1/2, quarter notes x 24 */

/* ---- helpers ------------------------------------------------------------------------------------------ */
static s32 exp2neg(u32 e)                       /* 16384 x 2^(-e / 65536) */
{
    u32 n = e >> 16, p = 65536 - (e & 0xffff), i = p >> 8;
    s32 fr = (s32)(p & 255), a, b;
    if (n > 14)
        return 0;
    a = i >= 256 ? 8192 : pd_exp2[i];
    b = i >= 255 ? 8192 : pd_exp2[i + 1];
    return ((a + (((b - a) * fr) >> 8)) << 1) >> n;
}

static inline s32 rdm(const s16 *L, u32 mask, u32 w, u32 d12)      /* a mono line, d12 frames (Q12) behind w */
{
    u32 p = (w << 12) - d12, a = (p >> 12) & mask, b = (a + 1) & mask;
    s32 f = (s32)(p & 0xfff), x0 = L[a];
    return x0 + (((L[b] - x0) * f) >> 12);
}

static inline s32 rds(const s16 *L, u32 w, u32 d12, int c)          /* the pre-delay line, one side, half scale */
{
    u32 p = (w << 12) - d12, a = (p >> 12) & PDM, b = (a + 1) & PDM;
    s32 f = (s32)(p & 0xfff), x0 = L[2 * a + c];
    return x0 + (((L[2 * b + c] - x0) * f) >> 12);
}

static u32 isqrt(u32 v)
{
    u32 r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* ---- the state ---------------------------------------------------------------------------------------- */
static s32 bf[2 * N], wet[2 * N], dryg[2];       /* the effect's input (half scale), its wet; the dry's gains */
#define HN      (N / 2)                         /* the diffusers and the tank run at 24 kHz: frames a block */
static s32 hb[2 * HN], hw[2 * HN], hprev[2];    /* a block at 24 kHz; a copy; the last frame out */
static u32 dw;                                  /* the pre-delay line: the next frame written */
static s32 pre12;                               /* the pre-delay now, Q12 frames */
static u32 apw[SK_NAP];                         /* the allpasses' positions */
static s32 bl_fb[2];                            /* SWIRL: its stages' last output, a side */
static u32 tw;                                  /* the tank: the next frame written */
static u32 tk_len[8];
static s32 tk_g[8], tk_lpc, tk_lp[8], tk_m[4], tk_ms[4], tk_peak, tk_guard = 16384;
static u32 tk_ph;
static u32 pw, sh_ph[2];                        /* SHIMMER: its line, its voices' phases (Q24) */
static s32 sh_prev[HN];                          /* the tank's output, the last block (REGEN) */
static s16 spr_cb[3][14][4];                    /* SPRING: the chirps' stages */
static u32 spr_w, spr_j;
static s32 spr_lp[3], spr_hp[3];
static s32 fx_peak, fx_guard = 16384;           /* the springs', the tape's and the taps' feedback guard */
static s32 mg_fb, mg_lp, mg_hp, mg_w[2];      /* TAPE */
static u32 mg_ph, mg_ph2;
static s32 nl_fb;                               /* NONLINEAR: its feedback */
static u32 rf_d[20];                            /* REFLECTIONS: the taps */
static s32 rf_gl[20], rf_gr[20];
static u32 rf_key = 0xffffffffu;
static s32 po_lp[2], po_lo[2], po_ma[2], po_mb[2], po_cab[4], po_dc[2];  /* the after-stage's filters */
static s32 sv_lo[6], sv_bd[6];                  /* VOWEL's formants (four), SWIRL's tone (two) */
static u32 ch_ph, ch_rw;
static s32 ch_m, ch_tgt;
static s32 sw_env, sw_fast, sw_slow, sw_inc;   /* SWELL's envelope (Q15), its follower */
static int sw_armed;
static s32 t2_env = 32767;                      /* T2's momentary swell */
static int mc_run = -1, run_size, first = 1;               /* the machine the render runs, and its size */
static int chg;                                 /* a change: 0 none, 1 muting, 2 clearing, 3 rising */
static s32 chg_g = 32767;                       /* the wet's gain through a change, Q15 */
static u32 clr_at, clr_end;                     /* the clearing: offsets into the region */

static void fx_reset(void)
{
    mc_run = -1;
    first = 1;
    chg = 0;
    chg_g = 32767;
    pre12 = 0;
    tk_guard = fx_guard = 16384;
    t2_env = 32767;
    sw_env = 32767;
    sw_armed = 1;
    rf_key = 0xffffffffu;
}

static void state_clear(void)                   /* the filters' and the stages' states */
{
    int k;
    s16 *c = &spr_cb[0][0][0];
    for (k = 0; k < 8; k++)
        tk_lp[k] = 0;
    for (k = 0; k < 6; k++)
        sv_lo[k] = sv_bd[k] = 0;
    for (k = 0; k < 3 * 14 * 4; k++)
        c[k] = 0;
    for (k = 0; k < 3; k++)
        spr_lp[k] = spr_hp[k] = 0;
    bl_fb[0] = bl_fb[1] = 0;
    mg_fb = nl_fb = mg_lp = mg_hp = 0;
    for (k = 0; k < HN; k++)
        sh_prev[k] = 0;
    hprev[0] = hprev[1] = 0;
}

static void fx_cmd(int c)
{
    (void)c;
}

static int pstep(int slot)                      /* the running machine's parameter, a step */
{
    int k = slot == P_P1 ? ST.knob[K_P1] : slot == P_P2 ? ST.knob[K_P2] : ST.sec[S_X1 + slot - P_X1];
    return sky_step(k, sky_pn[mc_run][slot]);
}

/* DECAY: the machine's RT60 in ms (TAPE's last head, NONLINEAR's time; with SYNC and a tempo, a division) */
static u32 rt_ms(int mc)
{
    if ((mc == MC_TAPE || mc == MC_NONLIN) && ST.opt[O_SYNC] && qfr) {
        u32 f = sync_frames(sk[K_DECAY], pre_div, 9) / 48;
        u32 hi = mc == MC_TAPE ? 1500 : 2000;
        return f < 50 ? 50 : f > hi ? hi : f;
    }
    return ((u32)rt_min[mc] * (u32)exp2_q12((sk[K_DECAY] * rt_lr[mc]) >> 15)) >> 12;
}

static u32 lfo_mhz(u32 base)                    /* MOD RATE: two octaves either side of the machine's */
{
    return (base * (u32)exp2_q12((ss[S_MRATE] - 16384) >> 5)) >> 12;
}

/* ---- the pre-delay ------------------------------------------------------------------------------------ */
HOT static void pdelay(s32 *b, s32 t12)             /* b in, delayed out; the delay glides to t12 */
{
    s16 *D = SK_PDL;
    s32 st, d;
    u32 p = dw;
    int i;
    if (absv(t12 - pre12) < N)                  /* a fraction of a frame from the target: there */
        pre12 = t12;
    st = (t12 - pre12) / N;
    if (st > 2048) st = 2048;
    if (st < -2048) st = -2048;
    d = pre12;
    if (!st && !(d & 0xfff)) {                  /* still, on a frame: plain reads */
        u32 q = (u32)d >> 12;
        for (i = 0; i < N; i++) {
            D[2 * p] = (s16)sat16(b[2 * i]);
            D[2 * p + 1] = (s16)sat16(b[2 * i + 1]);
            b[2 * i] = D[2 * ((p - q) & PDM)];
            b[2 * i + 1] = D[2 * ((p - q) & PDM) + 1];
            p = (p + 1) & PDM;
        }
        dw = p;
        return;
    }
    for (i = 0; i < N; i++) {
        D[2 * p] = (s16)sat16(b[2 * i]);
        D[2 * p + 1] = (s16)sat16(b[2 * i + 1]);
        d += st;
        b[2 * i] = rds(D, p, (u32)d, 0);
        b[2 * i + 1] = rds(D, p, (u32)d, 1);
        p = (p + 1) & PDM;
    }
    if (st && absv(t12 - d) < absv(st))       /* arrived: on the target exactly */
        d = t12;
    pre12 = d;
    dw = p;
}

/* 48 kHz to 24 kHz (each two frames' mean) and back (between each frame out and the last) */
HOT static void half_in(const s32 *b, s32 *h)
{
    int i;
    for (i = 0; i < HN; i++) {
        h[2 * i] = (b[4 * i] + b[4 * i + 2]) >> 1;
        h[2 * i + 1] = (b[4 * i + 1] + b[4 * i + 3]) >> 1;
    }
}

HOT static void half_out(const s32 *h, s32 *b)
{
    s32 pl = hprev[0], pr = hprev[1];
    int i;
    for (i = 0; i < HN; i++) {
        s32 l = h[2 * i], r = h[2 * i + 1];
        b[4 * i] = (pl + l) >> 1;
        b[4 * i + 1] = (pr + r) >> 1;
        b[4 * i + 2] = l;
        b[4 * i + 3] = r;
        pl = l, pr = r;
    }
    hprev[0] = pl, hprev[1] = pr;
}

/* ROOM's and HALL's early reflections: four taps a side after the pre-delay, into the diffusers */
HOT static void early(s32 *b, int mc, int size)
{
    const s16 *D = SK_PDL;
    u32 base = (dw - N - (u32)(pre12 >> 12)) & PDM, t[8];
    s32 mul = er_mul[mc][size], eg = mc == MC_ROOM ? 6554 : 4096;
    int i, k;
    for (k = 0; k < 4; k++) {
        t[k] = ((u32)er_t[0][k] * (u32)mul) >> 8;
        t[k + 4] = ((u32)er_t[1][k] * (u32)mul) >> 8;
    }
    for (i = 0; i < N; i++) {
        s32 l = 0, r = 0;
        u32 p = base + (u32)i;
        for (k = 0; k < 4; k++) {
            l += (D[2 * ((p - t[k]) & PDM)] * er_g[k]) >> 15;
            r += (D[2 * ((p - t[k + 4]) & PDM) + 1] * er_g[k]) >> 15;
        }
        b[2 * i] += (l * eg) >> 15;
        b[2 * i + 1] += (r * eg) >> 15;
    }
}

/* ---- the diffusers ------------------------------------------------------------------------------------ */
HOT static void ap_run(s32 *b, int side, int j, u32 len, s32 c)
{
    s16 *B = SK_AP + SK_AL * j;
    u32 p = apw[j];
    int i;
    if (len >= SK_AL) len = SK_AL - 1;
    if (len < 2) len = 2;
    if (p >= len) p = 0;
    for (i = side; i < 2 * HN; i += 2) {
        s32 v = B[p], u = sat16(b[i] - ((v * c + 16384) >> 15));
        B[p] = (s16)u;
        b[i] = v + ((u * c + 16384) >> 15);
        if (++p >= len) p = 0;
    }
    apw[j] = p;
}

static void diffuse(s32 *b, int n, u32 size, s32 c)
{
    int k;
    if (c > 26214) c = 26214;
    if (c <= 0 || n <= 0)
        return;
    for (k = 0; k < n; k++) {
        ap_run(b, 0, k, (ap_base[k] * size) >> 9, c);           /* frames at 24 kHz */
        ap_run(b, 1, k + 6, (ap_base[k + 6] * size) >> 9, c);
    }
}

/* SWIRL: three long allpasses a side, LENGTH their sizes, FEEDBACK around them */
HOT static void swirl(s32 *b, int len, int fb)
{
    s32 c = 22938, fg = fb * 1700;              /* FEEDBACK 0-17: 0 .. 0.88 */
    u32 sz = 64 + len * 34, L[6];              /* LENGTH 0-17: 0.25x .. 2.5x */
    int s, i, k;
    for (k = 0; k < 6; k++)
        L[k] = (bl_base[k] * sz) >> 9;
    for (s = 0; s < 2; s++) {
        s16 *B0 = SK_AP + SK_AL * (12 + 3 * s), *B1 = B0 + SK_AL, *B2 = B1 + SK_AL;
        u32 p0 = apw[12 + 3 * s], p1 = apw[13 + 3 * s], p2 = apw[14 + 3 * s];
        u32 l0 = L[3 * s], l1 = L[3 * s + 1], l2 = L[3 * s + 2];
        s32 f = bl_fb[s];
        if (p0 >= l0) p0 = 0;
        if (p1 >= l1) p1 = 0;
        if (p2 >= l2) p2 = 0;
        for (i = s; i < 2 * HN; i += 2) {
            s32 x = b[i] + ((f * fg) >> 15), v, u;
            v = B0[p0]; u = sat16(x - ((v * c + 16384) >> 15)); B0[p0] = (s16)u; x = v + ((u * c + 16384) >> 15);
            v = B1[p1]; u = sat16(x - ((v * c + 16384) >> 15)); B1[p1] = (s16)u; x = v + ((u * c + 16384) >> 15);
            v = B2[p2]; u = sat16(x - ((v * c + 16384) >> 15)); B2[p2] = (s16)u; x = v + ((u * c + 16384) >> 15);
            f = lim(x, 32767);
            b[i] = f;
            if (++p0 >= l0) p0 = 0;
            if (++p1 >= l1) p1 = 0;
            if (++p2 >= l2) p2 = 0;
        }
        bl_fb[s] = f;
        apw[12 + 3 * s] = p0, apw[13 + 3 * s] = p1, apw[14 + 3 * s] = p2;
    }
}

/* ---- the tank ----------------------------------------------------------------------------------------- */
/* Its lines' lengths, gains, damping and modulation for a block: size Q8, rt ms, depth Q12 frames, rate mHz */
static void tank_set(u32 size, u32 rt, s32 depth, u32 mhz, int hold)    /* hold: 0, 1 FREEZE, 2 INFINITE */
{
    int k, ti;
    if (rt < 20)
        rt = 20;
    for (k = 0; k < 8; k++) {
        u32 l = (tk_base[k] * size) >> 8;
        if (l < 64) l = 64;
        if (l > SK_TL - 64) l = SK_TL - 64;
        tk_len[k] = l >> 1;                     /* at 24 kHz */
        tk_g[k] = hold ? 16384 : exp2neg(11300u * l / rt);     /* 13607: -60 dB in rt; less, for the damping */
        tk_g[k] = (tk_g[k] * tk_guard) >> 14;
    }
    ti = 160 + ((sk[K_TONE] * 96) >> 15);       /* damping: 2.3 kHz .. open */
    ti += 26;                                   /* at 24 kHz a cutoff's coefficient is twice the cutoff's at 48 */
    tk_lpc = hold || ti >= 255 ? 16384 : pd_op[ti];
    depth >>= 1;
    tk_ph += lfo_inc((s32)mhz) * N;
    if (hold)                                   /* HOLD: the lines still (read between frames, they lose highs) */
        depth = 0;
    for (k = 0; k < 4; k += 3) {                /* lines 0 and 5 move (one a side), a quarter cycle apart */
        s32 t = (sinq(tk_ph + (u32)k * 0x40000000u) * depth) >> 15;
        tk_ms[k] = (t + depth + 4096 - tk_m[k]) / HN;
    }
    if (tk_peak > 26000 && hold != 1)           /* the guard: the lines near full scale (INFINITE; FREEZE adds nothing) */
        tk_guard -= tk_guard >> 6;
    else if (tk_guard < 16384)
        tk_guard += 16;
    tk_peak = 0;
}

__attribute__((optimize("O2")))
static void tank(s32 *b)                        /* b: half scale in, the wet out (four lines a side) */
{
    s16 *T = SK_TANK;
    u32 w = tw, l0 = tk_len[0], l1 = tk_len[1], l2 = tk_len[2], l3 = tk_len[3], l4 = tk_len[4], l5 = tk_len[5],
        l6 = tk_len[6], l7 = tk_len[7];
    s32 g0 = tk_g[0], g1 = tk_g[1], g2 = tk_g[2], g3 = tk_g[3], g4 = tk_g[4], g5 = tk_g[5], g6 = tk_g[6],
        g7 = tk_g[7], lpc = tk_lpc, m0 = tk_m[0], m3 = tk_m[3], pk = tk_peak;
    s32 z0 = tk_lp[0], z1 = tk_lp[1], z2 = tk_lp[2], z3 = tk_lp[3], z4 = tk_lp[4], z5 = tk_lp[5], z6 = tk_lp[6],
        z7 = tk_lp[7];
    int i;
    for (i = 0; i < HN; i++) {
        s32 y0, y1, y2, y3, y4, y5, y6, y7, s, xl = b[2 * i], xr = b[2 * i + 1], v;
        m0 += tk_ms[0], m3 += tk_ms[3];
        v = rdm(T, TLM, w, (l0 << 12) + (u32)m0);                 z0 += ((v - z0) * lpc) >> 14; y0 = (z0 * g0) >> 14;
        v = T[SK_TL + ((w - l1) & TLM)];                           z1 += ((v - z1) * lpc) >> 14; y1 = (z1 * g1) >> 14;
        v = T[2 * SK_TL + ((w - l2) & TLM)];                       z2 += ((v - z2) * lpc) >> 14; y2 = (z2 * g2) >> 14;
        v = T[3 * SK_TL + ((w - l3) & TLM)];                       z3 += ((v - z3) * lpc) >> 14; y3 = (z3 * g3) >> 14;
        v = T[4 * SK_TL + ((w - l4) & TLM)];                       z4 += ((v - z4) * lpc) >> 14; y4 = (z4 * g4) >> 14;
        v = rdm(T + 5 * SK_TL, TLM, w, (l5 << 12) + (u32)m3);     z5 += ((v - z5) * lpc) >> 14; y5 = (z5 * g5) >> 14;
        v = T[6 * SK_TL + ((w - l6) & TLM)];                       z6 += ((v - z6) * lpc) >> 14; y6 = (z6 * g6) >> 14;
        v = T[7 * SK_TL + ((w - l7) & TLM)];                       z7 += ((v - z7) * lpc) >> 14; y7 = (z7 * g7) >> 14;
        s = (y0 + y1 + y2 + y3 + y4 + y5 + y6 + y7) >> 2;        /* Householder: y - 2/8 of the sum */
        v = sat16(y0 - s + xl); T[w] = (s16)v;                     if (absv(v) > pk) pk = absv(v);
        v = sat16(y1 - s + xr); T[SK_TL + w] = (s16)v;
        v = sat16(y2 - s + xl); T[2 * SK_TL + w] = (s16)v;
        v = sat16(y3 - s + xr); T[3 * SK_TL + w] = (s16)v;
        v = sat16(y4 - s + xl); T[4 * SK_TL + w] = (s16)v;         if (absv(v) > pk) pk = absv(v);
        v = sat16(y5 - s + xr); T[5 * SK_TL + w] = (s16)v;
        v = sat16(y6 - s + xl); T[6 * SK_TL + w] = (s16)v;
        v = sat16(y7 - s + xr); T[7 * SK_TL + w] = (s16)v;
        w = (w + 1) & TLM;
        b[2 * i] = y0 + y2 + y4 + y6;
        b[2 * i + 1] = y1 + y3 + y5 + y7;
    }
    tk_m[0] = m0, tk_m[3] = m3;
    tk_lp[0] = z0, tk_lp[1] = z1, tk_lp[2] = z2, tk_lp[3] = z3, tk_lp[4] = z4, tk_lp[5] = z5, tk_lp[6] = z6,
    tk_lp[7] = z7;
    tk_peak = pk;
    tw = w;
}

/* ---- SHIMMER: two voices, each two windowed heads on a line moving at the interval's rate ------------- */
#define SH_GL   1536                            /* a head's sweep: 64 ms at 24 kHz */
HOT static void shimmer(s32 *b, s32 amt, int mode, int i1, int i2)
{
    s16 *P = SK_PIT;
    s32 inc[2], on[2];
    u32 p = pw, ph0 = sh_ph[0], ph1 = sh_ph[1];
    int i, k;
    on[0] = 1;
    on[1] = i2 > 0;
    inc[0] = (4096 - exp2_q12(sky_shift[i1])) * 4096 / SH_GL;
    inc[1] = on[1] ? (4096 - exp2_q12(sky_shift[i2 - 1])) * 4096 / SH_GL : 0;
    for (i = 0; i < HN; i++) {
        s32 src = 0, o = 0;
        u32 ph[2];
        if (mode != 1)                          /* INPUT, IN+REG: the tank's input */
            src = (b[2 * i] + b[2 * i + 1]) >> 1;
        if (mode != 0)                          /* REGEN, IN+REG: its output */
            src += sh_prev[i];
        P[p] = (s16)sat16(src);
        ph0 = (ph0 + (u32)inc[0]) & 0xffffff;
        ph1 = (ph1 + (u32)inc[1]) & 0xffffff;
        ph[0] = ph0, ph[1] = ph1;
        for (k = 0; k < 2; k++) {
            u32 q, h;
            if (!on[k])
                continue;
            for (h = 0; h < 2; h++) {           /* two heads half a sweep apart, each faded in and out */
                s32 wv;
                q = (ph[k] + h * 0x800000u) & 0xffffff;
                wv = (s32)((q < 0x800000u ? q : 0xffffffu - q) >> 8);
                o += (rdm(P, PLM, p, ((q >> 8) * SH_GL >> 4) + 8192) * wv) >> 15;
            }
        }
        o = (o * amt) >> 12;
        b[2 * i] += o;
        b[2 * i + 1] += o;
        p = (p + 1) & PLM;
    }
    pw = p, sh_ph[0] = ph0, sh_ph[1] = ph1;
}

/* ---- the after-stage: TONE, VOWEL's vowels, SWIRL's resonant tone, HALL's MID, LOW END, WIDTH -------- */
static const u8 vw_f[3][2] = { { 134, 150 }, { 108, 134 }, { 94, 128 } };   /* AH, OH, OO: pd_svf's steps */
static const u8 vw_ab[7][2] = { { 0, 2 }, { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 2, 2 }, { 0, 0 } };

HOT static void vowel(s32 *b, int vowel, int reso)
{
    static const s16 qv[3] = { 11469, 5734, 2458 };     /* MILD, MEDIUM, HIGH: the formants' damping, Q14 */
    s32 q = qv[reso], f[4], m, breath = 300 + (sk[K_TONE] >> 4);       /* TONE: the breath, 0.07 .. 0.57 */
    s32 jit = (sk[K_MOD] * 6) >> 15;           /* MOD: the singers wander */
    int i, k, va = vw_ab[vowel][0], vb = vw_ab[vowel][1];
    ch_ph += lfo_inc((s32)lfo_mhz(120)) * N;
    if (vowel == 6) {                           /* RANDOM: a new vowel every second and a half */
        if (++ch_rw >= 2250) {
            ch_rw = 0;
            ch_tgt = (s32)rnd_n(3) << 15;
        }
        ch_m += (ch_tgt - ch_m) >> 9;
        va = ch_m >> 15;
        vb = va < 2 ? va + 1 : 2;
        m = ch_m & 32767;
    } else {
        m = (sinq(ch_ph) + 32768) >> 1;
    }
    for (k = 0; k < 2; k++) {
        s32 idx = vw_f[va][k] + (((vw_f[vb][k] - vw_f[va][k]) * m) >> 15);
        s32 jj = (sinq(ch_ph * 7 + (u32)k * 0x30000000u) * jit) >> 15;
        f[k] = pd_svf[lim(idx + jj, 250)];
        f[k + 2] = pd_svf[lim(idx + 2 - jj, 250)];   /* R's singers a little apart */
    }
    for (i = 0; i < 2 * N; i += 2) {
        for (k = 0; k < 2; k++) {
            s32 x = b[i + k], o = 0, j;
            for (j = 0; j < 2; j++) {
                int s = 2 * k + j;
                s32 hi;
                sv_lo[s] = lim(sv_lo[s] + ((f[2 * k + j] * sv_bd[s] + 8192) >> 14), 1 << 19);
                hi = lim(x - sv_lo[s] - ((q * ((sv_bd[s] + 4) >> 3) + 1024) >> 11), 1 << 19);
                sv_bd[s] = lim(sv_bd[s] + ((f[2 * k + j] * hi + 8192) >> 14), 1 << 19);
                o += sv_bd[s];
            }
            b[i + k] = lim((((o >> 4) * q) >> 9) + ((x * breath) >> 12), TOP);
        }
    }
}

HOT static void swirl_tone(s32 *b)                  /* SWIRL's TONE: a resonant low-pass, 470 Hz .. 6 kHz */
{
    s32 f = pd_svf[110 + ((sk[K_TONE] * 115) >> 15)], q = 7373;
    int i, k;
    for (i = 0; i < 2 * N; i += 2)
        for (k = 0; k < 2; k++) {
            s32 hi;
            sv_lo[4 + k] = lim(sv_lo[4 + k] + ((f * sv_bd[4 + k] + 8192) >> 14), 1 << 17);
            hi = lim(b[i + k] - sv_lo[4 + k] - ((q * sv_bd[4 + k] + 8192) >> 14), 131071);
            sv_bd[4 + k] = lim(sv_bd[4 + k] + ((f * hi + 8192) >> 14), 1 << 17);
            b[i + k] = lim(sv_lo[4 + k], TOP);
        }
}

HOT static void post(s32 *b, int mc)
{
    s32 t0 = po_lp[0], t1 = po_lp[1], l0 = po_lo[0], l1 = po_lo[1];
    int i, ti = 140 + ((sk[K_TONE] * 115) >> 15), lo = sky_step(ST.knob[K_LOW], 21);
    s32 lg = exp2_q12((lo - 10) * 38) - 4096, alo = pd_op[93];
    for (i = 0; i < 2 * N; i += 2) {             /* bounded, and nothing under 20 Hz (the fixed point's drift) */
        b[i] = lim(b[i], TOP);
        b[i + 1] = lim(b[i + 1], TOP);
        b[i] -= LP(po_dc[0], b[i], pd_op[0]);
        b[i + 1] -= LP(po_dc[1], b[i + 1], pd_op[0]);
    }
    if (mc == MC_VOWEL)
        vowel(b, pstep(P_P1), pstep(P_P2));
    else if (mc == MC_SWIRL)
        swirl_tone(b);
    else if (ti < 252) {                        /* TONE: 900 Hz .. open */
        s32 ac = pd_op[ti];
        for (i = 0; i < 2 * N; i += 2) {
            b[i] = LP(t0, b[i], ac);
            b[i + 1] = LP(t1, b[i + 1], ac);
        }
    }
    if (mc == MC_HALL && pstep(P_P2) != 10) {   /* MID: 300 Hz .. 3 kHz, -12 .. +6 dB */
        int st = pstep(P_P2);
        s32 mg = st < 10 ? -3072 + st * 307 : (st - 10) * 410;      /* the gain - 1, Q12 */
        s32 ma0 = po_ma[0], ma1 = po_ma[1], mb0 = po_mb[0], mb1 = po_mb[1], ah = pd_op[183], al = pd_op[103];
        for (i = 0; i < 2 * N; i += 2) {
            s32 h0 = LP(ma0, b[i], ah), h1 = LP(ma1, b[i + 1], ah);
            s32 q0 = LP(mb0, b[i], al), q1 = LP(mb1, b[i + 1], al);
            b[i] = lim(b[i] + (((h0 - q0) * mg) >> 12), TOP);
            b[i + 1] = lim(b[i + 1] + (((h1 - q1) * mg) >> 12), TOP);
        }
        po_ma[0] = ma0, po_ma[1] = ma1, po_mb[0] = mb0, po_mb[1] = mb1;
    }
    if (lo != 10) {                             /* LOW END: a shelf under 250 Hz, -9 .. +9 dB */
        for (i = 0; i < 2 * N; i += 2) {
            s32 q0 = LP(l0, b[i], alo), q1 = LP(l1, b[i + 1], alo);
            b[i] = lim(b[i] + ((q0 * lg) >> 12), TOP);
            b[i + 1] = lim(b[i + 1] + ((q1 * lg) >> 12), TOP);
        }
    }
    {                                           /* WIDTH: the wet's sides, 0 .. 2x */
        s32 wd = ss[S_WIDTH] >> 6;
        if (wd < 252 || wd > 260)
            for (i = 0; i < 2 * N; i += 2) {
                s32 m = (b[i] + b[i + 1]) >> 1, s = (((b[i] - b[i + 1]) >> 1) * wd) >> 8;
                b[i] = lim(m + s, TOP);
                b[i + 1] = lim(m - s, TOP);
            }
    }
    po_lp[0] = t0, po_lp[1] = t1, po_lo[0] = l0, po_lo[1] = l1;
}

/* ---- the tank machines: ROOM HALL PLATE SWELL SWIRL NEBULA VOWEL SHIMMER ------------------------------ */
static void m_tank(int mc, u32 pre, int hold, int freeze)
{
    s32 depth = (sk[K_MOD] * mod_max[mc]) >> 3, c = ap_coef[mc], inmod = 0, sz;
    u32 mhz = lfo_mhz(mod_mhz[mc]);
    int i, size = run_size;
    if (mc == MC_NEBULA && sk[K_MOD] > 21500)    /* NEBULA past 2:00: the oscillators speed up */
        mhz += (mhz * (u32)(sk[K_MOD] - 21500)) >> 11;
    if ((mc == MC_ROOM || mc == MC_HALL) && sk[K_MOD] > 16384)    /* the knob's second half: the input moves */
        inmod = ((sinq(tk_ph + 0x20000000u) + 32768) * ((sk[K_MOD] - 16384) >> 6)) >> 9;
    pdelay(bf, (s32)(pre << 12) + inmod);
    if (mc == MC_ROOM || mc == MC_HALL)
        early(bf, mc, size);
    if (mc == MC_ROOM)
        c = 6554 + pstep(P_P2) * 1000;          /* DIFFUSION: 0.2 .. 0.81 */
    else if (mc == MC_NEBULA)
        c = pstep(P_P1) * 1250;                 /* 0 (grainy) .. 0.76 */
    c = (c * (ss[S_DIFF] >> 6)) >> 8;           /* page 2's DIFFUSION: 0 .. 2x the machine's */
    half_in(bf, hb);                            /* on at 24 kHz */
    diffuse(hb, n_ap[mc], ap_size[mc], c);
    if (mc == MC_SWIRL)
        swirl(hb, pstep(P_P1), pstep(P_P2));
    if (mc == MC_SHIMMER) {
        int amt = pstep(P_X1), mode = pstep(P_X2);
        if (amt)
            shimmer(hb, amt * 190, mode, pstep(P_P1), pstep(P_P2));
    }
    if (freeze)
        for (i = 0; i < 2 * HN; i++)
            hb[i] = 0;
    sz = tk_size[mc][size];
    tank_set((u32)sz, rt_ms(mc), depth, mhz, hold ? 1 + !freeze : 0);
    tank(hb);
    for (i = 0; i < HN; i++)                    /* SHIMMER's REGEN hears the tank, a block on */
        sh_prev[i] = (hb[2 * i] + hb[2 * i + 1]) >> 6;
    half_out(hb, bf);
}

/* ---- SPRING ---------------------------------------------------------------------------------------- */
static const u16 spr_len[3] = { 1913, 2399, 2843 };
static const s16 spr_a[3] = { 18022, 19333, 17039 };

HOT static void m_spring(u32 pre, int hold, int freeze)
{
    static const s16 drive[4] = { 1, 3, 7, 18 }, mk[4] = { 4096, 2600, 1700, 1100 };   /* DWELL: gain, make-up */
    s16 *S = SK_SPR;
    int dwell = pstep(P_P1), ns = pstep(P_P2) + 1, i, k, s;
    u32 rt = rt_ms(MC_SPRING), w = spr_w, j = spr_j;
    s32 g[3], alp = pd_op[226 + ((sk[K_TONE] * 29) >> 15)];    /* 3.9 .. 8.5 kHz (at 24 kHz: 26 steps up) */
    s32 ahp = pd_op[136 - 5 * sky_step(ST.knob[K_LOW], 21)];  /* LOW END: 300 .. 30 Hz under */
    s32 dr = drive[dwell], m = mk[dwell], pk = fx_peak, inj = freeze ? 0 : 1;
    s32 depth = (sk[K_MOD] * mod_max[MC_SPRING]) >> 4, md;   /* frames at 24 kHz */
    pdelay(bf, (s32)(pre << 12));
    half_in(bf, hb);                            /* the springs run at 24 kHz */
    tk_ph += lfo_inc((s32)lfo_mhz(mod_mhz[MC_SPRING])) * N;
    md = (sinq(tk_ph) * depth) >> 15;
    for (k = 0; k < 3; k++) {
        g[k] = hold ? 16384 : exp2neg(9500u * (spr_len[k] + 200u) / rt);    /* the loop's filters take the rest */
        g[k] = (g[k] * fx_guard) >> 14;
    }
    for (i = 0; i < HN; i++) {
        s32 x = (hb[2 * i] + hb[2 * i + 1]) >> 1, l = 0, r = 0;
        x = lim(x * dr, 16384);                 /* the preamp: 1.5 x - 0.5 x^3, at half scale */
        x = ((x * 3 - ((((x * x) >> 14) * x) >> 14)) * m) >> 13;
        x *= inj;
        for (k = 0; k < ns; k++) {
            s16 *L = S + SK_SL * k;
            s32 v = k ? L[(w - (spr_len[k] >> 1)) & SPM] : rdm(L, SPM, w, ((u32)spr_len[0] << 11) + (u32)md + (u32)depth);
            s32 c = spr_a[k];
            for (s = 0; s < 14; s++) {          /* the chirp: stretched allpasses, two frames (24 kHz) each */
                s32 bv = spr_cb[k][s][j], u = sat16(v - ((bv * c) >> 15));
                spr_cb[k][s][j] = (s16)u;
                v = bv + ((u * c) >> 15);
            }
            if (!hold) {
                v = LP(spr_lp[k], v, alp);
                v -= LP(spr_hp[k], v, ahp);
            }
            v = sat16(x + ((v * g[k]) >> 14));
            L[w] = (s16)v;
            if (absv(v) > pk) pk = absv(v);
            if (ns == 1)
                l = r = v;
            else if (k == 0)
                l += v;
            else if (k == 1)
                r += v;
            else
                l += v >> 1, r += v >> 1;
        }
        hb[2 * i] = ns == 3 ? (l * 11) >> 4 : l;     /* three springs: at two's level */
        hb[2 * i + 1] = ns == 3 ? (r * 11) >> 4 : r;
        w = (w + 1) & SPM;
        j = (j + 1) & 1;
    }
    half_out(hb, bf);
    spr_w = w, spr_j = j;
    fx_peak = pk;
}

/* ---- TAPE: a multi-head tape echo (PRE-DELAY is the feedback), its heads smeared and into a reverb ---- */
static const u16 hd_pos[2][3][6] = {           /* the heads, Q12 of the last one's time: EVEN, UNEVEN */
    { { 1365, 2731, 4096 }, { 1024, 2048, 3072, 4096 }, { 683, 1365, 2048, 2731, 3413, 4096 } },
    { { 860, 2130, 4096 }, { 1188, 1925, 2908, 4096 }, { 532, 1270, 1802, 2539, 3399, 4096 } } };

HOT static void m_tape(int hold, int freeze)
{
    static const u8 nh[3] = { 3, 4, 6 };
    s16 *D = SK_PDL;
    int hi = pstep(P_P1), sp = pstep(P_P2), dif = pstep(P_X1), H = nh[hi], i, h;
    u32 T = rt_ms(MC_TAPE) * 48, p = dw, d[6];
    s32 fb = hold ? 4096 : (kt(pd_fb, sk[K_PRE]) * 3973) >> 12, pk = fx_peak;
    s32 alp = pd_op[170 + ((sk[K_TONE] * 80) >> 15)], ahp = pd_op[120 - 5 * sky_step(ST.knob[K_LOW], 21)];
    s32 depth = sk[K_MOD] >> 2, ws[2], wv[2], fl = mg_fb, z = mg_lp, y = mg_hp, inj = freeze ? 0 : 1;
    fb = (fb * fx_guard) >> 14;
    for (h = 0; h < H; h++)
        d[h] = T * hd_pos[sp][hi][h];           /* Q12 frames */
    mg_ph += lfo_inc((s32)lfo_mhz(mod_mhz[MC_TAPE])) * N;      /* wow, and flutter ten times faster */
    mg_ph2 += lfo_inc((s32)lfo_mhz(mod_mhz[MC_TAPE]) * 11) * N;
    {
        s32 t = ((sinq(mg_ph) * depth) >> 10) + ((sinq(mg_ph2) * depth) >> 14) + (depth << 5);
        ws[0] = (t - mg_w[0]) / N;
        ws[1] = (t + ((sinq(mg_ph + 0x40000000u) * depth) >> 12) - mg_w[1]) / N;
    }
    wv[0] = mg_w[0], wv[1] = mg_w[1];
    for (i = 0; i < N; i++) {
        s32 x = (bf[2 * i] + bf[2 * i + 1]) >> 1, l = 0, r = 0, last = 0, prev = 0, v;
        v = sat16(x * inj + fl);
        D[2 * p] = (s16)v;
        if (absv(v) > pk) pk = absv(v);
        wv[0] += ws[0], wv[1] += ws[1];
        for (h = 0; h < H; h++) {
            v = rds(D, p, d[h] + (u32)wv[h & 1], 0);
            if (h & 1) r += v; else l += v;
            prev = last;
            last = v;
        }
        v = sp ? (last + prev) >> 1 : last;     /* EVEN: the last head back; UNEVEN: the last two */
        if (!hold) {
            v = LP(z, v, alp);
            v -= LP(y, v, ahp);
        }
        fl = (v * fb) >> 12;
        bf[2 * i] = (l * 2896) >> 12;
        bf[2 * i + 1] = (r * 2896) >> 12;
        p = (p + 1) & PDM;
    }
    dw = p;
    mg_fb = fl, mg_lp = z, mg_hp = y, mg_w[0] = wv[0], mg_w[1] = wv[1];
    fx_peak = pk;
    half_in(bf, hb);                            /* the heads smeared, and into a reverb, at 24 kHz */
    diffuse(hb, 2, ap_size[MC_TAPE], (dif * 1100 * (ss[S_DIFF] >> 6)) >> 8);
    {                                           /* more of the reverb with DIFFUSION */
        s32 lvl = 1200 + dif * 120;
        int k;
        for (k = 0; k < 2 * HN; k++)
            hw[k] = hb[k];
        tank_set(tk_size[MC_TAPE][0], 700 + (u32)dif * 120, 0, 300, 0);
        tank(hb);
        for (k = 0; k < 2 * HN; k++)
            hb[k] = hw[k] + ((hb[k] * lvl) >> 13);
    }
    half_out(hb, bf);
}

/* ---- NONLINEAR: 16 taps over its TIME in a shape, PRE-DELAY their feedback, then a late reverb ---------- */
HOT static void m_nonlin(int hold, int freeze)
{
    s16 *D = SK_PDL;
    int shp = pstep(P_P1), dif = pstep(P_P2), ld = pstep(P_X1), ll = pstep(P_X2), ms = pstep(P_X3), i, k;
    const s16 *gt = sky_nlg + 16 * shp;
    u32 T = rt_ms(MC_NONLIN) * 48, p = dw, d[16], mhz = (u32)exp2_q12(ms * 92) * 100 >> 12;  /* 0.1 .. 6.8 Hz */
    s32 fb = (kt(pd_fb, sk[K_PRE]) * 3891) >> 12, fl = nl_fb, pk = fx_peak, inj = freeze ? 0 : 1, sum = 0;
    for (k = 0; k < 16; k++)                    /* FEEDBACK: as a share of what the taps add up to */
        sum += gt[k];
    fb = (((fb * fx_guard) >> 14) << 16) / (sum > 4096 ? sum : 4096);
    if (hold && !ll)                            /* HOLD holds the late reverb: some of it, even at LATE LEVEL 0 */
        ll = 9;
    for (k = 0; k < 16; k++)                    /* the taps, plain reads (CPU): MOD moves the late reverb */
        d[k] = (T * (u32)(k + 1)) >> 4;
    for (i = 0; i < N; i++) {
        s32 l = 0, r = 0, v;
        v = sat16(bf[2 * i] * inj + fl);
        D[2 * p] = (s16)v;
        if (absv(v) > pk) pk = absv(v);
        D[2 * p + 1] = (s16)sat16(bf[2 * i + 1] * inj + fl);
        for (k = 0; k < 16; k += 2) {
            l += (D[2 * ((p - d[k]) & PDM)] * gt[k]) >> 15;
            r += (D[2 * ((p - d[k + 1]) & PDM) + 1] * gt[k + 1]) >> 15;
        }
        fl = (((l + r) >> 1) * fb) >> 12;
        bf[2 * i] = l;
        bf[2 * i + 1] = r;
        p = (p + 1) & PDM;
    }
    dw = p;
    nl_fb = fl;
    fx_peak = pk;
    half_in(bf, hb);                            /* the diffusers and the late reverb at 24 kHz */
    diffuse(hb, 2, ap_size[MC_NONLIN], (dif * 1250 * (ss[S_DIFF] >> 6)) >> 8);
    if (ll) {                                   /* the late reverb: LATE DECAY 0.3 .. 8 s, LATE LEVEL */
        s32 lvl = ll * 228;
        for (k = 0; k < 2 * HN; k++)
            hw[k] = hb[k];
        tank_set(tk_size[MC_NONLIN][0], ((u32)300 * (u32)exp2_q12(ld * 72)) >> 12,
                 (sk[K_MOD] * 6) >> 3, mhz, hold ? 1 + !freeze : 0);
        tank(hb);
        for (k = 0; k < 2 * HN; k++)
            hb[k] = hw[k] + ((hb[k] * lvl) >> 13);
    }
    half_out(hb, bf);
}

/* ---- REFLECTIONS: the image sources of a room around the amp, then a short tail ---------------------- */
/* The room: 100 .. 1000 sq ft by DECAY, SQUARE, RECT (wide) or OBLONG (deep); you at the front, the amp at
 * LOCATION X (left .. right) and Y (front .. back). 20 reflections, of the first and second order, each as
 * late as its path is longer than the direct one, as loud as its path is shorter and the walls harder (TONE),
 * from its side. Worked out again when they change. */
static void refl_calc(int lx, int ly, int shp)
{
    s32 s = (305 * exp2_q12((ST.knob[K_DECAY] * 425) / 127)) >> 12;    /* a square's side, cm */
    s32 W = shp == 1 ? (s * 1272) >> 10 : shp == 2 ? (s * 805) >> 10 : s;
    s32 L = shp == 1 ? (s * 805) >> 10 : shp == 2 ? (s * 1272) >> 10 : s;
    s32 sx = (W * (154 + 115 * lx)) >> 10, sy = (L * (256 + 110 * ly)) >> 10, hx = W / 2, hy = (L * 123) >> 10;
    s32 wall = 18000 + ST.knob[K_TONE] * 110, d0, n = 0, i, j;
    d0 = (s32)isqrt((u32)((sx - hx) * (sx - hx) + (sy - hy) * (sy - hy)));
    if (d0 < 50) d0 = 50;
    for (i = -2; i <= 2; i++)
        for (j = -2; j <= 2; j++) {
            s32 ix, iy, dx, dy, d, g, px, o = (i < 0 ? -i : i) + (j < 0 ? -j : j);
            if ((!i && !j) || o == 4 || n >= 20)
                continue;
            ix = i * W + ((i & 1) ? W - sx : sx);
            iy = j * L + ((j & 1) ? L - sy : sy);
            dx = ix - hx, dy = iy - hy;
            d = (s32)isqrt((u32)(dx * dx + dy * dy));
            if (d <= d0) d = d0 + 1;
            g = (d0 << 15) / d;
            g = (g * wall) >> 15;
            if (o > 1) g = (g * wall) >> 15;
            if (o > 2) g = (g * wall) >> 15;
            px = (dx << 15) / d;                /* -1 left .. 1 right */
            rf_d[n] = ((u32)(d - d0) * 1433u) >> 10;   /* cm to frames */
            if (rf_d[n] > SK_PL - 64) rf_d[n] = SK_PL - 64;
            rf_gl[n] = (g * (32768 - ((px * 26214) >> 15))) >> 15;
            rf_gr[n] = (g * (32768 + ((px * 26214) >> 15))) >> 15;
            n++;
        }
}

HOT static void m_refl(u32 pre, int hold, int freeze)
{
    s16 *D = SK_PDL, *P = SK_PIT;
    int lx = pstep(P_P1), ly = pstep(P_P2), shp = pstep(P_X1), i, k;
    u32 key = (u32)(ST.knob[K_DECAY] | ST.knob[K_TONE] << 7 | lx << 14 | ly << 17 | shp << 20), p = dw, q = pw;
    s32 depth = sk[K_MOD] >> 3, t12, st, d;
    if (key != rf_key) {
        rf_key = key;
        refl_calc(lx, ly, shp);
    }
    tk_ph += lfo_inc((s32)lfo_mhz(mod_mhz[MC_REFL])) * N;          /* MOD: the pre-delay wanders, a chorus */
    t12 = (s32)(pre << 12) + (((sinq(tk_ph) + 32768) * depth) >> 12);
    st = (t12 - pre12) / N;
    if (st > 2048) st = 2048;
    if (st < -2048) st = -2048;
    d = pre12;
    for (i = 0; i < N; i++) {
        s32 x = (bf[2 * i] + bf[2 * i + 1]) >> 1, l = 0, r = 0;
        D[2 * p] = D[2 * p + 1] = (s16)sat16(x);
        d += st;
        P[q] = (s16)rds(D, p, (u32)d, 0);       /* the amp, pre-delayed: the reflections tap that */
        for (k = 0; k < 20; k++) {
            s32 v = P[(q - rf_d[k]) & PLM];
            l += (v * rf_gl[k]) >> 15;
            r += (v * rf_gr[k]) >> 15;
        }
        bf[2 * i] = l;
        bf[2 * i + 1] = r;
        p = (p + 1) & PDM;
        q = (q + 1) & PLM;
    }
    pre12 = d;
    dw = p;
    pw = q;
    for (k = 0; k < 2 * N; k++)
        wet[k] = bf[k];
    half_in(bf, hb);                            /* the tail at 24 kHz */
    diffuse(hb, 2, ap_size[MC_REFL], (ap_coef[MC_REFL] * (ss[S_DIFF] >> 6)) >> 8);
    if (freeze)
        for (k = 0; k < 2 * HN; k++)
            hb[k] = 0;
    tank_set(96 + ((u32)ST.knob[K_DECAY] >> 1), rt_ms(MC_REFL) * 2, 0, 500, hold ? 1 + !freeze : 0);
    tank(hb);
    half_out(hb, bf);
    for (k = 0; k < 2 * N; k++)
        bf[k] = wet[k] + (bf[k] >> 2);
    /* the dry follows the amp across the room */
    dryg[0] = lx <= 3 ? 4096 : 4096 - (lx - 3) * 1000;
    dryg[1] = lx >= 3 ? 4096 : 4096 - (3 - lx) * 1000;
}

/* ---- a machine change, and the block -------------------------------------------------------------- */
static int change_key(int mc)                   /* what a change mutes for: the machine, and its tank's size */
{
    int sz = (mc == MC_ROOM || mc == MC_HALL || mc == MC_PLATE) ? sky_step(ST.knob[K_P1], 2) : 0;
    return mc * 2 + sz;
}

static void change_block(void)
{
    int mc = ST.opt[O_TYPE] % MC_N, key = change_key(mc);
    if (mc_run < 0) {                           /* the first block: the buffers are clear */
        mc_run = mc;
        run_size = key & 1;
        state_clear();
        return;
    }
    if (chg == 0 && key != mc_run * 2 + run_size)
        chg = 1;
    if (chg == 1) {                             /* muting the wet over 5 ms */
        chg_g -= 32767 / 8;
        if (chg_g <= 0) {
            int was = mc_run;
            chg_g = 0;
            mc_run = mc;
            run_size = key & 1;
            if (!ST.opt[O_SPILL] && was != mc) {    /* SPILLOVER off: the buffers cleared (and the line, for
                                                       TAPE's tape and NONLINEAR's taps) */
                int tape = was == MC_TAPE || was == MC_NONLIN || mc == MC_TAPE || mc == MC_NONLIN;
                clr_at = tape ? 0 : 0x00080000u;
                clr_end = (u32)(PD_CLEAR_HI - PD_BULK);
                state_clear();
                chg = 2;
            } else {
                chg = 3;
            }
        }
    } else if (chg == 2) {                      /* clearing, 16 KB a block */
        u32 e = clr_at + 16384 > clr_end ? clr_end : clr_at + 16384;
        u32 *p = (u32 *)(PD_BULK + clr_at), *q = (u32 *)(PD_BULK + e);
        while (p < q) {
            p[0] = p[1] = p[2] = p[3] = 0;
            p += 4;
        }
        clr_at = e;
        if (clr_at >= clr_end)
            chg = 3;
    } else if (chg == 3) {                      /* the wet back over 10 ms */
        chg_g += 32767 / 16;
        if (chg_g >= 32767) {
            chg_g = 32767;
            chg = 0;
        }
    }
}

HOT static void fx_block(s32 *w, const s32 *x, int mode)
{
    int i, mc, hold, freeze, sw_on = 0;
    u32 pre;
    s32 md, mw, dl, e0, e1, es, t0, t1, ts, dlev;

    change_block();
    mc = mc_run;
    hold = ST.sw[SW_HOLD] || ST.held[SW_HOLD];
    freeze = hold && ST.opt[O_HOLD] == HM_FREEZE;
    ST.flag[F_MC] = (u8)mc;
    ST.flag[F_HOLD] = (u8)(hold ? 1 + ST.opt[O_HOLD] : 0);
    ST.flag[F_SWITCH] = (u8)(chg != 0);
    ST.val[V_RT] = rt_ms(mc);
    dryg[0] = dryg[1] = 4096;

    for (i = 0; i < 2 * N; i++)                 /* the effect's input: half scale; bypassed, silence */
        bf[i] = mode == PD_RUN ? x[i] >> 1 : 0;

    /* PRE-DELAY: 0 .. 1.5 s, or with SYNC and a tempo a division, 1/32 .. 1/2 */
    {
        u32 q = (u32)(sk[K_PRE] >> 3) * (u32)(sk[K_PRE] >> 3);       /* 0 .. 4064^2 */
        pre = ST.opt[O_SYNC] && qfr ? sync_frames(sk[K_PRE], pre_div, 9) : ((q >> 8) * 1117u) >> 10;   /* 0 .. 1.5 s */
    }
    if (pre > 72000) pre = 72000;
    ST.val[V_PRE] = pre;
    if (first) {                                /* the first block: the pre-delay where it is set */
        pre12 = (s32)(pre << 12);
        first = 0;
    }

    /* SWELL: a note's start (the input over -40 dB, after a quiet, or jumping) starts the rise again */
    e0 = sw_env;
    if (mc == MC_SWELL) {
        s32 pk = 0;
        for (i = 0; i < 2 * N; i++)
            if (absv(bf[i]) > pk) pk = absv(bf[i]);
        sw_fast = pk > sw_fast ? pk : sw_fast - (sw_fast >> 6);
        sw_slow += (pk - sw_slow) >> 7;
        if (sw_fast < 50)
            sw_armed = 1;
        if (pk > 150 && (sw_armed || pk > 4 * sw_slow + 100)) {
            sw_armed = 0;
            sw_env = 0;
        }
        sw_inc = (s32)(32767u * N / (u32)sky_rise[pstep(P_P1)]) + 1;
        if (sw_env < 32767)
            sw_env = sw_env + sw_inc > 32767 ? 32767 : sw_env + sw_inc;
        sw_on = sw_env < 32767;
    } else {
        sw_env = 32767;
    }
    e1 = sw_env;
    es = (e1 - e0) / N;
    if (mc == MC_SWELL && pstep(P_P2) == 1)     /* SWELL DRY: the dry swells, and the reverb hears that */
        for (i = 0; i < N; i++) {
            s32 g = e0 + es * (i + 1);
            bf[2 * i] = (bf[2 * i] * g) >> 15;
            bf[2 * i + 1] = (bf[2 * i + 1] * g) >> 15;
        }

    if (chg == 2) {                             /* clearing: no wet */
        for (i = 0; i < 2 * N; i++)
            bf[i] = 0;
    } else if (mc == MC_SPRING) {
        m_spring(pre, hold, freeze);
    } else if (mc == MC_TAPE) {
        m_tape(hold, freeze);
    } else if (mc == MC_NONLIN) {
        m_nonlin(hold, freeze);
    } else if (mc == MC_REFL) {
        m_refl(pre, hold, freeze);
    } else {
        m_tank(mc, pre, hold, freeze);
    }
    if (mc == MC_SPRING || mc == MC_TAPE || mc == MC_NONLIN) {     /* their feedback's guard */
        if (fx_peak > 30000)
            fx_guard -= fx_guard >> 6;
        else if (fx_guard < 16384)
            fx_guard += 16;
        fx_peak = 0;
    }
    if (chg != 2) {
        for (i = 0; i < 2 * N; i++)             /* the wet at the dry's level */
            bf[i] = (bf[i] * out_g[mc]) >> 11;
        post(bf, mc);
    }

    /* T2 held: the wet out, and on release it swells back over SWELL's RISE TIME (1 s on the others) */
    t0 = t2_env;
    if (ST.sw[SW_SWELL]) {
        t2_env -= 32767 / 16;
        if (t2_env < 0) t2_env = 0;
    } else if (t2_env < 32767) {
        u32 rf = mc == MC_SWELL ? (u32)sky_rise[pstep(P_P1)] : 48000u;
        t2_env += (s32)(32767u * N / rf) + 1;
        if (t2_env > 32767) t2_env = 32767;
        sw_on = 1;
    }
    t1 = t2_env;
    ts = (t1 - t0) / N;
    ST.flag[F_SWELL] = (u8)(sw_on || ST.sw[SW_SWELL]);

    /* MIX: all dry at the minimum, 50/50 at three quarters, all wet at the maximum */
    md = sk[K_MIX] <= 24384 ? 4096 : ((32512 - sk[K_MIX]) << 12) / 8128;
    mw = sk[K_MIX] >= 24384 ? 4096 : (sk[K_MIX] << 12) / 24384;
    mw = (mw * (chg_g >> 3)) >> 12;
    dlev = ss[S_DRY] >= 32512 ? 4096 : ss[S_DRY] >> 3;
    dl = (md * dlev) >> 12;
    {
        int swd = mc == MC_SWELL && pstep(P_P2) == 1, sww = mc == MC_SWELL && !swd;
        s32 d0 = (dl * dryg[0]) >> 12, d1 = (dl * dryg[1]) >> 12;
        for (i = 0; i < N; i++) {
            s32 g = (t0 + ts * (i + 1)) >> 3, yl = bf[2 * i], yr = bf[2 * i + 1], xl = x[2 * i], xr = x[2 * i + 1];
            if (sww) {
                s32 e = e0 + es * (i + 1);
                g = (g * e) >> 15;
            } else if (swd) {
                s32 e = e0 + es * (i + 1);
                xl = (xl * e) >> 15;
                xr = (xr * e) >> 15;
            }
            yl = (((yl * g) >> 12) * mw) >> 12;
            yr = (((yr * g) >> 12) * mw) >> 12;
            if (mode == PD_RUN) {
                w[2 * i] = lim(((xl * d0) >> 12) + yl, TOP);
                w[2 * i + 1] = lim(((xr * d1) >> 12) + yr, TOP);
            } else {
                w[2 * i] = mode == PD_TAILS ? yl : 0;
                w[2 * i + 1] = mode == PD_TAILS ? yr : 0;
            }
        }
    }
    if (ST.opt[O_CAB] && mode == PD_RUN) {      /* CAB FILTER: a speaker's band, 90 Hz .. 4.5 kHz */
        s32 ah = pd_op[56], al = pd_op[178];
        for (i = 0; i < 2 * N; i += 2) {
            int k;
            for (k = 0; k < 2; k++) {
                s32 v = w[i + k];
                v -= LP(po_cab[k], v, ah);
                w[i + k] = LP(po_cab[2 + k], v, al);
            }
        }
    }
}
