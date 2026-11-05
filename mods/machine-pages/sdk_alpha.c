/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private adapter onto machine-pages' existing two-page/fifteen-value backend. */
#include "elekloader/sdk_alpha_machine.h"
#include "src.h"

/* ELEK_REGISTER_MACHINE emits struct sdk_alpha_registration; core reads it as a struct cm_machine. */
_Static_assert(sizeof(struct sdk_alpha_registration) == sizeof(struct cm_machine) &&
               __builtin_offsetof(struct sdk_alpha_registration, render) ==
               __builtin_offsetof(struct cm_machine, render) &&
               __builtin_offsetof(struct sdk_alpha_registration, ui) ==
               __builtin_offsetof(struct cm_machine, ui) &&
               SDK_ALPHA_UI_TAG == CM_UI_TAG, "registration layout");

const char sdk_alpha_dt1_ui_v1[] = "sdk alpha 1";
static const struct elek_machine *sdk_alpha_current;
static uint32_t sdk_alpha_empty_layout[11];
extern uint32_t mp_stock_layout(int32_t machine);
extern int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked);

struct sdk_alpha_page {
    const struct elek_machine *machine;
    struct cm_ui32 ui;
    struct cm_src_page pages[2];
};
static struct sdk_alpha_page sdk_alpha_pages[32]; /* same machine capacity as pages.c */
static uint32_t sdk_alpha_page_count;
static const struct elek_machine *sdk_alpha_presented;
static char sdk_alpha_labels[15][ELEK_UI_LABEL_MAX + 1];
static uint8_t sdk_alpha_visibility[15];
static const struct elek_ui_glyph *sdk_alpha_glyphs[15];

const struct elek_machine *sdk_alpha_machine(int32_t id)
{
    const struct sdk_alpha_machine_ui *u = (const void *)core_machine_ui(id);
    if (!u || u->abi != sdk_alpha_dt1_ui_v1)
        return 0;
    const struct elek_machine *m = u->machine;
    if (!m || m->id != id || m->page_count > 2 || m->_reserved[0] || m->_reserved[1] ||
        (m->page_count && !m->pages))
        return 0;
    return m;
}

/* Pages discover descriptors; explicit IDs keep values independent of layout order. */
static int32_t sdk_alpha_knobs(const struct elek_machine *m, const struct elek_ui_knob **knobs)
{
    uint32_t count = 0;
    for (uint32_t k = 0; k < 15; ++k) knobs[k] = 0;
    if (m->page_count > 2 || (m->page_count && !m->pages))
        return -1;
    for (uint32_t page = 0; page < m->page_count; ++page)
        for (uint32_t encoder = 0; encoder < 8; ++encoder) {
            const struct elek_ui_knob *knob = m->pages[page].knob[encoder];
            if (!knob) continue;
            uint32_t id = knob->id;
            if (id >= 15 || (knobs[id] && knobs[id] != knob))
                return -1;
            knobs[id] = knob;
            if (count <= id) count = id + 1;
        }
    for (uint32_t k = 0; k < count; ++k)
        if (!knobs[k]) return -1;
    return count;
}

/* Split the product so the full signed-32-bit bound range needs no 64-bit runtime. */
int32_t sdk_alpha_map(const struct elek_ui_knob *knob, int32_t raw)
{
    int32_t min = knob->min.supplied ? knob->min.value : 0;
    int32_t max = knob->max.supplied ? knob->max.value : 127;
    uint32_t span = (uint32_t)max - (uint32_t)min;
    uint32_t offset = (span / ELEK_UI_RAW_MAX) * (uint32_t)raw +
        ((span % ELEK_UI_RAW_MAX) * (uint32_t)raw + ELEK_UI_RAW_MAX / 2) / ELEK_UI_RAW_MAX;
    return (int32_t)((uint32_t)min + offset);
}

