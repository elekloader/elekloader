/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitakt-mk1/core3.h"

/* Extra value k (0-6) is sound slot 46 + k, parameter id 164 + k. */
#define MP_EXTRA_SLOT(k) (46 + (k))
#define MP_EXTRA_ID      164
#define MP_EXTRA_END     (MP_EXTRA_ID + CM_EXTRAS)   /* 171: the first id past them */

const struct cm_ui32 *mp_src_for(int32_t machine);
uint32_t mp_ui32(int32_t machine);
void mp_src_init(void);
int mp_stock_sound_parameter(uint32_t id);

/* The SDK alpha's machines (sdk_alpha.c, OS 1.54 only). Without it no machine is an SDK machine:
 * sdk_alpha_find and the lookups below are 0, and the compiler drops every SDK branch. */
#include "../sdk-alpha/machine.h"
const struct cm_ui32 *sdk_alpha_ui(int32_t id);
int32_t sdk_alpha_snapshot(const struct sdk_alpha_entry *, int32_t page, void *view);
int32_t sdk_alpha_value_text(int32_t machine, int32_t knob, int32_t raw, char *buf, int popup);
void sdk_alpha_render(int32_t *blocks);
void sdk_alpha_redraw(void *view);     /* sdk_alpha.s */
#ifndef SDK_ALPHA_DT1
#define sdk_alpha_find(id) ((const struct sdk_alpha_entry *)0)
#define sdk_alpha_ui(id) ((const struct cm_ui32 *)0)
#define sdk_alpha_value_text(machine, knob, raw, buf, popup) 0
#endif
