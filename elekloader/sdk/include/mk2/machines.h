/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MK2_MACHINES_H
#define MK2_MACHINES_H
#include <stdint.h>

/* machines-dn2 0.1: contribute a pointer to mk2_synths for a stock clone.
 * mk2_voice_synths registers a SHARC voice with the same descriptor;
 * its module carries the matching artifact (digitone-mk2/voice.h). SYN has
 * one empty page named after the mod unless mk2_voice_pages gives it pages;
 * clone selects stock defaults/permissions.
 * DN2 IDs 5..31 are permanent across builds; never derive them from load order or reuse them
 * for a different mod. clone is 0=FM TONE, 1=WAVETONE, 2=FM DRUM, 3=SWARMER.
 * Names are ASCII (31 characters), labels at most 7. Requires core 0.1. */
struct mk2_synth {
    uint32_t id, clone;
    const char *name, *label;
};

/* machines-dn2 0.1: contribute a pointer to mk2_voice_pages to give a SHARC
 * synth (mk2_voice_synths) its own SYN pages, 1..5 of 8 knobs. Page p, knob k
 * is sound slot 25 + 8p + k, saved with the sound. A NULL knob is an empty
 * slot. Labels are ASCII, 1..5 characters; defaults 0..127. SYN steps
 * through the pages, headed "NAME (p/n)". A knob answers the MIDI CC/NRPN
 * the clone has for its slot. An invalid entry is ignored
 * and the synth keeps one empty page. */
struct mk2_param {
    const char *label;
    uint32_t def;
};
struct mk2_page {
    const struct mk2_param *knob[8];
};
struct mk2_voice_pages {
    uint32_t id, count;
    const struct mk2_page *page;
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
