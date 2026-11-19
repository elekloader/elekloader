/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.15: pad strikes before stock plays them (ev_pad) and pad and key
 * LEDs from mods (ev_leds, core_led) (docs/ADAPTING.md, "Pads and LEDs
 * (core-ar2 0.15, Analog Rytm MKII)"). A mod that uses them asks for
 * "core": "0.15". Subscribe in mod.json,
 * {"event": "ev_pad", "fn": "my_pad"}. Everything here runs in the UI task.
 */
#ifndef ELEKLOADER_AR2_PADS_H
#define ELEKLOADER_AR2_PADS_H

#include <stdint.h>

/* ---- ev_pad ----
 * int32_t f(int32_t pad, int32_t value, int32_t flags), UI task, for each
 * of the 12 pads' (0 BD .. 11 CB) strikes, releases and pressure, before
 * stock's view sees them: stock plays the pad's sound, sends its note,
 * records it live and selects its track there.
 *   flags CORE_PAD_DOWN      a strike; value the velocity 1..127. Return
 *                            nonzero to take it: stock never sees this
 *                            touch of the pad (no sound, no note, no track
 *                            select), and no later handler is asked
 *         CORE_PAD_UP        its release; value 0. Every handler is told
 *         CORE_PAD_PRESSURE  pressure while it is held; value 0..32640
 *                            (the panel's 0..255 << 7). Every handler is
 *                            told; nonzero takes this one from stock
 *         | CORE_PAD_FUNC    FUNC is held
 * A touch whose strike a mod took is the mod's to its release: stock gets
 * neither its pressure nor its release, and one whose strike stock got
 * keeps its release, whatever the handlers answer. The pad's own key event
 * (key ids 0x90..0x9b) still comes to ev_key as before. */
#define CORE_PAD_DOWN 1
#define CORE_PAD_FUNC 2
#define CORE_PAD_UP 0x10
#define CORE_PAD_PRESSURE 0x20
#define CORE_PADS 12
/* the pads whose strike a mod holds now, bit p = pad p */
uint32_t core_pads_taken(void);

/* ---- LEDs ----
 * void f(void), UI task, every UI frame (from ev_tick, after every mod's
 * ev_tick): call core_led for each LED you want this frame. A LED no
 * handler sets in a frame goes back to stock, as stock last left it.
 * core_led(led, colour): colour 0..255, an index in the panel's palette
 * (0 dark), shown whatever stock does with that LED meanwhile (on, off,
 * blinking, glowing, the track's colour); CORE_LED_STOCK leaves it to stock.
 * led is stock's own LED number: trig keys 0..15 (CORE_LED_TRIG), the pads
 * 16..27 (CORE_LED_PAD); the other keys' numbers are stock's (0..192).
 * Returns 0, or -1 for a number with no LED. */
#define CORE_LED_STOCK (-1)
#define CORE_LED_TRIG(k) (k)
#define CORE_LED_PAD(p) (16 + (p))
int core_led(int led, int colour);

#endif
