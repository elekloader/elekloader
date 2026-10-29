/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DNM_NATIVE_H
#define DNM_NATIVE_H
#include "digitone-mk1/machines.h"

extern const struct dnm_machine *dnm_active(void);
extern const struct dnm_page *const dn_machine_pages[];
static inline uint32_t dnm_read32(const void *p)
{
    return *(volatile const uint32_t *)p;
}

static inline void *dnm_ptr(uint32_t p)
{
    return (void *)(uintptr_t)p;
}

extern int32_t dnm_knob_slot(int32_t knob);
extern int32_t dnm_reset(void *brain, int32_t syn, int32_t knob);
#endif
