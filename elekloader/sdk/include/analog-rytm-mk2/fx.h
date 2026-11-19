/* SPDX-License-Identifier: GPL-2.0-or-later
 * core-ar2 0.16: the FX skip's switches (mods/core-ar2/fx.c, fx.s,
 * mods/core-ar2/SKIPS.md, "The FX skip (0.16)").
 *
 * All three are off at boot. The mod that offers them as settings writes the
 * user's choice; nothing else writes them. Bytes, read once per audio block,
 * so any context may write them at any time.
 *
 * core_fx_skip_want: the send trim and the delay skip. Exact: the output stays
 * stock's, bit for bit. The send loop leaves out voice slots that send
 * nothing, and the delay is not called while nothing feeds it and its line
 * has held only 0 for a whole turn (about 11 s).
 *
 * core_reverb_clear_want: with core_fx_skip_want, the reverb clear and skip.
 * It changes the sound slightly: once the reverb's input has been 0 for 1 s
 * and its output is below 1 LSB of 24 bits, core clears its state and lines
 * and skips it until its input moves, so its next tail starts from silence,
 * as after a power-on, and the 1-LSB offset it leaves on main L goes.
 *
 * core_delay_clear_want: with core_fx_skip_want, the delay clear. It changes
 * the sound slightly: stock's delay, once fed, never decays to exactly 0 either,
 * so without it the delay skip ends the first time the delay is used. Once
 * nothing feeds the delay and nothing at or above 4 LSB of 24 bits has gone
 * into its line or out of it for a whole turn of the line (about 11 s), core
 * clears the line and the delay's audio state (about 0.35 s, without calling
 * it), and the delay skip takes over. Its next echoes start from silence.
 *
 * A mod that needs stock's exact FX render while it works calls
 * core_fx_skip_hold_add() when it starts and core_fx_skip_hold_release() when
 * it ends, once each: core_fx_skip_hold counts the holds and holds every part
 * off. Each call is one instruction, so any context may call them.
 */
#ifndef ELEKLOADER_AR2_FX_H
#define ELEKLOADER_AR2_FX_H

#include <stdint.h>

extern volatile uint8_t core_fx_skip_want;
extern volatile uint8_t core_reverb_clear_want;
extern volatile uint8_t core_delay_clear_want;
extern volatile uint32_t core_fx_skip_hold;
void core_fx_skip_hold_add(void);
void core_fx_skip_hold_release(void);

/* the audio blocks the skip has left a delay or reverb call out of, since boot */
extern volatile uint32_t core_fx_skips;

#endif
