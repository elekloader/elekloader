/* SPDX-License-Identifier: GPL-2.0-or-later */
/* machines-dn2's own declarations, shared by machines.c and sdk_alpha.c. Not for mods. */
#ifndef MACHINES_DN2_H
#define MACHINES_DN2_H
#include <stdint.h>

#define FIRST 5
#define COUNT 128                  /* synth IDs 5..127; SHARC voices keep 5..31 (the linker's claims) */
struct descriptor { const char *name, *label; uint32_t params[9]; };
struct string_rep { uint32_t length, capacity; int32_t references; char text[32]; };
#define ROWS 321
#define SLOT 25
#define PAGE_END 10                 /* Track Level ends a stock page */
#define SDK_ROW 41                  /* the added rows SDK machines share, after the five native pages and G */
#define SDK_KNOBS 15

/* machines.c */
const char *dn2_representation(struct string_rep *r, const char *s, unsigned max);
void dn2_add_row(uint32_t code, int32_t slot, const char *label, uint32_t def, const char *owner_name);
void dn2_row_label(uint32_t code, const char *label, const char *owner_name);
void dn2_row_format(uint32_t code, void (*format)(int32_t value, char *buf));
void dn2_ensure(void);
void dn2_rows_follow(void);

/* sdk_alpha.c */
struct mk2_synth;
const struct mk2_synth *dn2_sdk_synth(unsigned i);
void dn2_sdk_build(void);
uint32_t dn2_sdk_page_count(uint32_t type);
const struct descriptor *dn2_sdk_page(uint32_t type, uint32_t page);
uint32_t dn2_sdk_code(uint32_t type, uint32_t slot);
uint32_t dn2_sdk_renders(uint32_t type);
int32_t dn2_sdk_default(uint32_t knob);
uint32_t dn2_active_type(uint32_t filter);
#endif
