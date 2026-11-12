/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Four MKII (core-a4mk2 0.7, OS 1.56): the panel. The key
 * ids std1's ev_key passes, the encoders and the UI tick's rate
 * (elekloader/sdk/include/std1.h; mods/core-a4mk2/README.md, "Standard events
 * (0.7)").
 *
 * The ids are the firmware's KeyEvent codes. They are named KEYID_*, since
 * std1.h's KEY_* are ev_key's flags (KEY_FUNC is "FUNC is held"). Where an
 * id comes from: [unit] seen on a unit with core's check mods; [read] read
 * in the firmware's key handlers; [emu] the panel map an emulator of this OS
 * uses, measured by pressing keys there, to be confirmed on a unit.
 */
#ifndef ELEKLOADER_A4_KEYS_H
#define ELEKLOADER_A4_KEYS_H

/* ---- rates ---- */
#define CORE_TICK_HZ 20                 /* ev_tick, about 20 a second, as the Rytm's */

/* ---- keys ---- */
#define KEYID_TRIG(n) (0x00 + (n))      /* trig keys 1..16 as n = 0..15 [emu] */
#define KEYID_KEYBOARD(n) (0x10 + (n))  /* the keyboard's 13 keys, n = 0..12 [emu] */
#define KEYID_TRACK(t) (0x20 + (t))     /* track keys, t a track as params.h numbers them [read] */
#define KEYID_PERF 0x30                 /* page keys [unit] (analog-four-mk2/pages.h's CORE_KEY_*) */
#define KEYID_ARP 0x31
#define KEYID_NOTE 0x32
#define KEYID_OSC1 0x33
#define KEYID_OSC2 0x34
#define KEYID_FILTERS 0x35
#define KEYID_AMP 0x36
#define KEYID_ENV 0x37
#define KEYID_LFO 0x38
#define KEYID_MIX 0xb3
#define KEYID_ARROW(n) (0x40 + (n))     /* the four arrows, n = 0..3 [emu]; which is which: to confirm */
#define KEYID_YES 0x44                  /* [emu] */
#define KEYID_NO 0x45
#define KEYID_RECORD 0x50               /* [read] in the transport handler */
#define KEYID_PLAY 0x51
#define KEYID_STOP 0x52
#define KEYID_ENC_PUSH(e) (0x70 + (e))  /* an encoder's push, e an ENC_* below [emu] */
#define KEYID_FUNC 0x80                 /* [emu] */

/* ---- encoders: std1 counts them 0 to ENC_COUNT - 1 from the left ----
 * A..J are 0..9 and LEVEL / DATA is 10 (on an Analog Keys, which runs the
 * same OS, the panel sends it as 11; core's own handlers use the OS's test).
 * ev_param_turn's knob is one of these: the encoder whose turn it is. */
#define ENC_COUNT 11
#define ENC_A 0
#define ENC_J 9
#define ENC_LEVEL 10

#endif
