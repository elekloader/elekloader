/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiHedra: a Hedra-style three-voice rhythmic pitch shifter for the Digitone mk1's inputs, on the pedal shell
 * (pedal.h). Shared by dsp.c (the render, interrupt level) and ui.c (the UI task). */
#ifndef DHEDRA_H
#define DHEDRA_H

#define PD(x) dhedra_##x
#include "pedal.h"

/* The buffers: the region mod.json claims in the profile's bulk area. The OS never clears it, so the UI clears
 * it before the render's first block. */
#ifndef PD_BULK                                         /* a host build of the DSP for tests sets its own */
#define PD_BULK         0x47B00000u
#endif
#define DH_HFR          (1u << 16)                      /* the voices' input line: 1.37 s, mono s16, half scale */
#define DH_EFR          (1u << 16)                      /* the echo line: 1.37 s, stereo s16, half scale */
#define DH_HLINE        ((s16 *)PD_BULK)
#define DH_ELINE        ((s16 *)(PD_BULK + 2u * DH_HFR))
#define DH_DET          (PD_BULK + 2u * DH_HFR + 4u * DH_EFR)  /* the pitch detector's buffers, 4 KB: */
#define DH_DBUF         ((s16 *)DH_DET)                 /* the input at 12 kHz, a ring of 512 */
#define DH_FRAME        ((s16 *)(DH_DET + 1024u))       /* the frame being analysed, 384 */
#define DH_DN           ((u16 *)(DH_DET + 1792u))       /* its normalised difference, 193 */
#define PD_BULK_END     (DH_DET + 4096u)                /* 0x47B61000, 388 KB of the 896 KB */
#define PD_CLEAR_LO     PD_BULK
#define PD_CLEAR_HI     PD_BULK_END

enum { K_P1, K_P2, K_P3, K_MIX, K_TIME, K_FDBK, K_KEY, K_MTUN };        /* page 1 */
enum { S_TD1, S_TD2, S_TD3, S_GLIDE, S_LEV1, S_LEV2, S_LEV3, S_OUT };   /* page 2 */
enum { O_MODE, O_SCALE, O_RHYTHM, O_CORR, O_ALGO, O_HALF, O_SMOOTH, O_SPREAD };   /* page 3 */
enum { MODE_PFB, MODE_SERIES, MODE_DUALX, MODE_DUAL };   /* as the Hedra's CC 29, in its order */
enum { SC_MAJ, SC_MIN, SC_MEL, SC_HARM, SC_DBL, SC_LYDP, SC_MINP };
enum { CORR_NONE, CORR_LOOSE, CORR_STRICT };
enum { ALGO_CLASSIC, ALGO_SMOOTH };
enum { SP_WIDE, SP_NARROW, SP_MONO };
enum { SW_HARM, SW_SWELL, SW_HALF, SW_FREEZE };         /* T1-T4 (T3's state is the HALF setting) */
#define KEY_CHROMATIC   120                             /* KEY from here up: chromatic, as the Hedra's */
/* the controls' defaults (ui.c, and the host's tests) */
#define PD_KDEF     { 97, 120, 8, 64, 82, 40, 127, 0 }
#define PD_SDEF     { 0, 64, 97, 0, 64, 64, 64, 64 }
#define PD_ODEF     { MODE_SERIES, SC_MAJ, 0, CORR_LOOSE, ALGO_SMOOTH, 0, 1, SP_WIDE }
#define F_NOTE      0                                   /* flag[0]: the detected note's pitch class + 1, 0 none */
#define F_VOICED    1                                   /* 1 while the detector hears a pitch */
#define F_FREEZE    2
#define F_SWELL     3                                   /* 1 while a swell rises */
#define F_HARM      4                                   /* 1 while the voices are pitched (T1) */
#define V_PITCH     0                                   /* val[0]: the detected pitch, semitones above C0, Q12 */
#define V_TIME      1                                   /* val[1]: the main delay time, frames */
#define V_SHIFT     2                                   /* val[2]: voice 1's shift now, semitones Q12 (signed) */

#endif
