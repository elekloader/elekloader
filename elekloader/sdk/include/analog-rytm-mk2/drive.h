/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.14: mod folders in the +Drive file browser (docs/ADAPTING.md, "Mod
 * folders in the +Drive browser (core-ar2, Analog Rytm MKII)"). A mod lists
 * a folder of its own after the +Drive's root entries in SETTINGS > SAMPLES
 * and the sample slot browser, browsed with the browser's own keys:
 *
 *     static const struct core_drive_folder my_folder = {
 *         "SIDECAR", my_list, my_entry, my_parent, my_load};
 *
 *     "contribute": [
 *       {"to": "core_drive_folders", "order": 50, "data": "00000000",
 *        "relocs": [[0, "abs32", "sym:my_folder", 0]]}],
 *     "resources": {"core": "0.14"}
 *
 * Core owns every stock filesystem site this needs, and keeps a mod folder
 * read-only: no CREATE DIR, RENAME, DELETE, MOVE or UPLOAD in it, no path
 * for its entries, no real root entry with its name (a mkdir, file create,
 * rename or move of one is refused), and none of its files reaches stock's
 * sample loader. Real folders and files pass through unchanged.
 *
 * Folders are numbered by the mod: 0 is the mod's folder itself, the others
 * 1..1022 as the mod likes. A folder's entries are 0..n-1, subfolders and
 * files in the order the mod gives them; a file is named by its folder and
 * its index there (at most 1023 entries a folder). Each call runs in the UI
 * task, while the browser reads the folder, and must not wait:
 *
 *   list(folder)        the number of entries known for folder now, or -1
 *                       none yet. Called at each read of the folder, and for
 *                       folder 0 when the root is listed, so a mod can ask
 *                       its source for them early. When they arrive later,
 *                       call core_drive_refill.
 *   entry(folder, i, e) entry i: e->name, e->size (bytes, files) and
 *                       e->folder (a subfolder's number, or -1 for a file).
 *                       0 given, nonzero none.
 *   parent(folder)      the folder ".." goes to; -1 the +Drive's root.
 *   load(folder, i)     LOAD TO PROJ on file i: nonzero taken (it counts in
 *                       stock's "N SAMPLES LOADED"; nothing goes into the
 *                       Rytm's sample memory), 0 refused. May be 0 (none).
 *
 * At most 8 mod folders show; the name is 1..23 characters, no '/', and
 * never "mods" (the Drive Loader's) or the name of a real entry in the root
 * (that real one shows, the mod's doesn't). The folders show only in the
 * browser, not to the +Drive's USB transfer or other readers.
 */
#ifndef ELEKLOADER_AR2_DRIVE_H
#define ELEKLOADER_AR2_DRIVE_H

#include <stdint.h>

struct core_drive_entry {
    const char *name;       /* shown as given, up to 63 characters */
    uint32_t size;          /* a file's size in bytes */
    int32_t folder;         /* a subfolder's number (1..1022), or -1: a file */
};

struct core_drive_folder {
    const char *name;
    int32_t (*list)(uint32_t folder);
    int32_t (*entry)(uint32_t folder, uint32_t i, struct core_drive_entry *e);
    int32_t (*parent)(uint32_t folder);
    int32_t (*load)(uint32_t folder, uint32_t i);
};

/* folder's entries changed (or arrived): a browser showing it reads it again
 * at its next UI tick, as stock does after a CREATE DIR. folder -1: whichever
 * of the mod's folders is open. Any task or interrupt; never waits. Call it
 * when a listing is complete, not for each part of it. */
void core_drive_refill(const struct core_drive_folder *f, int32_t folder);

#endif
