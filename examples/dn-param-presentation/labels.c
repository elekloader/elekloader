/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stddef.h>
#include "digitone-mk1/core3.h"
static const char *name(int32_t id, int32_t field)
{
    if (id != (int32_t)fw_slot_ids[66]) return 0;
    if (field == CORE_PARAM_LONG) return "Attack";
    if (field == CORE_PARAM_SHORT) return "ATK";
    return 0;
}
static int32_t format(int32_t id, int32_t value, char text[CORE_PARAM_TEXT])
{
    uint32_t v;
    char *p = text;
    if (id != (int32_t)fw_slot_ids[66]) return 0;
    v = (uint32_t)value >> 8;
    if (v > 999) v = 999;
    if (v >= 100) *p++ = '0' + v / 100;
    if (v >= 10) *p++ = '0' + v / 10 % 10;
    *p++ = '0' + v % 10; *p = 0;
    return 1;
}
static int32_t visible(int32_t id)
{ return id == (int32_t)fw_slot_ids[66] ? 1 : -1; }
const struct core_param_presenter presentation = {&core_param_present_v2, name, format, visible};
typedef char override_abi[(sizeof(struct core_param_override) == 8) ? 1 : -1];
typedef char presenter_abi[(sizeof(struct core_param_presenter) == 16) ? 1 : -1];
typedef char presenter_fields[(offsetof(struct core_param_presenter, name) == 4 &&
    offsetof(struct core_param_presenter, format) == 8 &&
    offsetof(struct core_param_presenter, visible) == 12) ? 1 : -1];
