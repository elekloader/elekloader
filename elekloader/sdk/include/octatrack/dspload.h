/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DSP code loaded at run time on the Octatrack (mods/dspload-ot, with the DSP bus): a mod that
 * isn't flashed (a TELOS mod from the card) gives its DSP code to ot_dsp_load, and from the next
 * audio frames on it runs on DSP core 0 as an ev_dsp_rx handler (mods/dspbus-ot): at the head of
 * every frame, with ev_dsp_rx's registers (a, x1, r0, r1 and the condition codes free) and an rts
 * at its end.
 *
 * The code goes into the shared window's clear gap (P:0x33dab..0x33fff, 597 words for every
 * loaded mod together). It is assembled by the SDK's DSP assembler (elekloader.sdk.build's
 * dsp_assemble): its words, and the words that hold an address in the code (rel), which
 * ot_dsp_load moves to where the code lands. */
#ifndef OCTATRACK_DSPLOAD_H
#define OCTATRACK_DSPLOAD_H
#include <stdint.h>

struct ot_dsp_blob {
    const uint32_t *words;          /* 24-bit DSP words */
    uint16_t n;                     /* how many */
    uint16_t entry;                 /* the handler, a word offset into the code */
    const uint16_t *rel;            /* the words holding an address in the code (offset from its start) */
    uint16_t nrel;
};

#define OT_DSP_SLOTS 4

/* Queue a blob: -> its slot (0..3), or -1 (no slot, or no room in the gap). Call it from a task
 * (a TELOS mod's drive_init); the frame interrupt sends it to core 1 over the host port, three
 * times on quiet frames, and the copier there binds it. */
int ot_dsp_load(const struct ot_dsp_blob *b);

/* What happened, for a debugger or a page: [0] blocks queued, [1] chunks sent, [2] doorbells
 * rung, [3] frames seen, [4] quiet (ping 0) frames. */
extern volatile uint32_t ot_dsp_counts[8];

#endif
