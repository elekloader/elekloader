/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MK2_MACHINES_H
#define MK2_MACHINES_H
#include <stdint.h>

/* machines-dn2 0.1: contribute a pointer to mk2_synths for a stock clone.
 * mk2_voice_synths registers a SHARC voice with the same descriptor;
 * its module carries the matching artifact (digitone-mk2/voice.h). SYN has
 * one empty page named after the mod; clone selects stock defaults/permissions.
 * DN2 IDs 5..31 are permanent across builds; never derive them from load order or reuse them
 * for a different mod. clone is 0=FM TONE, 1=WAVETONE, 2=FM DRUM, 3=SWARMER.
 * Names are ASCII (31 characters), labels at most 7. Requires core 0.1. */
struct mk2_synth {
    uint32_t id, clone;
    const char *name, *label;
};

/* machines-dn2 0.1: contribute a pointer to mk2_filters for a stock filter
 * clone. mk2_voice_filters registers a SHARC filter with the same descriptor
 * and matching artifact (digitone-mk2/filter.h). Its FLTR page has no controls;
 * common defaults remain, but filter-specific parameter slots are unmapped.
 * DN2 IDs 6..31 are permanent and independent of synth IDs.
 * clone: 0=MULTI-MODE, 1=LOWPASS 4, 2=EQUALIZER, 3=COMB-, 4=LEGACY LP/HP,
 * 5=COMB+. Names/labels have the limits above. A missing mod keeps its ID
 * and uses MULTI-MODE. The shared name does not enable Digitakt II support. */
struct mk2_filter {
    uint32_t id, clone;
    const char *name, *label;
};
#endif
