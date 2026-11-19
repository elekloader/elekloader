/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.15: the USB input's gain, held by a mod (docs/ADAPTING.md,
 * "USB input gain (core-ar2 0.15, Analog Rytm MKII)"; mods/core-ar2/usbin.s).
 * A mod that uses it asks for "core": "0.15".
 *
 * Stock cuts the USB input before it mixes it in, by the step AUDIO
 * ROUTING's USB TO MAIN [dB] sets. While a mod holds a step, that step is
 * used instead; the setting is not written, and is used again when the mod
 * lets go. One mod holds it at a time. The Rytm has one stereo USB input
 * pair here, so the step applies to whatever arrives on it (a DAW's audio
 * too). Only stock's own steps can be held: the loudest, CORE_USB_IN_NONE,
 * is stock's with the setting at its top.
 */
#ifndef ELEKLOADER_AR2_USBIN_H
#define ELEKLOADER_AR2_USBIN_H

#include <stdint.h>

/* the steps, 6 dB apart */
#define CORE_USB_IN_CUT18   0   /* the most cut */
#define CORE_USB_IN_CUT12   1
#define CORE_USB_IN_CUT6    2
#define CORE_USB_IN_NONE    3   /* no cut */
#define CORE_USB_IN_RELEASE (-1)

/* Hold step for who (any address of the mod's own, the same each call),
 * or with CORE_USB_IN_RELEASE let who's hold go. UI task. 0 done; -1
 * refused: another mod holds it, who is 0, or step is not a step. */
int32_t core_usb_in_gain(const void *who, int32_t step);

/* The step in force now, held or the setting's (0..3). */
int32_t core_usb_in_step(void);

/* Who holds it, 0 while nobody does. */
const void *core_usb_in_holder(void);

#endif
