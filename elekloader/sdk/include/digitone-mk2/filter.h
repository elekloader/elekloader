/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DN2_FILTER_H
#define DN2_FILTER_H
#include <stdint.h>

/* dn2-filter-mono32-l2-v1: replace the selected filter before stock AMP.
 * Process 32 mono floats at 48 kHz in place. Slot is 0..15, not a track.
 * The callback has at most 20 stack bytes; its caller adds eight bytes. */
#define DN2_FILTER_RESET 1u

struct dn2_filter_input {
    float *samples;
    uint32_t slot;
    uint32_t flags; /* RESET on entry to this filter or a new voice trigger. */
    /* This block's stock controls for the slot, once per 32 samples and not
     * smoothed. cutoff is FREQ/128 plus the stock filter envelope (pre-delay,
     * ATK/DEC/SUS/REL scaled by the bipolar ENV) and key tracking, clamped
     * to 0..1. resonance is RESO/128, 0..0.992. g is G/127, 0..1, when the
     * filter declares G (mk2/machines.h); without it, ignore g. */
    float cutoff;
    float resonance;
    float g;
};
void sharc_filter_render(const struct dn2_filter_input *input);
#endif
