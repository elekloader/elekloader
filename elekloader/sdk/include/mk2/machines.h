/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MK2_MACHINES_H
#define MK2_MACHINES_H
#include <stdint.h>

/* machines-dn2 0.1: contribute a pointer to mk2_voice_synths to register a SHARC
 * voice; its module carries the matching artifact (digitone-mk2/voice.h). SYN has
 * one empty page named after the mod unless mk2_voice_pages gives it pages.
 * The stock firmware sees every custom synth as its host type, FM TONE: its
 * permissions, and its parameter ownership and MIDI CCs for the knobs.
 * DN2 IDs 5..31 are permanent across builds; never derive them from load order or reuse them
 * for a different mod. Names are ASCII (31 characters), labels at most 7. Requires core 0.1. */
struct mk2_synth {
    uint32_t id;
    const char *name, *label;
};

/* machines-dn2 0.1: contribute a pointer to mk2_voice_pages to give a SHARC
 * synth (mk2_voice_synths) its own SYN pages, 1..5 of 8 knobs. Page p, knob k
 * is sound slot 25 + 8p + k, saved with the sound. A NULL knob is an empty
 * slot. Labels are ASCII, 1..5 characters; defaults 0..127. SYN steps
 * through the pages, headed "NAME (p/n)". A knob answers the MIDI CC/NRPN
 * FM TONE has for its slot. An invalid entry is ignored
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

/* machines-dn2 0.1: contribute a pointer to mk2_voice_filters to register a SHARC
 * filter with its matching artifact (digitone-mk2/filter.h). Its host type,
 * LOWPASS 4, supplies the FLTR page, defaults and parameters; its kernel
 * replaces only the audio. DN2 IDs 6..31 are permanent and independent of
 * synth IDs. Names/labels have the limits above. A missing mod keeps its ID
 * and uses MULTI-MODE. The shared name does not enable Digitakt II support. */
struct mk2_filter {
    uint32_t id;
    const char *name, *label;
    /* Optional, custom filters only: draws the FLTR page's response graph. freq, reso and g
     * are the page's shown FREQ, RESO and G, locks included, as 16-bit words (64.00 is
     * 0x4000); g is 0 without a declared G. Fill y[0..50], one per column left to right, with
     * 0 (bottom) to 16 (top), or 0xff for no dot; larger values are drawn at 16. Without it
     * the graph stays empty. */
    void (*curve)(uint32_t freq, uint32_t reso, uint32_t g, uint8_t y[51]);
    /* Optional, custom filters only: the FLTR page's G knob (sound slot 66), with its label
     * (1..5 ASCII characters, shown in capitals) and default (0..127). It answers G's CC 18
     * and NRPN, takes locks and is saved with the sound; the kernel gets it in its input.
     * Without it G stays a dotted cell. The linker refuses an invalid one. */
    const struct mk2_param *g;
};
#endif
