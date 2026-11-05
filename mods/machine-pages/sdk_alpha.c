/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's machines on machine-pages' two-page, fifteen-value backend (OS 1.54); the part every
 * device shares is ../sdk-alpha/machine.c. */
#include "dev.h"

/* ELEK_REGISTER_MACHINE emits struct sdk_alpha_registration; core reads it as a struct cm_machine. */
_Static_assert(sizeof(struct sdk_alpha_registration) == sizeof(struct cm_machine) &&
               __builtin_offsetof(struct sdk_alpha_registration, render) ==
               __builtin_offsetof(struct cm_machine, render) &&
               __builtin_offsetof(struct sdk_alpha_registration, ui) ==
               __builtin_offsetof(struct cm_machine, ui) &&
               SDK_ALPHA_UI_TAG == CM_UI_TAG, "registration layout");

const char sdk_alpha_dt1_ui_v1[] = "sdk alpha 1";
static const struct sdk_alpha_entry *sdk_alpha_current;
static uint32_t sdk_alpha_empty_layout[11];
extern uint32_t mp_stock_layout(int32_t machine);
extern int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked);

struct sdk_alpha_page {
    struct cm_ui32 ui;
    struct cm_src_page pages[2];
};
static struct sdk_alpha_page sdk_alpha_pages[SDK_ALPHA_MACHINES];

const struct cm_ui32 *sdk_alpha_ui(int32_t id)
{
    const struct sdk_alpha_entry *e = sdk_alpha_find(id);
    if (!e || !e->machine->page_count)
        return 0;
    const struct elek_machine *m = e->machine;
    struct sdk_alpha_page *p = &sdk_alpha_pages[e - sdk_alpha_entries];
    if (p->ui.pages)
        return &p->ui;
    p->ui.base.abi = cm_ui_v32;
    p->ui.base.page_from = 3;
    p->ui.base.group = m->name;
    p->ui.page_count = m->page_count;
    for (uint32_t k = 0; k < 15; ++k) {
        struct cm_knob *n = k < 8 ? &p->ui.base.knob[k] : &p->ui.extra[k - 8];
        n->look = 134;              /* native unipolar encoder, 0..127 in 8.8 */
        n->flags = CM_RANGE | CM_DEFAULT | CM_NOT_SAMPLE;
        n->max = ELEK_UI_RAW_MAX;
        if (k < e->count) {
            n->name = n->lname = e->knob[k]->label;
            n->def = sdk_alpha_default(e->knob[k]);
        } else
            n->flags |= CM_HIDDEN;
    }
    for (uint32_t page = 0; page < m->page_count; ++page) {
        p->pages[page].title = m->pages[page].label;
        for (int k = 0; k < 8; ++k) {
            const struct elek_ui_knob *knob = m->pages[page].knob[k];
            p->pages[page].parameter[k] = knob ? knob->id : CM_SRC_BLANK;
        }
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

void sdk_alpha_render(int32_t *blocks)
{
    for (uint32_t track = 0; track < 8; ++track) {
        int32_t id = fw_render_machine[track];
        const struct sdk_alpha_entry *e = sdk_alpha_find(id);
        sdk_alpha_select(track, id);
        if (!e || !e->machine->render) continue;
        int32_t raw[SDK_ALPHA_KNOBS];
        for (uint32_t k = 0; k < e->count; ++k)
            raw[k] = cm_src_value(track, k);
        struct elek_voice voice = {
            .slot = track, .track = track, .pitch = fw_voice_note[track],
            .velocity = fw_voice_vel[track] >> 8, .gate = fw_amp_env[3 * track] != 0,
        };
        sdk_alpha_voice(e, &voice, raw, (fw_voice_start & (1u << track)) != 0,
                        blocks + ELEK_VOICE_FRAMES * track);
    }
}

uint32_t sdk_alpha_layout(int32_t machine, uint32_t track)
{
    const struct sdk_alpha_entry *e = sdk_alpha_find(machine);
    sdk_alpha_current = 0;
    if (!e || e->machine->page_count)
        return 0;
    sdk_alpha_current = e;
    const uint32_t *stock = (const uint32_t *)mp_stock_layout(3);
    for (int i = 0; i < 11; ++i)
        sdk_alpha_empty_layout[i] = i >= 2 && i < 10 ? 0 : stock[i];
    if (track < 8)
        sdk_alpha_snapshot(e, ELEK_UI_NO_PAGE, 0);
    return (uint32_t)sdk_alpha_empty_layout;
}

int32_t sdk_alpha_empty_selected(void)
{
    return sdk_alpha_current != 0;
}

/* A mapped integer's text fits the native value field; long values retain their popup text. */
int32_t sdk_alpha_value_text(int32_t machine, int32_t k, int32_t raw, char *buf, int popup)
{
    const struct sdk_alpha_entry *e = sdk_alpha_find(machine);
    if (!e || (uint32_t)k >= e->count)
        return 0;
    int32_t value = sdk_alpha_map(e->knob[k], raw);
    char digits[10];
    uint32_t n = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int count = 0, pos = 0;
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    if (!popup && count + (value < 0) > 5) { buf[0] = '#'; buf[1] = 0; return 1; }
    if (value < 0) buf[pos++] = '-';
    while (count) buf[pos++] = digits[--count];
    buf[pos] = 0;
    return 1;
}
