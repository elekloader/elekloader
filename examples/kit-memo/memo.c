/* SPDX-License-Identifier: GPL-2.0-or-later */
/* kit-memo: the smallest mod on core-dt2 1.1 and core-dn2 0.3's kit data,
 * project data and value rows (docs/ADAPTING.md, "Frames, LEDs, project and kit
 * data, value rows"). Each kit keeps a memo, OFF, A, B or C, set in SETTINGS >
 * PERSONALIZE > KIT MEMO for the kit on: it is saved with the kit, comes back
 * when the kit is loaded, and goes with the kit when the pattern is copied. The
 * project keeps the last memo set. The header says the kit's memo when a kit
 * with one comes on. */
#include "mk2/core.h"

static uint32_t memos[CORE_KITS];   /* kit data: one long a kit */
static uint32_t last;               /* project data */
static int shown = -1;

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

void memo_tick(void *ctrl)
{
    int kit = core_kit_on();
    (void)ctrl;
    if (kit != shown) {
        shown = kit;
        if (kit >= 0 && memos[kit] && memos[kit] < 4)
            core_notice(names[memos[kit]]);
    }
}
