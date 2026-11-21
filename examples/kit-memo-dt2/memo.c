/* SPDX-License-Identifier: GPL-2.0-or-later */
/* kit-memo: the smallest mod on core-dt2 1.1's kit data, project data and
 * value rows (docs/ADAPTING.md, "Project data, kit data and value rows
 * (core-dt2 1.1, Digitakt II)"). Each kit keeps a memo, OFF, A, B or C, set in SETTINGS >
 * PERSONALIZE > KIT MEMO for the kit on: it is saved with the kit, comes back
 * when the kit is loaded, and goes with the kit when the pattern is copied.
 * The project keeps the last memo set. */
#include "digitakt-mk2/core.h"

static uint32_t memos[CORE_KITS];   /* kit data: one long a kit */
static uint32_t last;               /* project data */

const core_kitdata_t memo_kitdata = {{'M', 'E', 'M', 'O'}, sizeof memos[0], memos, 0};
const core_projdata_t memo_projdata = {{'M', 'E', 'M', 'L'}, sizeof last, &last, 0};

static const char *const names[] = {"OFF", "A", "B", "C"};

static uint32_t get(void)
{
    int kit = core_kit_on();
    return kit >= 0 && memos[kit] < 4 ? memos[kit] : 0;
}

static void set(uint32_t v)
{
    int kit = core_kit_on();
    if (kit >= 0)
        memos[kit] = last = v;
}

static const core_value_t row = {"KIT MEMO", names, 4, get, set};

void memo_personalize(void *menu)
{
    core_addvalue(menu, &row);
}
