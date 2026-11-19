/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-check: a test for core-ar2's mod folders in the +Drive browser
 * (analog-rytm-mk2/drive.h). The folder DRIVE CHECK:
 *
 *     DRIVE CHECK/     SUB/, A.WAV (1000 B), B.WAV (2 MB)
 *       SUB/           C.WAV, D.WAV, E.WAV (12 KB each), .. back up
 *
 * A folder's listing is not known until asked: list() answers -1 and starts
 * a countdown of 20 ticks (about a second), after which the listing is
 * "in" and the mod calls core_drive_refill, as a mod does when another
 * device answers. LOAD TO PROJ takes the files with an even index and
 * refuses those with an odd one.
 *
 * dc_rec (magic DRV1) counts what happened, for a reader of RAM:
 *   +4  list calls        +8  asks (a countdown started)
 *   +12 refills           +16 entry calls
 *   +20 loads taken       +24 loads refused
 *   +28 the last load's folder << 16 | index
 *   +32 bit f: folder f's listing is in
 */
#include <stdint.h>
#include "analog-rytm-mk2/drive.h"

#define FOLDERS 2
#define WAIT 20

struct dc_rec {
    uint32_t magic, lists, asks, refills, entries, taken, refused, last, in;
};
struct dc_rec dc_rec = {0x44525631u};   /* "DRV1" */
static uint8_t countdown[FOLDERS];

int32_t dc_list(uint32_t folder);
int32_t dc_entry(uint32_t folder, uint32_t i, struct core_drive_entry *e);
int32_t dc_parent(uint32_t folder);
int32_t dc_load(uint32_t folder, uint32_t i);
const struct core_drive_folder dc_folder = {"DRIVE CHECK", dc_list, dc_entry, dc_parent, dc_load};

static const struct core_drive_entry top[] = {
    {"SUB", 0, 1}, {"A.WAV", 1000, -1}, {"B.WAV", 2097152, -1}};
static const struct core_drive_entry sub[] = {
    {"C.WAV", 12288, -1}, {"D.WAV", 12288, -1}, {"E.WAV", 12288, -1}};

int32_t dc_list(uint32_t folder)
{
    dc_rec.lists++;
    if (folder >= FOLDERS)
        return 0;
    if (dc_rec.in & (1u << folder))
        return 3;
    if (!countdown[folder]) {
        countdown[folder] = WAIT;
        dc_rec.asks++;
    }
    return -1;
}

int32_t dc_entry(uint32_t folder, uint32_t i, struct core_drive_entry *e)
{
    const struct core_drive_entry *t = folder ? sub : top;
    dc_rec.entries++;
    if (folder >= FOLDERS || !(dc_rec.in & (1u << folder)) || i >= 3)
        return 1;
    *e = t[i];
    return 0;
}

int32_t dc_parent(uint32_t folder)
{
    return folder == 1 ? 0 : -1;
}

int32_t dc_load(uint32_t folder, uint32_t i)
{
    dc_rec.last = folder << 16 | i;
    if (i & 1) {
        dc_rec.refused++;
        return 0;
    }
    dc_rec.taken++;
    return 1;
}

void dc_tick(void)
{
    uint32_t f;
    for (f = 0; f < FOLDERS; f++)
        if (countdown[f] && !--countdown[f]) {
            dc_rec.in |= 1u << f;
            dc_rec.refills++;
            core_drive_refill(&dc_folder, (int32_t)f);
        }
}
