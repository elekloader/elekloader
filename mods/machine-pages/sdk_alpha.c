/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's machines on machine-pages' two-page, fifteen-value backend (OS 1.54); the part every
 * device shares is ../sdk-alpha/machine.c. */
#include "dev.h"
#include "elekloader/sdk_alpha_keys.h"

const char sdk_alpha_abi_v1[] = "sdk alpha 1";
extern int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked);

struct sdk_alpha_page {
    struct cm_ui32 ui;
    struct cm_src_page pages[2];
};
static struct sdk_alpha_page sdk_alpha_pages[SDK_ALPHA_MACHINES];

const struct cm_ui32 *sdk_alpha_ui(int32_t id)
{
    const struct sdk_alpha_entry *e = sdk_alpha_find(id);
    if (!e)
        return 0;
    const struct elek_machine *m = e->machine;
    struct sdk_alpha_page *p = &sdk_alpha_pages[e - sdk_alpha_entries];
    if (p->ui.pages)
        return &p->ui;
    p->ui.base.abi = cm_ui_v32;
    p->ui.base.page_from = 3;
    p->ui.base.group = m->name;
    p->ui.page_count = e->pages;
    for (uint32_t k = 0; k < 15; ++k) {
        struct cm_knob *n = k < 8 ? &p->ui.base.knob[k] : &p->ui.extra[k - 8];
        n->look = 134;              /* native unipolar encoder, 0..127 in 8.8 */
        n->flags = CM_RANGE | CM_DEFAULT | CM_NOT_SAMPLE;
        n->max = SDK_ALPHA_RAW_MAX;
        if (k < e->count) {
            n->name = n->lname = m->knobs[k].label;
            n->def = sdk_alpha_default(&m->knobs[k]);
        } else
            n->flags |= CM_HIDDEN;
    }
    for (uint32_t page = 0; page < e->pages; ++page) {
        p->pages[page].title = e->title[page];
        for (int k = 0; k < 8; ++k)
            p->pages[page].parameter[k] = e->encoder[page][k] >= 0 ? e->encoder[page][k] : CM_SRC_BLANK;
    }
    p->ui.pages = p->pages;
    return &p->ui;
}

/* The active track's values, from the view drawing them (with its locks) or else the kit. */
int32_t sdk_alpha_snapshot(const struct sdk_alpha_entry *e, int32_t page, void *view)
{
    uint32_t track = fw_active_track;
    const uint8_t *sound = 0;
    int32_t raw[SDK_ALPHA_KNOBS];
    if (track >= 8) return 0;
    if (!view && e->count) {
        const uint8_t *kit = fw_kit;
        if (!kit)
            return 0;
        sound = FW_SOUND(kit, track);
    }
    for (uint32_t k = 0; k < e->count; ++k) {
        uint8_t locked;
        uint32_t id = k < 8 ? 0x84 + k : MP_EXTRA_ID + k - 8;
        uint32_t slot = k < 8 ? 17 + k : MP_EXTRA_SLOT(k - 8);
        raw[k] = view ? mp_shown_value(view, id, &locked) :
            (int16_t)((sound[20 + 2 * slot] << 8) | sound[21 + 2 * slot]);
    }
    const struct sdk_alpha_entry *before = sdk_alpha_seen;
    return sdk_alpha_present(e, track, page, raw, 0) | (before != e);
}

/* Mono machines (.play), on core 4.2's ev_live_note: every live key on such a track is taken, and the stock
 * plays only the note .prio chooses, through fw_note_on and fw_note_off, so it holds one key at a time. */
static struct {
    struct sdk_alpha_keys keys;
    int32_t id;                         /* the machine they are for */
    int32_t stock;                      /* the note the stock holds, or -1 */
} mono[8];
static volatile int32_t mono_bend[8];   /* 16.16, added to the stock's note: legato's pitch */

static uint16_t mono_lock(void)
{
    uint16_t sr = 0;
#ifdef __m68k__
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) :: "memory", "cc");
#endif
    return sr;
}

static void mono_unlock(uint16_t sr)
{
#ifdef __m68k__
    __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory", "cc");
#else
    (void)sr;
#endif
}

