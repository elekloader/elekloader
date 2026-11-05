/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's machines, the part every companion shares (machine.h). The companion's device layer
 * (dev.h, in its own folder) gives:
 *   SDK_ALPHA_MACHINES   how many machines it takes;
 *   dev_candidate(i, &m) its i-th registered machine in m (0 for one that is not an SDK machine);
 *                        0 past the last;
 *   dev_fillrect         the firmware's fillrect(bmp, x0, y0, x1, y1, on);
 *   SDK_ALPHA_VOICES     the voices it renders, when it renders. */
#include "dev.h"

struct sdk_alpha_entry sdk_alpha_entries[SDK_ALPHA_MACHINES];
int32_t sdk_alpha_count = -1;

/* The checks the builder makes too, once: a machine refused here is not an SDK machine. The entry is
 * written only with its final values, which a reentrant run writes again. */
static int index_machine(struct sdk_alpha_entry *e, const struct elek_machine *m)
{
    const struct elek_ui_knob *by_id[SDK_ALPHA_KNOBS];
    uint32_t count = 0, k;
    if (m->page_count > 2 || (m->page_count && !m->pages) || m->_reserved[0] || m->_reserved[1])
        return 0;
    for (k = 0; k < SDK_ALPHA_KNOBS; ++k)
        by_id[k] = 0;
    for (uint32_t page = 0; page < m->page_count; ++page)
        for (uint32_t encoder = 0; encoder < 8; ++encoder) {
            const struct elek_ui_knob *knob = m->pages[page].knob[encoder];
            if (!knob) continue;
            uint32_t id = knob->id;     /* explicit ids keep values independent of layout order */
            if (id >= SDK_ALPHA_KNOBS || (by_id[id] && by_id[id] != knob))
                return 0;
            by_id[id] = knob;
            if (count <= id) count = id + 1;
        }
    for (k = 0; k < count; ++k)
        if (!by_id[k]) return 0;
    for (k = 0; k < count; ++k)
        e->knob[k] = by_id[k];
    e->count = count;
    e->machine = m;
    return 1;
}

/* Reentrant: a second run writes the same data; the count is published once every entry is ready. */
void sdk_alpha_init(void)
{
    const struct elek_machine *m;
    int32_t n = 0;
    if (sdk_alpha_count >= 0)
        return;
    for (uint32_t i = 0; n < SDK_ALPHA_MACHINES && dev_candidate(i, &m); ++i)
        if (m && index_machine(&sdk_alpha_entries[n], m))
            ++n;
    __asm__ volatile ("" ::: "memory");
    sdk_alpha_count = n;
}

const struct sdk_alpha_entry *sdk_alpha_find(int32_t id)
{
    sdk_alpha_init();
    for (int32_t i = 0; i < sdk_alpha_count; ++i)
        if (sdk_alpha_entries[i].machine->id == id)
            return &sdk_alpha_entries[i];
    return 0;
}

/* Split the product so the full signed-32-bit bound range needs no 64-bit runtime. */
int32_t sdk_alpha_map(const struct elek_ui_knob *knob, int32_t raw)
{
    int32_t min = knob->min.supplied ? knob->min.value : 0;
    int32_t max = knob->max.supplied ? knob->max.value : 127;
    uint32_t span = (uint32_t)max - (uint32_t)min;
    if (raw < 0) raw = 0;
    if (raw > ELEK_UI_RAW_MAX) raw = ELEK_UI_RAW_MAX;
    uint32_t offset = (span / ELEK_UI_RAW_MAX) * (uint32_t)raw +
        ((span % ELEK_UI_RAW_MAX) * (uint32_t)raw + ELEK_UI_RAW_MAX / 2) / ELEK_UI_RAW_MAX;
    return (int32_t)((uint32_t)min + offset);
}

int32_t sdk_alpha_default(const struct elek_ui_knob *knob)
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

char sdk_alpha_labels[SDK_ALPHA_KNOBS][ELEK_UI_LABEL_MAX + 1];
uint8_t sdk_alpha_visibility[SDK_ALPHA_KNOBS];
const struct elek_ui_glyph *sdk_alpha_glyphs[SDK_ALPHA_KNOBS];
const struct sdk_alpha_entry *sdk_alpha_seen;
int32_t sdk_alpha_seen_page = ELEK_UI_NO_PAGE;
static uint32_t seen_track;
static int32_t seen_raw[SDK_ALPHA_KNOBS];

