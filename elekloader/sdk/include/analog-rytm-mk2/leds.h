/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Rytm MKII (core-ar2 0.17, OS 1.74): the LEDs.
 * ev_key_leds(mgr) runs after the views and stock's defaults composed their
 * LEDs and before the frame's changes go to the panel, so an LED a handler
 * sets stays over the view's. core_led_set(mgr, led, colour) sets one
 * through stock's own LED set (mgr is ev_key_leds' and is not needed). The
 * LED ids are stock's logical LEDs, 0..192, as pads.h's core_led takes them;
 * colour is the panel's palette (0 dark). Which id is which LED, and what
 * each colour shows, are to be measured on the unit. */
#ifndef ELEKLOADER_AR2_LEDS_H
#define ELEKLOADER_AR2_LEDS_H

#define LED_COUNT 193
#define LED_OFF 0u
void core_led_set(void *mgr, unsigned led, unsigned colour);

#endif