int32_t sdk_alpha_live_note(int32_t track, int32_t note, int32_t vel, int32_t on, int32_t source)
{
    const uint8_t *kit = fw_kit;
    if ((uint32_t)track >= 8 || (uint32_t)note > 127 || !kit)
        return 0;
    int32_t id = FW_SOUND(kit, track)[FW_SOUND_MACHINE];
    const struct sdk_alpha_entry *e = sdk_alpha_find(id);
    if (!e || e->machine->play == ELEK_POLY)
        return 0;
    /* shortcut: the lock covers the choice, not the stock calls, so a UI key and a MIDI note on one track
     * at the same instant may play in either order; widen it if that is ever heard. */
    uint16_t sr = mono_lock();
    if (mono[track].id != id) {
        for (uint8_t *p = (uint8_t *)&mono[track].keys; p < (uint8_t *)(&mono[track].keys + 1); ++p)
            *p = 0;
        mono[track].id = id;
        mono[track].stock = -1;
        mono_bend[track] = 0;
    }
    /* a release of a key not held (pressed before the machine came) is the stock's */
    if (!on && sdk_alpha_held(&mono[track].keys, note << 16) < 0) {
        mono_unlock(sr);
        return 0;
    }
    struct sdk_alpha_held key = {0, 0};
    int32_t did = sdk_alpha_key(&mono[track].keys, e->machine, note << 16, on ? vel : 0, on, &key);
    int32_t was = mono[track].stock, play = key.pitch >> 16;
    if (did == SDK_ALPHA_PLAY) {
        mono[track].stock = play;
        mono_bend[track] = 0;
    } else if (did == SDK_ALPHA_LEGATO) {
        mono_bend[track] = key.pitch - (was << 16);
    } else if (did == SDK_ALPHA_SILENCE) {
        mono[track].stock = -1;
        mono_bend[track] = 0;
    }
    mono_unlock(sr);
    if (did == SDK_ALPHA_PLAY) {
        if (was >= 0)
            fw_note_off(track, was, source);
        if (on && play == note)
            return 0;                   /* the key pressed: the stock plays it, with its own arguments */
        fw_note_on(track, play, key.velocity, source, 0, -1, -1);
    } else if (did == SDK_ALPHA_SILENCE) {
        if (!on && was == note)
            return 0;                   /* the key released: the stock releases it */
        fw_note_off(track, was, source);
    }
    return 1;
}

/* A track the AMP envelope has silenced and no trig starts: inaudible after the AMP page, so it is not
 * rendered (the level is 0 only then). render's idle counts its frames. */
static int silent(uint32_t track)
{
    return !(fw_voice_start & (1u << track)) && fw_amp_env[3 * track + 1] == 0;
}

void sdk_alpha_render(int32_t *blocks)
{
    static uint32_t idle[8];
    for (uint32_t track = 0; track < 8; ++track) {
        int32_t id = fw_render_machine[track];
        const struct sdk_alpha_entry *e = sdk_alpha_find(id);
        sdk_alpha_select(track, id);
        if (!e || !e->machine->render) continue;
        if (silent(track)) {
            for (uint32_t i = 0; i < ELEK_VOICE_FRAMES; ++i)
                blocks[ELEK_VOICE_FRAMES * track + i] = 0;
            if (idle[track] <= UINT32_MAX - ELEK_VOICE_FRAMES)
                idle[track] += ELEK_VOICE_FRAMES;
            continue;
        }
        int32_t raw[SDK_ALPHA_KNOBS];
        for (uint32_t k = 0; k < e->count; ++k)
            raw[k] = cm_src_value(track, k);
        struct elek_voice voice = {
            .track = track, .pitch = fw_voice_note[track] + (mono[track].id == id ? mono_bend[track] : 0),
            .velocity = fw_voice_vel[track] >> 8,
        };
        sdk_alpha_voice(e, track, &voice, raw, (fw_voice_start & (1u << track)) != 0, idle[track],
                        blocks + ELEK_VOICE_FRAMES * track);
        idle[track] = 0;
    }
}

/* .present's value text, or the mapped integer, fits the native value field; long numbers keep their
 * popup text. */
int32_t sdk_alpha_value_text(int32_t machine, int32_t k, int32_t raw, char *buf, int popup)
{
    const struct sdk_alpha_entry *e = sdk_alpha_find(machine);
    if (!e || (uint32_t)k >= e->count)
        return 0;
    sdk_alpha_value(e, (uint32_t)k, raw, buf, popup);
    return 1;
}
