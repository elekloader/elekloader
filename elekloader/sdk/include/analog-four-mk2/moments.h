/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2: core-ar2 0.11's moments on the Analog Four MKII (README,
 * "Moments"). Events are subscribed in mod.json; calls are core's
 * exports.
 */
#ifndef ELEKLOADER_A4MK2_MOMENTS_H
#define ELEKLOADER_A4MK2_MOMENTS_H

#include <stdint.h>

/* the live project's name: 16 bytes, not always zero-terminated. Any task. */
const char *core_project_name(void);

/* every handle field's value in the live project ("handle": true in a
 * storage field's declaration; 0 is none): fn(field, item, index, value)
 * for each item whose handle is not 0, with core_store_get's item and index:
 * the 128 kits as edited, then as last saved (each with its 4 sounds), the
 * 4 global slots, the 128 patterns and their 6 tracks, the project. UI
 * task. */
void core_handle_fields(void (*fn)(unsigned field, void *item, int index, unsigned value));

#endif
