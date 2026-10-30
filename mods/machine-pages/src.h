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

#ifdef SDK_ALPHA_DT1
#include "elekloader/sdk_alpha_machine.h"
const struct elek_machine *sdk_alpha_machine(int32_t id);
const struct cm_ui32 *sdk_alpha_ui(int32_t id);
void sdk_alpha_snapshot(const struct elek_machine *, int32_t page, void *view);
int32_t sdk_alpha_value_text(int32_t machine, int32_t knob, int32_t raw, char *buf, int popup);
#endif
