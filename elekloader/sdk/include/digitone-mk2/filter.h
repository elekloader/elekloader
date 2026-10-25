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
};
void sharc_filter_render(const struct dn2_filter_input *input);
#endif
