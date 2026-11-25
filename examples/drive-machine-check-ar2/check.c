/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-machine-check-ar2: core-ar2 0.22's +Drive machines (machines.c,
 * core_boot_drive_machines), without a +Drive. The Drive Loader merges a
 * +Drive mod's core_machines entry into one of the table's spare entries
 * in ev_boot_drive, after core's .boot has walked the table; this mod does
 * the same from its own ev_boot_drive handler, so a build of core and this
 * mod alone shows whether such an entry reaches the machine table.
 *
 * It imports core_desc, so the build has the descriptor and with it the
 * spare entries (docs/DRIVE.md, "The build descriptor").
 *
 * The record (dmc, after the magic DMC1), longs:
 *   0 the magic
 *   1 1: the entry went into a spare entry; -1: no spare entry; -2: slot
 *     37 already taken by a flashed entry
 *   2 the slot's name pointer (0x401bf254 + 8 * 37) at the end of the
 *     handler (before core walks the table again)
 *   3..6 at the first tick: the name pointer, the label pointer, the home
 *     track and voice mask (bytes 0x401bf42c + 2 * 37, as home << 8 | mask),
 *     the engine (0x402906dc + 4 * 37)
 *   7 1 when long 3 is DRV CHECK's name, 2 when long 6 is not the stock
 *     engine either (3: both: the entry reached the table)
 *   8 DRV CHECK's engine calls
 *   9 the record id of slot 37's knob B (sound index 10) with its name, 0
 *     when no record of slot 37 names "Tune Check" (the records' write)
 *   10 ticks
 */
#include <stdint.h>
#include "analog-rytm-mk2/machines.h"

#define SLOT 37
#define NAMES ((const char *const *)0x401bf254)
#define AVAIL ((const volatile uint8_t *)0x401bf42c)
#define ENGINES ((const core_engine *)0x402906dc)
#define RECORDS ((const uint8_t *)0x401bfbc8)
#define RECORD_SIZE 0x34
#define RECORD_COUNT 469
#define L(p, o) (*(const volatile int32_t *)((p) + (o)))

extern const struct core_machine *core_machines[];
extern const uint8_t core_desc[];

volatile int32_t dmc[11] = {0x444d4331};   /* "DMC1" */
static core_engine stock_engine;

static void dmc_engine(int32_t voice, void *frame, uint16_t *blk, int32_t pitch)
{
    dmc[8]++;
    CORE_ENGINE_BD_CLASSIC(voice, frame, blk, pitch);
}

static const char name[] = "DRV CHECK";
static const char tune[] = "Tune Check";

static const struct core_machine machine = {
    SLOT, dmc_engine, 0, CORE_VOICE_BD, 0, name, "DRVC",
    {
        {"Level", "LEV", 0, 127, 100, 0},
        {tune, "TUN", 32, 96, 58, 1},
        {"Decay Time", "DEC", 0, 127, 32, 0},
        {"Hold Time", "HLD", 0, 127, 32, 0},
        {"Sweep Time", "SWT", 0, 127, 64, 0},
        {"Sweep Depth", "SWD", 0, 127, 42, 0},
        {"Waveform", "WAV", 0, 2, 1, 0},
        {"Transient", "TRA", 0, 55, 17, 0},
    },
};

void dmc_boot_drive(void)
{
    const struct core_machine **e = core_machines;
    (void)core_desc[0];
    stock_engine = ENGINES[SLOT];
    for (; *e; e++)
        if ((*e)->slot == SLOT) {
            dmc[1] = -2;
            return;
        }
    /* the zero entry ends the table; spare zero entries follow it, so
     * the one after must be zero too for this to be a spare one */
    if (e[1]) {
        dmc[1] = -1;
        return;
    }
    *e = &machine;
    dmc[1] = 1;
    dmc[2] = (int32_t)NAMES[2 * SLOT];
}

void dmc_tick(void *ctrl)
{
    (void)ctrl;
    if (!dmc[10]++) {
        dmc[3] = (int32_t)NAMES[2 * SLOT];
        dmc[4] = (int32_t)NAMES[2 * SLOT + 1];
        dmc[5] = (int32_t)AVAIL[2 * SLOT] << 8 | AVAIL[2 * SLOT + 1];
        dmc[6] = (int32_t)ENGINES[SLOT];
        dmc[7] = (dmc[3] == (int32_t)name) | (dmc[6] != (int32_t)stock_engine) << 1;
        for (int id = 0; id < RECORD_COUNT; id++) {
            const uint8_t *r = RECORDS + RECORD_SIZE * id;
            if (L(r, 0x00) == SLOT && L(r, 0x04) == 10 && L(r, 0x28) == (int32_t)tune)
                dmc[9] = id;
        }
    }
}
