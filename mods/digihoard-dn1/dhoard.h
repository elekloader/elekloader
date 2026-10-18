/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DigiHoard: an echo collector for the Digitone mk1's inputs, on the pedal shell (pedal.h). Shared by
 * dsp.c (the render, interrupt level) and ui.c (the UI task). */
#ifndef DHOARD_H
#define DHOARD_H

#define PD(x) dhoard_##x
#include "pedal.h"

/* The buffers: the region mod.json claims in the profile's bulk area (0x46200000-0x47300000). The OS never clears
 * it. The echo's lines come first and the UI clears them before the render's first block (512 KB, two ticks);
 * the memory after them is never cleared: the render counts how much of it it has written and reads nothing
 * older. */
#ifndef PD_BULK                                         /* a host build of the DSP for tests sets its own */
#define PD_BULK         0x46200000u
#endif
#define HB_LFR          (1u << 17)                      /* the echo's lines: 2.73 s, two channels s16 at half
                                                           scale (the primary echo, the secondary's) */
#define HB_LINE         ((s16 *)PD_BULK)
#define HB_MFR          8640000u                        /* the memory: 3 minutes, mono s16 */
#define HB_MEM          ((s16 *)(PD_BULK + 4u * HB_LFR))
#define PD_BULK_END     (PD_BULK + 4u * HB_LFR + 2u * HB_MFR)   /* 0x472FAC00 */
#define PD_CLEAR_LO     PD_BULK
#define PD_CLEAR_HI     (PD_BULK + 4u * HB_LFR)

enum { K_VOL, K_RPTS, K_SIZE, K_MOD, K_SPRD, K_SCAN, K_RAMP, K_MIX };               /* page 1 */
enum { S_WIDE, S_KEEP, S_MDRY, S_OUT, S_TONE, S_GLID, S_STAY, S_INPT };             /* page 2 */
enum { O_BANK, O_NUM, O_MODE, O_SCAN, O_MEM, O_LOOP, O_RAMP, O_SHAPE };             /* page 3 */
enum { BANK_A, BANK_OFF, BANK_B };
enum { MODE_IN, MODE_OUT, MODE_FEED };
enum { SCAN_AUTO, SCAN_MAN };
enum { MEM_NORM, MEM_CLCT, MEM_ALWS, MEM_BOTH };        /* the COLLECT and ALWAYS options */
enum { LOOP_MOM, LOOP_LTCH };
enum { RT_OFF, RT_RPTS, RT_SIZE, RT_MOD, RT_SPRD, RT_SCAN };        /* the knob RAMP moves */
enum { SH_RUP, SH_RDN, SH_BTOP, SH_BBOT, SH_RAND, SH_SQR };          /* ramp up or down once; bounce */
enum { C_MDRY, C_WIPE };                                /* SETUP rows */
enum { MD_SCAN, MD_OFF, MD_ON };                        /* MEMORY DRY */
enum { SW_SCAN, SW_LOOP, SW_CLEAR, SW_WIPE };            /* the latches: T1 hold, T2 hold, T3, T4 hold */
enum { M_NONE, M_SSPD, M_STAB, M_TRIM, M_MSPD, M_FILT, M_DROP };    /* the modifiers: none, A1-A3, B1-B3 */
enum { CMD_CLEAR = 1, CMD_RAMP = 2, CMD_EXP = 128 };    /* CMD_EXP + 0-127: the expression pedal */
/* the controls' defaults (ui.c, and the host's tests) */
#define PD_KDEF     { 64, 50, 64, 64, 0, 0, 40, 64 }
#define PD_SDEF     { 80, 100, 90, 64, 110, 40, 40, 64 }
#define PD_ODEF     { BANK_OFF, 0, MODE_OUT, SCAN_AUTO, MEM_NORM, LOOP_MOM, RT_OFF, SH_BTOP }
#define F_STATE     0                                   /* flag[0]: the bits below */
#define FS_LOOP     1
#define FS_MOVE     2                                   /* a head is moving (scanning) */
#define FS_AWAY     4                                   /* AUTO: away in the past */
#define FS_WIPE     8
#define FS_CLEARED  16                                  /* a second after CLEAR */
#define F_MOD       1                                   /* the modifier playing, M_* */
#define F_RAMP      2                                   /* the ramped knob's value now, 0-127 */
#define V_FILL      0                                   /* val[0]: frames in the memory */
#define V_DP        1                                   /* val[1]: the primary head, frames back */
#define V_DS        2                                   /* val[2]: the secondary's (0: SPREAD off) */
#define V_SIZE      3                                   /* val[3]: SIZE, frames */

#endif
