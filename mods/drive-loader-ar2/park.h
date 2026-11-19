/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Parked +Drive storage in /mods/.store, as the MODS page reads it (PAGE.md, "Parked data"):
 * which mods have parked fields, how many bits in each scope, and FORGET DATA's rewrite.
 * The file's layout is docs/DRIVE.md's, "+Drive storage" (format 1); elekloader/drive_store.py
 * is its twin (decode, forget, encode), and tests/test_drive_park.py checks these against it.
 * Pure: bytes in, bytes out, no file calls. */
#ifndef DL_PARK_H
#define DL_PARK_H

#include "dl.h"

#define DL_STORE_NAME ".store"           /* in DL_DIR */
#define DL_STORE_SCOPES 6                /* sound, kit, global, track, pattern, project */
#define DL_STORE_NAME_MAX 255            /* a mod id or a symbol, in bytes */

struct dl_park {                         /* a mod with parked data */
    char id[DL_STORE_NAME_MAX + 1];      /* its id, whole */
    u16 bits[DL_STORE_SCOPES];           /* its parked fields' bits, per scope */
    u16 fields;                          /* its parked fields */
    u16 codes, ccs;                      /* the lock codes and CCs kept for them */
};

extern const char *const dl_store_scopes[DL_STORE_SCOPES];

/* /mods/.store's bytes (after the file system's header) -> the mods with parked fields, freed
 * codes or freed CCs, in the order their first parked field was parked (then the order of their
 * codes and CCs). out == 0 only counts. -> how many (at most max with out), or -1 for a file
 * drive_store.decode doesn't take (cut short, another format, a wrong CRC, a bad record): the
 * boot then places afresh, so nothing is parked. */
int dl_park_list(const u8 *raw, u32 len, struct dl_park *out, int max);

/* FORGET DATA (drive_store.forget, then encode): raw without the parked fields, freed codes and
 * freed CCs of the mods ids[0..n), into out (len bytes: it never grows). The rest of the file,
 * the live layout ID included, stays as it is. -> the new length, or -1 for a file
 * dl_park_list refuses. Forgetting a mod with nothing parked changes nothing but leaves a valid
 * file (the same bytes). */
s32 dl_park_forget(const u8 *raw, u32 len, const char *const *ids, int n, u8 *out);

#endif
