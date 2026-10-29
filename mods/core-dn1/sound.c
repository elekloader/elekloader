/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-dn1 4.2, OS1.44: persistent Sound edits.
 * UI task only. No new hook or boot action: these run only when a mod calls.
 * core_sound_set writes the kit, then calls core_sound_apply. Firmware pointer
 * returns use D0; declarations deliberately use uint32_t rather than void *. */
#include "digitone-mk1/core3.h"
#ifdef CORE_NATIVE_UI

static uint32_t read32(const void *p) { return *(volatile const uint32_t *)p; }
static void *sound_ptr(uint32_t p) { return (void *)(uintptr_t)p; }
/* Internal continuation of the validated assembly setter. The kit is already
 * written. A missing project/ref leaves kit/live updated without an event. */
void core_sound_apply(int32_t value, int32_t track, int32_t slot)
{
    uint32_t project;
    void *ref;
    const uint32_t *vtable;
    struct {
        uint32_t vtable;
        int32_t slot;
        uint8_t changed, pad[3];
    } event;
    fw_sound_live(value, track, slot);
    project = fw_project_get();
    if (!project)
        return;
    ref = sound_ptr(fw_sound_ref(project + 0x250, track));
    if (!ref)
        return;
    event.vtable = (uint32_t)(uintptr_t)&fw_sound_change_event_vtable;
    event.slot = slot;
    event.changed = 1;
    event.pad[0] = event.pad[1] = event.pad[2] = 0;
    vtable = sound_ptr(read32(ref));
    ((void (*)(void *, void *))sound_ptr(vtable[4]))(ref, &event);
}

#endif
