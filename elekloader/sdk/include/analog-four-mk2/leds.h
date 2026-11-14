/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Four MKII (core-a4mk2 0.7, OS 1.56): the LEDs.
 * ev_key_leds(mgr) runs after the views composed their LEDs and before the
 * frame goes to the panel, so an LED a handler sets stays over the view's.
 * core_led_set(mgr, led, colour) sets one through the LedManager's own set.
 * The LED ids are the LedManager's: stock's own keys use 0x20..0x25 (the
 * track keys) and 0x30..0x38 (the page keys) [read]; the rest, and what each
 * colour value shows, are to be measured. */
#ifndef ELEKLOADER_A4_LEDS_H
#define ELEKLOADER_A4_LEDS_H

#define LED_OFF 0u
#define LED_ON 15u                      /* colour 15, the value stock passes with its off */
void core_led_set(void *mgr, unsigned led, unsigned colour);

#endif
