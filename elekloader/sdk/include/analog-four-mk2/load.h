/* SPDX-License-Identifier: GPL-2.0-or-later
 * core-a4 0.2: the audio interrupt's load (mods/core-ar2/load.s, which core-a4 builds).
 *
 * The last audio block's time, from core's ev_render_in (order 0) to its
 * ev_render_out (order 100), as a share of the time between two blocks, in
 * 256ths: 256 is the whole block. The interrupt's own entry and exit are
 * outside it, so the real share is a little higher. 0 until two blocks have
 * run. On the A4 the stock figure is not yet measured.
 *
 * Optional work in the interrupt, work that does not change the sound,
 * stands aside while it is high:
 *
 *     if (core_audio_load > 243) return;     // 95%
 */
#ifndef ELEKLOADER_A4_LOAD_H
#define ELEKLOADER_A4_LOAD_H

#include <stdint.h>

extern volatile uint32_t core_audio_load;

#endif
