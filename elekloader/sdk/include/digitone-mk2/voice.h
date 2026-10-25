/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DN2_VOICE_H
#define DN2_VOICE_H
#include <stdint.h>

/* dn2-voice-mono32-l2-v1: before the stock envelope, filter and mixer.
 * One call per matching voice slot per 32-sample block at 48 kHz.
 * A trigger's first block is silent; START marks the next, settled block.
 * Keep independent state per slot. The stock envelope owns release. */
#define DN2_VOICE_START 1u
struct dn2_voice_input {
    float *output;                 /* write all 32 samples */
    uint32_t slot;                 /* 0..15, not the track number */
    uint32_t pitch;                /* stock unsigned 8.8 MIDI note */
    uint32_t flags;                /* DN2_VOICE_START, otherwise zero */
};
void sharc_voice_render(const struct dn2_voice_input *input);
#endif
