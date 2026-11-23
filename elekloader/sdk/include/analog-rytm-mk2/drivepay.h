/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-ar2 0.12: the Drive payload table, for sidecar-core (docs/DRIVE.md, "Payloads on
 * the +Drive"). A +Drive mod's .payload stays on the +Drive; the boot keeps, for each loaded
 * mod with one, an entry the calls below walk and read. Import every name weakly:
 *
 *     "weak": ["drive_payload_abi", "drive_payload_count", "drive_payload_at",
 *              "drive_payload_find", "drive_payload_info", "dl_payload_read"]
 *
 * so a build without the loader links too: drive_payload_abi then reads 0, and nothing else may
 * be called. A flashed sidecar-core and a +Drive one use the same names (the build descriptor
 * carries every flashed export, and the boot linker takes weak imports).
 *
 * At the Pi's connect, after core_payloads (a flashed mod's payloads, in RAM):
 *
 *     if (drive_payload_abi >= 1)
 *         for (i = 0; i < drive_payload_count(); i++) {
 *             const struct drive_payload *h = drive_payload_at(i);
 *             drive_payload_info(h, &id, &len, sha);    send {id, len, sha} to the Pi
 *         }
 *     then for each one the Pi lacks: dl_payload_read(h, at, buf, n) in a loop, from a task
 *
 * The entries are fixed once the boot ends. While a mod runs, its file can't be deleted or
 * moved over the /mods route or on the MODS page, and an upload over it waits for the next boot,
 * but stock's SAMPLES browser and Transfer can still change it: dl_payload_read then answers -2,
 * and the user restarts the unit. Check the sha256 of what you ship.
 */
#ifndef ELEKLOADER_AR2_DRIVEPAY_H
#define ELEKLOADER_AR2_DRIVEPAY_H

#include <stdint.h>

struct drive_payload;               /* a handle: opaque, valid until the next boot */

extern const uint32_t drive_payload_abi;   /* 1: the calls below; 0: no loader */

/* the entries, 0..count-1 */
uint32_t drive_payload_count(void);
/* entry i, or 0 past the end */
const struct drive_payload *drive_payload_at(uint32_t i);
/* the entry of payload id (FNV-1a of the mod's id, as core_payloads), or 0 */
const struct drive_payload *drive_payload_find(uint32_t id);
/* the entry's id, length in bytes and sha256 (any pointer may be 0) -> 0, or -1 a bad handle */
int32_t drive_payload_info(const struct drive_payload *h, uint32_t *id, uint32_t *len,
                           uint8_t sha256[32]);
/* n bytes of the payload from byte at into out. A task only, never an interrupt (it takes the
 * loader's lock and reads the +Drive). -> bytes read (fewer than n only at the payload's end),
 * -1 bad arguments, -2 the file changed since the boot (restart), -3 a read error or bad hex. */
int32_t dl_payload_read(const struct drive_payload *h, uint32_t at, uint8_t *out, uint32_t n);

#endif
