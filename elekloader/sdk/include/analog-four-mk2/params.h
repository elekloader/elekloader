/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Four MKII (core-a4mk2 0.7, OS 1.56): tracks and
 * parameters, as std1's events number them (elekloader/sdk/include/std1.h).
 *
 * Tracks count from 0, the synth tracks first, then the FX and CV tracks
 * (decided 2026-11-12). The CV track makes no audio. */
#ifndef ELEKLOADER_A4_PARAMS_H
#define ELEKLOADER_A4_PARAMS_H

#define TRACK_COUNT 6
#define TRACK_SYNTH(n) (n)              /* synth tracks 1..4 as n = 0..3 */
#define TRACK_FX 4
#define TRACK_CV 5

/* Parameter ids are the firmware's, the ones its parameter pages use
 * (page_info's ids A..J; analog-four-mk2/pages.h): stock's are
 * 0..PARAM_STOCK_LAST, and a mod's own (lock.h) come after them. std1's
 * shared parameter names are not settled yet, so none is named here. */
#define PARAM_STOCK_LAST 0x126

#endif
