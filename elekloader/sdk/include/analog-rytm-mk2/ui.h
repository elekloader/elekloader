/* SPDX-License-Identifier: GPL-2.0-or-later
 * core-ar2 0.16: the UI skip's switch (mods/core-ar2/cast.s,
 * mods/core-ar2/SKIPS.md, "The UI skip (0.16)").
 *
 * The skip runs only when a mod asks for it and no mod holds it off:
 * core_ui_skip_want nonzero and core_ui_skip_hold 0. It is off at boot.
 * It changes nothing the unit does or shows, only the CPU the UI takes, so
 * the mod that offers it as a setting writes the user's choice into
 * core_ui_skip_want; nothing else writes it. A byte, read once per cast, so
 * any context may write it at any time.
 *
 * A mod that measures stock's UI load calls core_ui_skip_hold_add() when it
 * starts and core_ui_skip_hold_release() when it ends, once each. Each call
 * is one instruction, so any context may call them.
 */
#ifndef ELEKLOADER_AR2_UI_H
#define ELEKLOADER_AR2_UI_H

#include <stdint.h>

extern volatile uint8_t core_ui_skip_want;
extern volatile uint32_t core_ui_skip_hold;
void core_ui_skip_hold_add(void);
void core_ui_skip_hold_release(void);

/* casts answered from the memo, and casts that ran stock's search, since
 * boot (counted only while the skip is on) */
extern volatile uint32_t core_ui_skip_hits;
extern volatile uint32_t core_ui_skip_misses;

#endif