/* Choose the first native step that reaches the mapped default. */
static int32_t sdk_alpha_default(const struct elek_ui_knob *knob)
{
    if (!knob->def.supplied) return 0;
    int32_t lo = 0, hi = ELEK_UI_RAW_MAX;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (sdk_alpha_map(knob, mid) < knob->def.value) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static const struct elek_machine *sdk_alpha_voice_machine[8];
static uint32_t sdk_alpha_voice_state[8][ELEK_VOICE_STATE_MAX / 4];
static uint8_t sdk_alpha_start[8];      /* fw_voice_start lasts one block; kept until render runs */

const struct cm_ui32 *sdk_alpha_ui(int32_t id)
{
    const struct elek_machine *m = sdk_alpha_machine(id);
    if (!m || !m->page_count)
        return 0;
    for (uint32_t i = 0; i < sdk_alpha_page_count; ++i)
        if (sdk_alpha_pages[i].machine == m)
            return &sdk_alpha_pages[i].ui;
    if (sdk_alpha_page_count == 32)
        return 0;
    const struct elek_ui_knob *knobs[15];
    int32_t knob_count = sdk_alpha_knobs(m, knobs);
    if (knob_count < 0)
        return 0;
    struct sdk_alpha_page *p = &sdk_alpha_pages[sdk_alpha_page_count++];
    p->machine = m;
    p->ui.base.abi = cm_ui_v32;
    p->ui.base.page_from = 3;
    p->ui.base.group = m->name;
    p->ui.page_count = m->page_count;
    p->ui.pages = p->pages;
    for (uint32_t k = 0; k < 15; ++k) {
        struct cm_knob *n = k < 8 ? &p->ui.base.knob[k] : &p->ui.extra[k - 8];
        n->look = 134;              /* native unipolar encoder, 0..127 in 8.8 */
        n->flags = CM_RANGE | CM_DEFAULT | CM_NOT_SAMPLE;
        n->max = ELEK_UI_RAW_MAX;
        if (k < (uint32_t)knob_count) {
            n->name = n->lname = knobs[k]->label;
            n->def = sdk_alpha_default(knobs[k]);
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
    return &p->ui;
}

int32_t sdk_alpha_snapshot(const struct elek_machine *m, int32_t page, void *view)
{
    uint32_t track = fw_active_track;
    const struct elek_ui_knob *descriptors[15];
    int32_t knob_count = sdk_alpha_knobs(m, descriptors);
    if (knob_count < 0)
        return 0;
    const uint8_t *sound = 0;
    if (track >= 8) return 0;
    if (!view && knob_count) {
        const uint8_t *kit = fw_kit;
        if (!kit)
            return 0;
        sound = FW_SOUND(kit, track);
    }
    struct elek_ui_knob_presentation presentation[15];
    struct elek_ui_knob_presentation *output[15];
    struct elek_ui_value values[15];
    const struct elek_ui_value *knobs[15];
    for (uint32_t k = 0; k < (uint32_t)knob_count; ++k) {
        uint8_t locked;
        uint32_t id = k < 8 ? 0x84 + k : MP_EXTRA_ID + k - 8;
        uint32_t slot = k < 8 ? 17 + k : MP_EXTRA_SLOT(k - 8);
        int32_t raw = view ? mp_shown_value(view, id, &locked) :
            (int16_t)((sound[20 + 2 * slot] << 8) | sound[21 + 2 * slot]);
        if (raw < 0) raw = 0;
        if (raw > ELEK_UI_RAW_MAX) raw = ELEK_UI_RAW_MAX;
        values[k].raw_value = raw;
        values[k].value = sdk_alpha_map(descriptors[k], raw);
        knobs[k] = &values[k];
        presentation[k].label = descriptors[k]->label;
        presentation[k].visible = 1;
        presentation[k].glyph = descriptors[k]->glyph;
        output[k] = &presentation[k];
    }
    const struct elek_ui_context ctx = {
        .page = page, .page_count = m->page_count, .knob_count = knob_count,
        .knob = knob_count ? knobs : 0, .track = track,
    };
    struct elek_ui_presentation out = {
        .knob_count = knob_count, .knob = knob_count ? output : 0,
    };
    if (m->present)
        m->present(&ctx, &out);
    int32_t changed = sdk_alpha_presented != m;
    for (uint32_t k = 0; k < (uint32_t)knob_count; ++k) {
        const char *label = presentation[k].label;
        if (!label) label = descriptors[k]->label;
        uint32_t n = 0;
        for (; n < ELEK_UI_LABEL_MAX && label[n]; ++n) {
            unsigned char c = label[n];
            char next = c >= 32 && c <= 126 ? c : '?';
            changed |= sdk_alpha_labels[k][n] != next;
            sdk_alpha_labels[k][n] = next;
        }
        changed |= sdk_alpha_labels[k][n] != 0;
        sdk_alpha_labels[k][n] = 0;
        uint8_t visible = presentation[k].visible != 0;
        changed |= sdk_alpha_visibility[k] != visible;
        sdk_alpha_visibility[k] = visible;
        changed |= sdk_alpha_glyphs[k] != presentation[k].glyph;
        sdk_alpha_glyphs[k] = presentation[k].glyph;
    }
    sdk_alpha_presented = m;
    return changed;
}

void sdk_alpha_render(int32_t *blocks)
{
    for (uint32_t track = 0; track < 8; ++track) {
        const struct elek_machine *m = sdk_alpha_machine(fw_render_machine[track]);
        if (m != sdk_alpha_voice_machine[track]) {
            for (uint32_t i = 0; i < ELEK_VOICE_STATE_MAX / 4; ++i)
                sdk_alpha_voice_state[track][i] = 0;
            sdk_alpha_voice_machine[track] = m;
            sdk_alpha_start[track] = 0;
        }
        if (!m || !m->render) continue;
        if (fw_voice_start & (1u << track)) sdk_alpha_start[track] = 1;
        const struct elek_ui_knob *descriptors[15];
        int32_t count = sdk_alpha_knobs(m, descriptors);
        if (count < 0) continue;
        int32_t knobs[15];
        for (int32_t k = 0; k < count; ++k) {
            int32_t raw = cm_src_value(track, k);
            if (raw < 0) raw = 0;
            if (raw > ELEK_UI_RAW_MAX) raw = ELEK_UI_RAW_MAX;
            knobs[k] = sdk_alpha_map(descriptors[k], raw);
        }
        const struct elek_audio_context ctx = {
            .knob_count = count, .knob = count ? knobs : 0,
        };
        const struct elek_voice voice = {
            .slot = track, .track = track, .pitch = fw_voice_note[track],
            .velocity = fw_voice_vel[track] >> 8, .gate = fw_amp_env[3 * track] != 0,
            .flags = sdk_alpha_start[track] ? ELEK_VOICE_START : 0,
            .state = m->voice_state ? sdk_alpha_voice_state[track] : 0,
        };
        m->render(&ctx, &voice, blocks + ELEK_VOICE_FRAMES * track);
        sdk_alpha_start[track] = 0;
    }
}

const char *sdk_alpha_label(const struct elek_machine *m, uint32_t k)
{
    if (!m)
        return 0;
    const struct elek_ui_knob *knobs[15];
    int32_t count = sdk_alpha_knobs(m, knobs);
    if (count < 0 || k >= (uint32_t)count)
        return 0;
    return m == sdk_alpha_presented ? sdk_alpha_labels[k] : knobs[k]->label;
}

const struct elek_ui_glyph *sdk_alpha_glyph(const struct elek_machine *m, uint32_t k)
{
    const struct elek_ui_knob *knobs[15];
    int32_t count = sdk_alpha_knobs(m, knobs);
    if (count < 0 || k >= (uint32_t)count)
        return 0;
    if (m == sdk_alpha_presented)
        return sdk_alpha_glyphs[k];
    return knobs[k]->glyph;
}

int32_t sdk_alpha_visible(const struct elek_machine *m, uint32_t k)
{
    const struct elek_ui_knob *knobs[15];
    int32_t count = sdk_alpha_knobs(m, knobs);
    return count >= 0 && k < (uint32_t)count &&
        (m != sdk_alpha_presented || sdk_alpha_visibility[k]);
}

void sdk_alpha_present(const struct elek_machine *machine)
{
    sdk_alpha_snapshot(machine, ELEK_UI_NO_PAGE, 0);
}

uint32_t sdk_alpha_layout(int32_t machine, uint32_t track)
{
    const struct elek_machine *m = sdk_alpha_machine(machine);
    sdk_alpha_current = 0;
    if (!m || m->page_count)
        return 0;
    sdk_alpha_current = m;
    const uint32_t *stock = (const uint32_t *)mp_stock_layout(3);
    for (int i = 0; i < 11; ++i)
        sdk_alpha_empty_layout[i] = i >= 2 && i < 10 ? 0 : stock[i];
    if (track < 8)
        sdk_alpha_present(m);
    return (uint32_t)sdk_alpha_empty_layout;
}

int32_t sdk_alpha_empty_selected(void)
{
    return sdk_alpha_current != 0;
}

/* A mapped integer's text fits the native value field; long values retain their popup text. */
int32_t sdk_alpha_value_text(int32_t machine, int32_t k, int32_t raw, char *buf, int popup)
{
    const struct elek_machine *m = sdk_alpha_machine(machine);
    if (!m)
        return 0;
    const struct elek_ui_knob *knobs[15];
    int32_t knob_count = sdk_alpha_knobs(m, knobs);
    if (knob_count < 0 || (uint32_t)k >= (uint32_t)knob_count)
        return 0;
    int32_t value = sdk_alpha_map(knobs[k], raw < 0 ? 0 : raw > ELEK_UI_RAW_MAX ? ELEK_UI_RAW_MAX : raw);
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
