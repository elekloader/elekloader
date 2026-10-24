/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Three settings for tests of settings-ot: a binary and an option on the card, a number with the
 * project. `sts` records them and how often each changed(), at a fixed place for the dumps. */
#include <stdint.h>
#include "octatrack/core.h"

volatile int32_t sts_flag = 1, sts_mode = 0, sts_level = 64;
volatile uint32_t sts_changes[3];

static void changed(const struct ot_setting *s) { ++sts_changes[s->key - 1]; }

static const char *const modes[] = { "SLOW", "MID", "FAST" };

const struct ot_setting sts_s1 = { OT_SETTING_ABI, OT_TAG('S', 'T', 'S', '1'), 1, OT_SET_BINARY, OT_SET_CARD,
                                   "PROBE", "FLAG", 0, 1, 0, &sts_flag, 1, changed };
const struct ot_setting sts_s2 = { OT_SETTING_ABI, OT_TAG('S', 'T', 'S', '1'), 2, OT_SET_OPTION, OT_SET_CARD,
                                   "PROBE", "MODE", 0, 2, modes, &sts_mode, 0, changed };
const struct ot_setting sts_s3 = { OT_SETTING_ABI, OT_TAG('S', 'T', 'S', '1'), 3, OT_SET_NUMBER, OT_SET_PROJECT,
                                   "PROBE", "LEVEL", 0, 127, 0, &sts_level, 64, changed };
