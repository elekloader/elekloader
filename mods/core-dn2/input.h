/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CORE_DN2_INPUT_H
#define CORE_DN2_INPUT_H

enum { CORE_KEYS_GROUPS = 7 };
enum { CORE_KEYS_OK, CORE_KEYS_INVALID, CORE_KEYS_BUSY, CORE_KEYS_OVERLAP };

/* UI task only; merges synthetic masks with the physical panel state. */
int core_keys_set(unsigned group, unsigned mask);
int core_keys_clear(void);
void core_panel_buttons(unsigned group, unsigned physical_mask);

#endif
