/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiTensor: a Tensor-style time-warp pedal for the Digitone mk1's inputs, on the pedal shell (pedal.h). Shared
 * by dsp.c (the render, interrupt level) and ui.c (the UI task). */
#ifndef DTENSOR_H
#define DTENSOR_H

#define PD(x) dtensor_##x
#include "pedal.h"

/* The buffer: the region mod.json claims in the profile's bulk area. The OS never clears it, so the UI clears
 * it before the render's first block. */
#ifndef PD_BULK                                         /* a host build of the DSP for tests sets its own */
#define PD_BULK         0x47700000u
#endif
#define DT_FR           (1u << 19)                      /* the tape: 10.9 s, stereo s16 at half scale */
#define DT_TAPE         ((s16 *)PD_BULK)
#define PD_BULK_END     (PD_BULK + 4u * DT_FR)          /* 0x47900000 */
#define PD_CLEAR_LO     PD_BULK
#define PD_CLEAR_HI     PD_BULK_END

enum { K_SPEED, K_PITCH, K_TIME, K_BLEND, K_RANDOM, K_GLIDE, K_TONE, K_SLICE };        /* page 1 */
enum { S_GRAIN, S_XFADE, S_RSPD, S_RPCH, S_RREV, S_RSTU, S_WIDTH, S_OUT };             /* page 2 */
enum { O_MODE, O_DIR, O_HOLD, O_CAPT, O_QUANT, O_PATN, O_RSYNC };                      /* page 3 */
enum { MODE_TAPE, MODE_STRETCH };
enum { DIR_FWD, DIR_REV, DIR_ALT };
enum { CAPT_LAST, CAPT_NEXT };
enum { SW_HOLD, SW_REV, SW_STOP, SW_RAND };             /* the latches: T1-T4 */
enum { CMD_HOLD_ON = 1, CMD_HOLD_OFF, CMD_STOP_ON, CMD_STOP_OFF };
enum { H_OFF, H_ARMED, H_CAPTURE, H_LOOP };             /* flag[0]: HOLD, the render's */
/* the controls' defaults (ui.c, and the host's tests) */
#define PD_KDEF     { 96, 64, 64, 64, 40, 20, 64, 0 }
#define PD_SDEF     { 50, 40, 64, 64, 64, 64, 64, 64 }
#define PD_ODEF     { MODE_STRETCH, DIR_FWD, 0, CAPT_LAST, 0, 0, 1, 0 }
#define F_HOLD      0
#define F_STOP      1                                   /* 1 while the tape slows or is stopped */
#define F_EVENT     2                                   /* the random event playing: 0 none, 1 speed, 2 pitch,
                                                           3 reverse, 4 stutter */
#define F_REV       3                                   /* 1 while it plays backwards */
#define V_WIN       0                                   /* val[0]: the window, frames */
#define V_SPEED     1                                   /* val[1]: the speed now, Q12, signed */

#endif
