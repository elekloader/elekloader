/* SPDX-License-Identifier: GPL-2.0-or-later
 * core-ar2 0.16: the silent-voice skip's switch (mods/core-ar2/idle.c,
 * mods/core-ar2/SKIPS.md, "The silent-voice skip (0.16)").
 *
 * The skip runs only when a mod asks for it and no mod holds it off:
 * core_voice_skip_want nonzero and core_voice_skip_hold 0. It is off at boot.
 * It changes the sound slightly (a silent voice's next note starts from its
 * power-on state), so the mod that offers it as a setting writes the user's
 * choice into core_voice_skip_want; nothing else writes it. A byte, read once
 * per slot per audio block, so any context may write it at any time.
 *
 * A mod that needs stock's exact render while it works (one that reads a
 * voice's machine state, or measures stock's load) calls
 * core_voice_skip_hold_add() when it starts and core_voice_skip_hold_release()
 * when it ends, once each: core_voice_skip_hold counts the holds. Each call is
 * one instruction, so any context may call them.
 *
 * Off by either path, a slot that was skipped gets its machine call again
 * from the next audio block, and its digital state still goes back to the
 * boot's at its next SYN trig.
 */
#ifndef ELEKLOADER_AR2_VOICE_H
#define ELEKLOADER_AR2_VOICE_H

#include <stdint.h>

extern volatile uint8_t core_voice_skip_want;
extern volatile uint32_t core_voice_skip_hold;
void core_voice_skip_hold_add(void);
void core_voice_skip_hold_release(void);

/* the audio blocks the skip has saved a machine call in, since boot */
extern volatile uint32_t core_voice_skips;

/* The sample-layer skip (0.16, mods/core-ar2/sample.s; README, "The
 * sample-layer skip (0.16)"): a slot whose sample layer is not playing and
 * whose filter history is zero no longer runs its resample, filter and output
 * stage. The sound is unchanged, bit for bit. Off at boot; it runs while
 * core_sample_skip_want is nonzero and core_voice_skip_hold is 0. The mod that
 * offers it as a setting writes the user's choice; nothing else writes it. A
 * byte, read once per slot per audio block, so any context may write it. */
extern volatile uint8_t core_sample_skip_want;

/* the slot-blocks the sample-layer skip has left out, since boot */
extern volatile uint32_t core_sample_skips;

#endif
