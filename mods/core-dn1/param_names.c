/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitone-mk1/core3.h"
#ifdef CORE_NATIVE_UI
extern const struct core_param_override *const core_param_override[];
extern const struct core_param_presenter *const core_param_presenters[];
const uint32_t core_param_present_v2 = 2;
static char value_text[CORE_PARAM_TEXT];
/* Existing core_param_override callbacks return pointer bits in D0, as
 * core_param_short consumes them. GCC pointer calls would instead read A0. */
extern uint32_t core_param_name_call(uint32_t callback, int32_t id, int32_t field);

static const char *name_override(int32_t id, int32_t field)
{
    const struct core_param_presenter *const *provider;
    for (provider = core_param_presenters; *provider; provider++) {
        const struct core_param_presenter *p = *provider;
        const char *text;
        if (p->abi == &core_param_present_v2 && p->name &&
            (text = p->name(id, field))) return text;
    }
    /* The original name callback only accepts SHORT, even when it ignores field. */
    if (field != CORE_PARAM_SHORT) return 0;
    const struct core_param_override *const *p;
    for (p = core_param_override; *p; p++) {
        const char *text;
        if ((*p)->name && (text = (void *)(uintptr_t)core_param_name_call(
                (uint32_t)(uintptr_t)(*p)->name, id, field))) return text;
    }
    return 0;
}

uint32_t core_param_name(int32_t id, int32_t field)
{
    const char *text;
    if (id < 0 || id >= 185) id = 0;
    if (field != CORE_PARAM_LONG && field != CORE_PARAM_GROUP && field != CORE_PARAM_SHORT) return 0;
    text = name_override(id, field);
    return text ? (uint32_t)(uintptr_t)text : *(const uint32_t *)(fw_params + 60 * id + field);
}

uint32_t core_param_value_text(int32_t id, int32_t value)
{
    const struct core_param_presenter *const *p;
    for (p = core_param_presenters; *p; p++) {
        if ((*p)->abi != &core_param_present_v2 || !(*p)->format) continue;
        value_text[0] = value_text[CORE_PARAM_TEXT - 1] = 0;
        if ((*p)->format(id, value, value_text)) {
            value_text[CORE_PARAM_TEXT - 1] = 0;
            return (uint32_t)(uintptr_t)value_text;
        }
    }
    return 0; /* Native formatter fallback in the assembly bridge. */
}

int32_t core_param_dest_visible(int32_t id)
{
    const struct core_param_presenter *const *p;
    for (p = core_param_presenters; *p; p++) {
        int32_t answer;
        if ((*p)->abi == &core_param_present_v2 && (*p)->visible &&
            (answer = (*p)->visible(id)) >= 0) return answer != 0;
    }
    return 1;
}

/* Compact only the fresh native UI vector. IDs, capacity and saved assignments stay intact. */
void core_param_lfo_filter(void *shared)
{
    uint32_t *vec;
    int32_t *begin, *end, *out;
    if (!shared || !*(uint32_t *)shared) return;
    vec = (void *)(uintptr_t)*(uint32_t *)shared;
    begin = (void *)(uintptr_t)vec[0]; end = (void *)(uintptr_t)vec[1];
    if (!begin || end < begin || end - begin > 185) return;
    out = begin;
    for (int32_t *p = begin; p < end; p++)
        if (core_param_dest_visible(*p)) *out++ = *p;
    vec[1] = (uint32_t)(uintptr_t)out;
}

int32_t core_param_lfo_step(uint32_t owner, int32_t id, int32_t proposed, int32_t delta)
{
    typedef int32_t (*get_fn)(uint32_t, int32_t);
    typedef int32_t (*step_fn)(uint32_t, int32_t, int32_t, int32_t);
    uint32_t *vtable;
    int32_t previous;
    if ((id != 52 && id != 61) || !delta || !core_param_presenters[0]) return proposed;
    if (proposed < 0 || proposed >> 8 >= 79 || core_param_dest_visible(fw_slot_ids[proposed >> 8]))
        return proposed;
    if (!owner) return proposed;
    vtable = *(uint32_t **)(uintptr_t)owner;
    previous = ((get_fn)(uintptr_t)vtable[10])(owner, id);
    for (int32_t n = 0; n < 79; n++) {
        int32_t next = ((step_fn)(uintptr_t)vtable[8])(owner, id, proposed, delta > 0 ? 256 : -256);
        if (next == proposed) break;
        proposed = next;
        if (proposed < 0 || proposed >> 8 >= 79 || core_param_dest_visible(fw_slot_ids[proposed >> 8]))
            return proposed;
    }
    return previous;
}

uint32_t core_param_lfo_name(int32_t offset, int32_t mode)
{
    /* mode 2 is the native list's width fallback, just like compact mode 1. */
    return core_param_name(offset / 60, mode ? CORE_PARAM_SHORT : CORE_PARAM_LONG);
}
#endif
