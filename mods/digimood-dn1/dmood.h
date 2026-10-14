/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiMood: a MOOD-style two-channel pedal (a Wet Channel and an always-listening Micro-Looper) for the Digitone
 * mk1's inputs, on the pedal shell (pedal.h). Shared by dsp.c (the render, interrupt level) and ui.c (the UI
 * task). */
#ifndef DMOOD_H
#define DMOOD_H

#define PD(x) dmood_##x
#include "pedal.h"

/* The buffers: the region mod.json claims in the profile's bulk area (0x47300000-0x47700000). The OS never
 * clears it, so the UI clears what the render reads before the render's first block (3.4 MB: 14 ticks). */
#ifndef PD_BULK                                         /* a host build of the DSP for tests sets its own */
#define PD_BULK         0x47300000u
#endif
#define MD_RT           720000u                         /* the Micro-Looper's ring: 15 s at 48K, stereo s16 */
#define MD_RING         ((s16 *)PD_BULK)                /* at half scale, 2,880,000 bytes */
#define MD_WL_FR        (1u << 17)                      /* the Wet Channel's line (DELAY, SLIP): 2.7 s at 48K */
#define MD_WLINE        ((s16 *)(PD_BULK + 0x002C0000u))
#define MD_REV          ((s16 *)(PD_BULK + 0x00340000u)) /* REVERB: 8 lines of 8192, then 8 allpasses of 1024 */
#define MD_REV_LINE     8192
#define PD_BULK_END     (PD_BULK + 0x00364000u)
#define PD_CLEAR_LO     PD_BULK
#define PD_CLEAR_HI     PD_BULK_END

/* page 1: the pedal's knobs, the wet side's on the left, the shared ones in the middle, the looper's on the right */
enum { K_TIME, K_MIX, K_LEN, K_BAL, K_WMOD, K_CLK, K_LMOD, K_TONE };
/* page 2: the hidden options */
enum { S_WIDTH, S_FADE, S_DIRECT, S_OUT, S_WOBL, S_DAMP, S_XFAD, S_RAMP };
/* page 3: the toggles and the dip switches */
enum { O_WET, O_LOOP, O_ROUT, O_SYNC, O_SPRD, O_LATCH, O_DRYK, O_NDUB };
enum { WET_REV, WET_DLY, WET_SLIP };
enum { LOOP_ENV, LOOP_TAPE, LOOP_STR };
enum { R_IN, R_MLIN, R_ML };                            /* ROUTING: what the Wet Channel hears */
enum { SY_OFF, SY_LOOP, SY_WET };                       /* SYNC: none, the looper to the wet, the wet to the looper */
enum { SP_OFF, SP_BOTH, SP_WET, SP_LOOP };              /* SPREAD, and SPREAD SOLO */
enum { C_SMOOTH, C_HALF };                              /* SETUP: SMOOTH CLOCK, LOOP BUFFER */
enum { SW_WET, SW_LOOP, SW_FRZ, SW_DUB };               /* the latches: the channels on, FREEZE, OVERDUB */
/* the controls' defaults (ui.c, and the host's tests) */
#define PD_KDEF     { 64, 64, 85, 64, 80, 115, 90, 127 }
#define PD_SDEF     { 96, 127, 0, 64, 24, 40, 24, 0 }
#define PD_ODEF     { WET_REV, LOOP_TAPE, R_MLIN, SY_OFF, SP_OFF, 0, 0, 0 }
#define F_WET       0                                   /* the Wet Channel: 0 off, 1 on, 2 frozen */
#define F_LOOP      1                                   /* the Micro-Looper: 0 listening, 1 playing, 2 overdubbing */
#define F_ENV       2                                   /* ENV: 1 while the input holds a slice */
#define F_CLK       3                                   /* CLOCK's step, 0-9 (2K .. 48K) */
#define V_LEN       0                                   /* val[0]: the loop's length, ticks */
#define V_RATE      1                                   /* val[1]: the clock, Hz */
#define V_TIME      2                                   /* val[2]: the wet's TIME, ticks */
#define V_GRAIN     3                                   /* val[3]: ENV's slice, STRETCH's grain, ticks */

#endif
