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

#endif
