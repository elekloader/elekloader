/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Core Sound API with explicit native-object fixtures; no machines dependency. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/core-dn1/sound.c"

static uint8_t kit[4 * 326 + 0x2c];
uint8_t *volatile fw_kit = kit;
const uint32_t fw_sound_change_event_vtable = 0;
static uint32_t ref[1], ref_vtable[5];
static int32_t changed, sound_writes, latest_slot;
static int32_t project_present = 1, ref_present = 1;
static uint32_t ptr(const void *p)
{
    assert((uintptr_t)p <= UINT32_MAX);
    return (uint32_t)(uintptr_t)p;
}
static void sound_changed(void *r, void *e)
{
    uint32_t *ev = e;
    assert(r == ref && ev[0] == ptr(&fw_sound_change_event_vtable));
    assert(*((uint8_t *)e + 8) == 1);
    assert((int32_t)ev[1] == latest_slot);
    changed++;
}
void core_sound_set(int32_t track, int32_t slot, int32_t value)
{
    if (!fw_kit || track < 0 || track >= 4 || slot < 0 || slot >= 79) return;
    sound_writes++;
    latest_slot = slot;
    FW_SOUND_SLOT(kit, track, slot) = value;
    core_sound_apply(value, track, slot);
}
void fw_sound_live(int32_t value, int32_t track, int32_t slot)
{ assert(FW_SOUND_SLOT(kit, track, slot) == value); }
uint32_t fw_project_get(void) { return project_present ? 0x100000 : 0; }
uint32_t fw_sound_ref(uint32_t owner, int32_t track)
{
    assert(owner == 0x100250 && track >= 0 && track < 4);
    return ref_present ? ptr(ref) : 0;
}
static void test_core_sound_api(void)
{
    int32_t before = sound_writes;
    core_sound_set(-1, 0, 123); core_sound_set(4, 0, 123);
    core_sound_set(0, -1, 123); core_sound_set(0, 79, 123);
    fw_kit = 0; core_sound_set(0, 0, 123); fw_kit = kit;
    assert(sound_writes == before);
    for (int32_t track = 0; track < 4; track++) {
        core_sound_set(track, 78, 1000 + track);
        assert(FW_SOUND_SLOT(kit, track, 78) == 1000 + track);
    }
    assert(sound_writes == before + 4);
    puts("PASS core-only persistent edits: all tracks/bounds and absent kit");
}

int main(void)
{
    ref[0] = ptr(ref_vtable);
    ref_vtable[4] = ptr(sound_changed);
    core_sound_set(0, 17, 1234);
    assert(sound_writes == 1 && changed == 1);
    int32_t before = changed;
    project_present = 0;
    core_sound_set(0, 17, 1234);
    assert(changed == before);
    project_present = 1; ref_present = 0;
    core_sound_set(0, 17, 1234);
    assert(changed == before);
    ref_present = 1;
    test_core_sound_api();
    puts("PASS native SoundRef event and absent project/ref");
    return 0;
}
