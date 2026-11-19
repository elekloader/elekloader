/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.15: the sound a trig plays (README, "Stock engines, trig
 * sounds, core_param_set (0.15)"). sound.s files, for each track, the
 * sound item stock pushed to the engine last: the track's sound in a live kit (a plain trig, a kit
 * apply) or a pool sound (a sound lock). Plain reads, from the audio
 * interrupt or the UI task. */
#include <stdint.h>

#define TRACKS 12
#define POOL ((uint8_t *)0x41313c47)        /* pool sound i: POOL + 0x98 i */
#define POOL_SOUNDS 128
#define SOUND_SIZE 0x98

void *volatile core_trig_items[TRACKS];

void *core_track_sound(int32_t track)
{
    return (uint32_t)track < TRACKS ? core_trig_items[track] : 0;
}

int core_track_pool_sound(int32_t track)
{
    uint8_t *s = core_track_sound(track);
    uint32_t o = (uint32_t)(s - POOL);
    if (!s || (uint32_t)s < (uint32_t)POOL || o % SOUND_SIZE || o / SOUND_SIZE >= POOL_SOUNDS)
        return -1;
    return o / SOUND_SIZE;
}

/* core_param_set: set a sound parameter of track t's sound in the current
 * kit as a knob turn does. Track t's parameter model (SoundParameterSet,
 * vtable 0x401dce24) is the app's (*0x419f28a8) + 0x1240 + 0x1c t; its slot
 * +0x4c gives the record id of a sound index for the sound's machine (0:
 * none), and 0x400a906c(model, id, value, 0, 1, out) is what the knob's turn
 * (slot +0x2c) ends in: it clamps the value to the record's range, commits it
 * (the live value, the kit word, a lock while live recording, the host's
 * notification, the view's redraw) and with out sends PARAM OUTPUT. UI task
 * only. */
#define APP() (*(volatile uint32_t *)0x419f28a8)
#define MODELS 0x1240
#define MODEL_SIZE 0x1c
#define SOUND_INDICES 42
#define SET_VALUE ((void (*)(void *, int32_t, int32_t, int32_t, int32_t, int32_t))0x400a906c)

int core_param_set(int32_t track, int32_t index, int32_t value, int32_t out)
{
    uint32_t app = APP();
    if (!app || (uint32_t)track >= TRACKS || (uint32_t)index >= SOUND_INDICES)
        return 0;
    void *model = (void *)(app + MODELS + MODEL_SIZE * track);
    int32_t (*record_of)(void *, int32_t) = *(int32_t (**)(void *, int32_t))(*(uint8_t **)model + 0x4c);
    int32_t id = record_of(model, index);
    if (id <= 0)
        return 0;
    SET_VALUE(model, id, value & 0xffff, 0, 1, out != 0);
    return 1;
}
