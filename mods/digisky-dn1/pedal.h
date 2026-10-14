/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The pedal shell: what the GAS-killer pedals for the Digitone mk1 share (DigiMood, DigiHedra, DigiTensor,
 * DigiHabit, DigiSky), as DigiCosm and DigiChroma do it. A pedal is a whole mod on core-dn1 3.2's core_audio:
 * its Mod Menu entry opens its page, and while the page is open it owns the audio (the synths are muted) and
 * processes the Digitone's inputs; NO gives both back. Each mod carries an identical copy of pedal.h,
 * pedal_dsp.inc, pedal_ui.inc and pedal_tables.py; its own header defines PD() (its symbols' prefix) and its
 * buffers, then includes this.
 *
 * The render side (dsp.c, interrupt level) and the UI side (ui.c, the UI task) share struct pd_state: bytes
 * either side writes whole, and a ring of one-shot commands from the UI to the render. */
#ifndef PEDAL_H
#define PEDAL_H

#include <stdint.h>
#include "digitone-mk1/core3.h"

typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int8_t s8;

enum { CL_FREE, CL_TEMPO, CL_TAP };             /* the clock: free, the Digitone's tempo, tapped */
enum { TK_PRESS, TK_TAP, TK_HOLD, TK_UP };      /* a track key T1-T4: down; up before a hold; held; up after one */

#define PD_NCMD 8
struct pd_state {
    volatile u8 knob[8], sec[8];        /* page 1 and page 2 (FUNC): 0-127 */
    volatile u8 opt[8];                 /* page 3: each 0 .. its count - 1 */
    volatile u8 sw[4];                  /* the mod's latches (T1-T4 usually), 0/1 */
    volatile u8 held[4];                /* T1-T4 down now */
    volatile u8 cfg[4];                 /* the mod's own SETUP rows */
    volatile u8 bypass;                 /* 1: the pedal is off (the dry only, and the trails) */
    volatile u8 trails;                 /* 1: after a bypass the tails ring out */
    volatile u8 clock;                  /* CL_* */
    volatile u8 mono;                   /* 1: input L to both sides */
    volatile u8 active;                 /* the render's: 1 while it has sound to make */
    volatile u8 overload;               /* the render's: 1 when the guard bypassed the pedal (the UI clears it) */
    volatile u8 flag[8];                /* the mod's render reports bytes for its UI */
    volatile u32 val[4];                /* and words */
    volatile u32 tap;                   /* the tapped tempo, BPM x 120 */
    volatile u32 blocks;                /* blocks rendered: the UI's clock, 1500 a second */
    volatile u16 meter[2];              /* the output's peak, 0-32767, decayed by the UI */
    volatile u16 cpu;                   /* the pedal's render's share of a block, per mille */
    volatile u16 load;                  /* the whole render's, per mille (DMA timer 0; not in the emulator) */
    volatile u8 cmdq[PD_NCMD];          /* one-shot commands, the UI's ring: it writes cmdq[cw % N], then cw++ */
    volatile u8 cw, cr;                 /* the render takes them at its next block */
};

extern struct pd_state PD(st);
extern struct core_audio_owner PD(owner);
void PD(render)(int32_t *out, const int32_t *in);
void PD(rin)(void);                     /* ev_render_in, ev_render_out: the CPU guard's clock */
void PD(rout)(void);
void PD(dsp_reset)(void);
u32 PD(tempo)(void);                    /* the clock's tempo, BPM x 120 (0 when free) */

#endif
