/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DN2_VOICE_H
#define DN2_VOICE_H
#include <stdint.h>

/* dn2-voice-mono32-l2-v1: before the stock envelope, filter and mixer.
 * One call per matching voice slot per 32-sample block at 48 kHz.
 * A trigger's first block is silent; START marks the next, settled block.
 * Keep independent state per slot. The stock envelope owns release.
 * The callback runs on its own 4 KiB stack and may use 2,048 bytes of it;
 * interrupts use the rest. An overrun mutes the synth.
 * DN2_VOICE_STATE(type) gives each slot a zeroed `type` at input->state,
 * kept between calls; reset it yourself on START. */
#define DN2_VOICE_START 1u
#define DN2_VOICE_PARAMS 40
#define DN2_VOICE_STATE(type) const uint32_t sharc_voice_state_bytes = sizeof(type)
struct dn2_voice_input {
    float *output;                 /* write all 32 samples */
    uint32_t slot;                 /* 0..15, not the track number */
    uint32_t pitch;                /* stock unsigned 8.8 MIDI note */
    uint32_t flags;                /* DN2_VOICE_START, otherwise zero */
    const uint32_t *param;         /* this voice's knobs (mk2/machines.h pages), unsigned 8.8:
                                      param[8p + k] is page p knob k, 0..32512 = 0..127.0,
                                      with locks and modulation applied */
    void *state;                   /* this slot's DN2_VOICE_STATE, or null without one */
};
void sharc_voice_render(const struct dn2_voice_input *input);
#endif
