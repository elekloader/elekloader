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

const char sdk_alpha_dt1_empty_ui[] = "sdk alpha dt1 empty";
const char sdk_alpha_dt1_ui_v3[] = "sdk alpha dt1 pages 3";
const char sdk_alpha_dt1_ui_v4[] = "sdk alpha dt1 glyphs 4";
const char sdk_alpha_dt1_ui_v5[] = "sdk alpha dt1 render 5";
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

/* Older compiled descriptors end before the glyph pointer (v3), or before .def and .render (v4). */
static int sdk_alpha_has_glyphs(const struct elek_machine *m)
{
    const struct sdk_alpha_machine_ui *u = (const void *)core_machine_ui(m->id);
    return u && (u->abi == sdk_alpha_dt1_ui_v4 || u->abi == sdk_alpha_dt1_ui_v5);
}

static int sdk_alpha_v5(const struct elek_machine *m)
{
    const struct sdk_alpha_machine_ui *u = (const void *)core_machine_ui(m->id);
    return u && u->abi == sdk_alpha_dt1_ui_v5;
}

const struct elek_machine *sdk_alpha_machine(int32_t id)
{
    const struct sdk_alpha_machine_ui *u = (const void *)core_machine_ui(id);
    if (!u || (u->abi != sdk_alpha_dt1_empty_ui && u->abi != sdk_alpha_dt1_ui_v3 &&
               u->abi != sdk_alpha_dt1_ui_v4 && u->abi != sdk_alpha_dt1_ui_v5))
        return 0;
    const struct elek_machine *m = u->machine;
    if (!m || m->id != id || m->page_count > 2 || m->_reserved[0] || m->_reserved[1] ||
        (m->page_count && !m->pages) ||
        (u->abi == sdk_alpha_dt1_empty_ui && m->page_count))
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

/* The smallest raw value that maps to value or above: sdk_alpha_map only rises. */
static int32_t sdk_alpha_unmap(const struct elek_ui_knob *knob, int32_t value)
{
    int32_t lo = 0, hi = ELEK_UI_RAW_MAX;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (sdk_alpha_map(knob, mid) < value) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

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
            if (sdk_alpha_v5(m) && knobs[k]->def.supplied)
                n->def = sdk_alpha_unmap(knobs[k], knobs[k]->def.value);
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
    const struct elek_ui_knob *descriptors[15];
    int32_t knob_count = sdk_alpha_knobs(m, descriptors);
    if (knob_count < 0)
        return 0;
    const uint8_t *sound = 0;
    if (!view && knob_count) {
        uint32_t track = fw_active_track;
        const uint8_t *kit = fw_kit;
        if (track >= 8 || !kit)
            return 0;
        sound = FW_SOUND(kit, track);
    }
    struct elek_ui_knob_presentation presentation[15];
    struct elek_ui_knob_presentation *output[15];
    struct elek_ui_value values[15];
    const struct elek_ui_value *knobs[15];
    int has_glyphs = sdk_alpha_has_glyphs(m);
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
        presentation[k].glyph = has_glyphs ? descriptors[k]->glyph : 0;
        output[k] = &presentation[k];
    }
    const struct elek_ui_context ctx = {
        .page = page, .page_count = m->page_count, .knob_count = knob_count,
        .knob = knob_count ? knobs : 0, .track = fw_active_track,
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
    return sdk_alpha_has_glyphs(m) ? knobs[k]->glyph : 0;
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

void sdk_alpha_format(int32_t value, char *buf, int popup);

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
    sdk_alpha_format(sdk_alpha_map(knobs[k], raw < 0 ? 0 : raw > ELEK_UI_RAW_MAX ? ELEK_UI_RAW_MAX : raw),
                     buf, popup);
    return 1;
}

void sdk_alpha_format(int32_t value, char *buf, int popup)
{
    char digits[10];
    uint32_t n = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int count = 0, pos = 0;
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    if (!popup && count + (value < 0) > 5) { buf[0] = '#'; buf[1] = 0; return; }
    if (value < 0) buf[pos++] = '-';
    while (count) buf[pos++] = digits[--count];
    buf[pos] = 0;
}

/* sin, saw, anything: a phase step a sample at 48 kHz, as the Digitone's dnm_phase_inc. */
uint32_t elek_phase_inc(int32_t pitch)
{
    int32_t octaves = 0;
    if (pitch < 0) pitch = 0;
    if (pitch > 128 << 16) pitch = 128 << 16;           /* the Digitone's range */
    pitch += 3 << 16;                                   /* fw_pitch_tab is 1 at 63 << 16 / 384 */
    while (pitch > 87 << 16) {                          /* past the table's end, an octave at a time */
        pitch -= 12 << 16;
        ++octaves;
    }
    return ((23410327u >> 13) * (fw_pitch_tab[pitch / 384] >> 16)) << octaves;  /* 261.63 Hz times it */
}

/* From mp_render_s, before ev_render_voices' handlers: each track playing an SDK machine with a render.
 * One voice a track, so the voice slot is the track. */
void sdk_alpha_render(int32_t *blocks)
{
    for (uint32_t t = 0; t < 8; ++t) {
        const struct elek_machine *m = sdk_alpha_machine(fw_render_machine[t]);
        if (!m || !sdk_alpha_v5(m) || !m->render)
            continue;
        const struct elek_ui_knob *descriptors[15];
        int32_t knob_count = sdk_alpha_knobs(m, descriptors);
        if (knob_count < 0)
            continue;
        struct elek_ui_value values[15];
        const struct elek_ui_value *knobs[15];
        for (int32_t k = 0; k < knob_count; ++k) {
            int32_t raw = cm_src_value(t, k);
            if (raw < 0) raw = 0;
            if (raw > ELEK_UI_RAW_MAX) raw = ELEK_UI_RAW_MAX;
            values[k].raw_value = raw;
            values[k].value = sdk_alpha_map(descriptors[k], raw);
            knobs[k] = &values[k];
        }
        const struct elek_ui_context ctx = {
            .page = ELEK_UI_NO_PAGE, .page_count = m->page_count, .knob_count = knob_count,
            .knob = knob_count ? knobs : 0, .track = t,
        };
        const struct elek_voice voice = {
            .voice = t, .pitch = fw_voice_note[t], .velocity = fw_voice_vel[t] >> 8,
            .fresh = (fw_voice_start >> t) & 1,
            .gate = fw_amp_env[3 * t] != 0,             /* phase 0: released (the render's note-off mask) */
        };
        m->render(&ctx, &voice, blocks + 32 * t);
    }
}
