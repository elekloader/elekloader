/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.6: payloads (docs/ADAPTING.md, "Payloads (core-a4mk2 0.6,
 * Analog Four MKII)"), core-ar2 0.11's. A mod ships data in a .payload
 * section, in assembly for example
 *
 *         .section .payload, "a"
 *         .globl  my_blob
 *     my_blob:
 *         .incbin "blob.bin"
 *
 * The linker places it in the mods' area after the code, the boot copier
 * copies it there with the code, and its own symbols (my_blob) reach it as
 * any other. Another mod finds it by the mod's id.
 */
#ifndef ELEKLOADER_A4_PAYLOAD_H
#define ELEKLOADER_A4_PAYLOAD_H

#include <stdint.h>

/* the payload of the mod whose id hashes to `id` (core_payload_id): its
 * address, and its length in *len if len is not 0; 0 (and *len = 0) when
 * the build has none with that id. Any task or interrupt. */
const void *core_payload(uint32_t id, uint32_t *len);

/* the id of a mod's payload: the FNV-1a hash (32 bits) of its id */
uint32_t core_payload_id(const char *mod_id);

/* the table itself, for a mod that walks every payload: {id, address,
 * length} entries, ending with a zero entry */
struct core_payload_entry {
    uint32_t id;
    const void *addr;
    uint32_t len;
};
extern const struct core_payload_entry core_payloads[];

#endif
