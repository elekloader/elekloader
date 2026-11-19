/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Rytm MKII (core-ar2 0.17, OS 1.74): tracks and
 * parameters, as std1's events number them (elekloader/sdk/include/std1.h).
 *
 * Tracks count from 0, the 12 drum tracks first (BD .. CB), then the FX
 * track as 12, as core's own calls number them. The Rytm has 8 voice slots
 * for its 12 tracks, so a voice is not a track. */
#ifndef ELEKLOADER_AR2_PARAMS_H
#define ELEKLOADER_AR2_PARAMS_H

#define TRACK_COUNT 13
#define TRACK_DRUM(n) (n)               /* drum tracks 1..12 as n = 0..11 */
#define TRACK_FX 12

/* Parameter ids are the firmware's, the ones its parameter pages use
 * (pages.h): stock's are 0..PARAM_STOCK_LAST, core's lockable parameters
 * follow them, and a mod's own come from CORE_PARAM_LOCK_FIRST (555).
 * std1's shared parameter names are not settled yet, so none is named here. */
#define PARAM_STOCK_LAST 468

#endif
