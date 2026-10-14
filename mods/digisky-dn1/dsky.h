/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiSky: a BigSky-style reverb pedal for the Digitone mk1's inputs, on the pedal shell (pedal.h). Shared by
 * dsp.c (the render, interrupt level) and ui.c (the UI task). */
#ifndef DSKY_H
#define DSKY_H

#define PD(x) dsky_##x
#include "pedal.h"

/* The buffers: the region mod.json claims in the profile's bulk area (2 MB). The OS never clears it, so the UI
 * clears what the render reads (1.14 MB, five ticks) before the render's first block. */
#ifndef PD_BULK                                         /* a host build of the DSP for tests sets its own */
#define PD_BULK         0x47900000u
#endif
#define SK_PD_FR        (1u << 17)                      /* the pre-delay line: 2.7 s, stereo s16 at half scale;
                                                           MAGNETO's tape and NONLINEAR's taps too */
#define SK_PDL          ((s16 *)PD_BULK)                /* 512 KB */
#define SK_TL           16384                           /* a tank line: 341 ms, mono s16 */
#define SK_TANK         ((s16 *)(PD_BULK + 0x00080000u)) /* eight lines: 256 KB */
#define SK_AL           8192                            /* an allpass's room */
#define SK_NAP          18                              /* six diffusers a side, three bloom stages a side */
#define SK_AP           ((s16 *)(PD_BULK + 0x000C0000u)) /* 288 KB */
#define SK_SL           8192                            /* a spring's line */
#define SK_SPR          ((s16 *)(PD_BULK + 0x00110000u)) /* three springs: 48 KB */
#define SK_PL           8192                            /* the shimmer's pitch line, mono */
#define SK_PIT          ((s16 *)(PD_BULK + 0x0011C000u)) /* 16 KB */
#define PD_CLEAR_LO     PD_BULK
#define PD_CLEAR_HI     (PD_BULK + 0x00120000u)

/* the twelve machines, in the BigSky's TYPE order (its CC 19 values) */
enum { MC_ROOM, MC_HALL, MC_PLATE, MC_SPRING, MC_SWELL, MC_BLOOM, MC_CLOUD, MC_CHORALE, MC_SHIMMER, MC_MAGNETO,
       MC_NONLIN, MC_REFL, MC_N };
enum { K_DECAY, K_PRE, K_MIX, K_TONE, K_P1, K_P2, K_MOD, K_LOW };                    /* page 1 */
enum { S_X1, S_X2, S_X3, S_BOOST, S_DIFF, S_MRATE, S_WIDTH, S_DRY };                /* page 2 */
enum { O_TYPE, O_HOLD, O_PERSIST, O_SYNC, O_CAB, O_SPILL };                          /* page 3 */
enum { HM_FREEZE, HM_INFINITE };
enum { SW_HOLD, SW_SWELL };                             /* the latches: HOLD (T1, CC 97), the momentary swell (T2) */
/* A machine's own parameters: five slots, PARAM 1 and 2 (knobs E, F) and three extras (page 2's A-C), each with
 * a number of steps as the BigSky's menu has them (0: the machine has none there). A slot's knob 0-127 holds
 * step (knob * n) >> 7. LOW END is knob H for every machine (21 steps). */
enum { P_P1, P_P2, P_X1, P_X2, P_X3 };
static const u8 sky_pn[MC_N][5] = {
    { 2, 21, 0, 0, 0 },         /* ROOM: SIZE (STUDIO, CLUB), DIFFUSION */
    { 2, 21, 0, 0, 0 },         /* HALL: SIZE (CONCERT, ARENA), MID */
    { 2, 0, 0, 0, 0 },          /* PLATE: SIZE (SMALL, LARGE) */
    { 4, 3, 0, 0, 0 },          /* SPRING: DWELL (CLEAN, COMBO, TUBE, OVERDRIVE), SPRINGS (1-3) */
    { 23, 2, 0, 0, 0 },         /* SWELL: RISE TIME (0.08-4 s), MODE (WET, DRY) */
    { 18, 18, 0, 0, 0 },        /* BLOOM: LENGTH, FEEDBACK */
    { 21, 0, 0, 0, 0 },         /* CLOUD: DIFFUSION */
    { 7, 3, 0, 0, 0 },          /* CHORALE: VOWEL, RESONANCE (MILD, MEDIUM, HIGH) */
    { 28, 29, 19, 3, 0 },       /* SHIMMER: SHIFT 1, SHIFT 2 (OFF first), AMOUNT, MODE (INPUT, REGEN, IN+REG) */
    { 3, 2, 21, 0, 0 },         /* MAGNETO: HEADS (3, 4, 6), SPACING (EVEN, UNEVEN), DIFFUSION */
    { 6, 21, 18, 19, 18 },      /* NONLINEAR: SHAPE, DIFFUSION, LATE DECAY, LATE LEVEL, MOD SPEED */
    { 7, 7, 3, 0, 0 },          /* REFLECTIONS: LOCATION X, LOCATION Y, SHAPE (SQUARE, RECT, OBLONG) */
};
static inline int sky_step(int knob, int n) { return n ? (knob * n) >> 7 : 0; }
static inline int sky_knob(int step, int n) { int k = (step * 128 + 64) / n; return k > 127 ? 127 : k; }

/* the controls' defaults (ui.c, and the host's tests): HALL, CONCERT, MID flat */
#define PD_KDEF     { 50, 20, 64, 64, 32, 64, 40, 64 }
#define PD_SDEF     { 0, 0, 0, 64, 64, 64, 64, 127 }
#define PD_ODEF     { MC_HALL, HM_INFINITE, 1, 0, 0, 0, 0, 0 }
#define F_MC        0                                   /* flag[0]: the machine the render runs */
#define F_HOLD      1                                   /* flag[1]: 0, 1 FREEZE, 2 INFINITE */
#define F_SWELL     2                                   /* flag[2]: 1 while a swell rises (SWELL, or T2) */
#define F_SWITCH    3                                   /* flag[3]: 1 while a machine change mutes the wet */
#define V_RT        0                                   /* val[0]: DECAY now, ms (MAGNETO's, NONLINEAR's time) */
#define V_PRE       1                                   /* val[1]: the pre-delay now, frames */

#endif
