/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native.h"

const uint32_t dnm_ui_v1 = 1;

void dnm_sound_edit(int32_t track, int32_t slot, int32_t value)
{
    core_sound_set(track, slot, value);
}

int32_t dnm_knob_value(int32_t knob, int32_t *locked)
{
    int32_t slot = dnm_knob_slot(knob), track = fw_active_track, value;
    uint8_t native_locked = 0;
    void *view;
    if (locked)
        *locked = 0;
    if (!fw_kit || track < 0 || track >= 4 || slot < 0 || slot >= 79)
        return 0;
    view = dnm_syn_view((knob % DNM_KNOBS) / 8 + 1);
    if (!view)
        return FW_SOUND_SLOT(fw_kit, track, slot);
    value = fw_view_param_value(view, fw_slot_ids[slot], &native_locked);
    if (locked)
        *locked = native_locked != 0;
    return value;
}

int32_t dnm_reset(void *brain, int32_t syn, int32_t knob)
{
    const struct dnm_machine *m = dnm_active();
    void *view = dnm_syn_view(syn);
    int32_t track = fw_active_track;
    if (!brain || !view || !m || track < 0 || track >= 4 || knob < 0 || knob >= DNM_KNOBS_ALL)
        return 0;
    if (dnm_knob_slot(knob) < 0 || (knob >= DNM_KNOBS && !(m->flags & DNM_PAGES2)))
        return 1;
    if (!m->knobs[knob].name || !m->knobs[knob].name[0])
        return 1;
    if (!fw_key_held(1) || !m->defaults || m->defaults[knob] == -1)
        return 0;
    void *owner = dnm_ptr(dnm_read32((uint8_t *)view + 0x68));
    void *object = owner ? dnm_ptr(fw_track_get(owner)) : 0;
    int32_t result = core_lock_selected(object, dnm_knob_slot(knob), m->defaults[knob]);
    if (result < 0) {
        *((volatile uint8_t *)brain + 0x60) = 1;
        return 1; /* Consume a failed reset too: never fall through to FM defaults. */
    }
    if (!result) core_sound_set(track, dnm_knob_slot(knob), m->defaults[knob]);
    *((volatile uint8_t *)brain + 0x60) = 1;
    return 1;
}
