/* SPDX-License-Identifier: GPL-2.0-or-later
 * core-ar2 0.4: the audio interrupt's load (mods/core-ar2/load.s).
 *
 * The last audio block's time, from core's ev_render_in (order 0) to its
 * ev_render_out (order 100), as a share of the time between two blocks, in
 * 256ths: 256 is the whole block. The interrupt's own entry and exit are
 * outside it, so the real share is a little higher. 0 until two blocks have
 * run. Stock alone peaks near 215 (84%) while a pattern plays.
 *
 * Optional work in the interrupt, work that does not change the sound,
 * stands aside while it is high:
 *
 *     if (core_audio_load > 243) return;     // 95%
 */
#ifndef ELEKLOADER_AR2_LOAD_H
#define ELEKLOADER_AR2_LOAD_H

#include <stdint.h>

extern volatile uint32_t core_audio_load;

/* core-ar2 0.9: the audio blocks rendered since boot, from 1 at the first
 * (core's ev_render_in counts them, before any mod's) */
extern volatile uint32_t core_block;

#endif
