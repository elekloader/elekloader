/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: the mods' payloads (elekloader's .payload section).
 *
 * A mod's .payload is data it ships with its code: the linker places it in
 * the mods' area after the code, the boot copier copies it there with the
 * code, and the table core_payloads lists each mod's, {id, address, length}.
 * The id is the FNV-1a hash (32 bits) of the mod's id (link.py payload_id).
 * From 0.11 the mods' area is 0x47b10000..0x47fff000 (the profile's
 * core_ddr), above the step tag tables core keeps at fixed
 * addresses (steps.c). core-a4mk2 0.6 builds this file too, with the A4's
 * area from 0.6, 0x45650000..0x47c00000.
 */

#include <stdint.h>

struct core_payload_entry {
    uint32_t id;
    const void *addr;
    uint32_t len;
};

extern const struct core_payload_entry core_payloads[];   /* ends with a zero entry */

/* the payload with this id: its address, and its length in *len (if len is
 * not 0); 0 and *len = 0 when no mod in the build carries one */
const void *core_payload(uint32_t id, uint32_t *len)
{
    const struct core_payload_entry *e;
    for (e = core_payloads; e->addr; e++)
        if (e->id == id) {
            if (len)
                *len = e->len;
            return e->addr;
        }
    if (len)
        *len = 0;
    return 0;
}

/* the id core_payload takes for a mod's id */
uint32_t core_payload_id(const char *mod_id)
{
    uint32_t h = 0x811c9dc5u;
    while (*mod_id)
        h = (h ^ (unsigned char)*mod_id++) * 0x01000193u;
    return h;
}