int32_t sdk_alpha_present(const struct sdk_alpha_entry *e, uint32_t track, int32_t page,
                          const int32_t *raw, int force)
{
    const struct elek_machine *m = e->machine;
    uint32_t n = e->count, k;
    struct elek_ui_value values[SDK_ALPHA_KNOBS];
    const struct elek_ui_value *input[SDK_ALPHA_KNOBS];  /* 0..n-1 filled: no memset in the build */
    struct elek_ui_knob_presentation presentation[SDK_ALPHA_KNOBS];
    struct elek_ui_knob_presentation *output[SDK_ALPHA_KNOBS];
    int32_t changed = 0;
    if (page < 0 || (uint32_t)page >= m->page_count)
        page = ELEK_UI_NO_PAGE;
    for (k = 0; k < n; ++k) {
        int32_t r = raw[k] < 0 ? 0 : raw[k] > ELEK_UI_RAW_MAX ? ELEK_UI_RAW_MAX : raw[k];
        values[k].raw_value = r;
        values[k].value = sdk_alpha_map(e->knob[k], r);
        input[k] = &values[k];
        presentation[k].label = e->knob[k]->label;
        presentation[k].visible = 1;
        presentation[k].glyph = e->knob[k]->glyph;
        output[k] = &presentation[k];
    }
    if (!force && e == sdk_alpha_seen && track == seen_track && page == sdk_alpha_seen_page) {
        for (k = 0; k < n && values[k].raw_value == seen_raw[k]; ++k)
            ;
        if (k == n)
            return 0;
    }
    for (k = 0; k < n; ++k)
        seen_raw[k] = values[k].raw_value;
    sdk_alpha_seen = e;
    seen_track = track;
    sdk_alpha_seen_page = page;
    const struct elek_ui_context ctx = {
        .page = page, .page_count = m->page_count, .knob_count = n,
        .knob = n ? input : 0, .track = track,
    };
    struct elek_ui_presentation out = { .knob_count = n, .knob = n ? output : 0 };
    if (m->present)
        m->present(&ctx, &out);
    for (k = 0; k < n; ++k) {
        const char *label = presentation[k].label ? presentation[k].label : e->knob[k]->label;
        uint32_t c = 0;
        for (; c < ELEK_UI_LABEL_MAX && label[c]; ++c) {
            unsigned char ch = label[c];
            char next = ch >= 32 && ch <= 126 ? ch : '?';
            changed |= sdk_alpha_labels[k][c] != next;
            sdk_alpha_labels[k][c] = next;
        }
        changed |= sdk_alpha_labels[k][c] != 0;
        sdk_alpha_labels[k][c] = 0;
        uint8_t visible = presentation[k].visible != 0;
        changed |= sdk_alpha_visibility[k] != visible;
        sdk_alpha_visibility[k] = visible;
        changed |= sdk_alpha_glyphs[k] != presentation[k].glyph;
        sdk_alpha_glyphs[k] = presentation[k].glyph;
    }
    return changed;
}

const char *sdk_alpha_label(const struct sdk_alpha_entry *e, uint32_t k)
{
    if (k >= e->count)
        return 0;
    return e == sdk_alpha_seen ? sdk_alpha_labels[k] : e->knob[k]->label;
}

const struct elek_ui_glyph *sdk_alpha_glyph(const struct sdk_alpha_entry *e, uint32_t k)
{
    if (k >= e->count)
        return 0;
    return e == sdk_alpha_seen ? sdk_alpha_glyphs[k] : e->knob[k]->glyph;
}

int32_t sdk_alpha_visible(const struct sdk_alpha_entry *e, uint32_t k)
{
    return k < e->count && (e != sdk_alpha_seen || sdk_alpha_visibility[k]);
}

void sdk_alpha_blit(void *bmp, int32_t x, int32_t y, const struct elek_ui_glyph *glyph)
{
    dev_fillrect(bmp, x, y, x + 16, y + 16, 0);
    for (int row = 0; row < 16; ++row)
        for (int col = 0; col < 16; ++col)
            if (glyph->rows[row] & (0x8000u >> col))
                dev_fillrect(bmp, x + col, y + 16 - row, x + col, y + 16 - row, 1);
}

#ifdef SDK_ALPHA_VOICES
static struct {
    int32_t id;
    uint8_t start;                      /* the device's start flag lasts one block; kept until render */
    uint32_t state[ELEK_VOICE_STATE_MAX / 4];
} voices[SDK_ALPHA_VOICES];

void sdk_alpha_select(uint32_t slot, int32_t id)
{
    if (voices[slot].id == id) return;
    for (uint32_t i = 0; i < ELEK_VOICE_STATE_MAX / 4; ++i) voices[slot].state[i] = 0;
    voices[slot].id = id;
    voices[slot].start = 0;
}

void sdk_alpha_voice(const struct sdk_alpha_entry *e, struct elek_voice *v, const int32_t *raw,
                     int start, elek_sample *out)
{
    int32_t knob[SDK_ALPHA_KNOBS];
    if (start) voices[v->slot].start = 1;
    for (uint32_t k = 0; k < e->count; ++k)
        knob[k] = sdk_alpha_map(e->knob[k], raw[k]);
    const struct elek_audio_context ctx = { .knob_count = e->count, .knob = e->count ? knob : 0 };
    v->flags = voices[v->slot].start ? ELEK_VOICE_START : 0;
    v->state = e->machine->voice_state ? voices[v->slot].state : 0;
    e->machine->render(&ctx, v, out);
    voices[v->slot].start = 0;
}
#endif
