/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Rytm MKII (core-ar2 0.17, OS 1.74): the panel. The key
 * ids std1's ev_key passes, the encoders and the UI tick's rate
 * (elekloader/sdk/include/std1.h; mods/core-ar2/README.md, "Standard events
 * (0.17)").
 *
 * The ids are the firmware's KeyEvent codes. They are named KEYID_*, since
 * std1.h's KEY_* are ev_key's flags. Where an id comes from: [unit] seen on
 * a unit with a mod that reads it; [read] read in the firmware's key
 * handlers. Only the ids core and its mods already rely on are named here;
 * the rest come with the std1 hardware run.
 */
#ifndef ELEKLOADER_AR2_KEYS_H
#define ELEKLOADER_AR2_KEYS_H

/* ---- rates ---- */
#define CORE_TICK_HZ 20                 /* ev_tick, about 20 a second (the Digitakt's 30) */

/* ---- keys ---- */
#define KEYID_SMPL 0x32                 /* [unit] smpl-machines */
#define KEYID_NO 0x45                   /* [read] the Drive Loader's skip key */
#define KEYID_FUNC 0x80                 /* [read] pads.c */
#define KEYID_TRK 0x86                  /* [unit] smpl-machines */
#define KEYID_PAD(n) (0x90 + (n))       /* the 12 pads, n = 0 BD .. 11 CB [read] (pads.h) */

/* ---- encoders: std1 counts them 0 to ENC_COUNT - 1 from the left ----
 * A..H are 0..7 and LEVEL / DATA is 8, as the firmware's EncoderEvent
 * numbers them (screen.c). ev_enc's enc and ev_param_turn's knob are one of
 * these. */
#define ENC_COUNT 9
#define ENC_A 0
#define ENC_H 7
#define ENC_LEVEL 8

#endif
