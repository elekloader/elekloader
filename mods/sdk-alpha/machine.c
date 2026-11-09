/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK alpha's machines, the part every companion shares (machine.h). The companion's device layer
 * (dev.h, in its own folder) gives:
 *   SDK_ALPHA_MACHINES   how many machines it takes;
 *   dev_candidate(i, &m) its i-th registered machine in m (0 for one that is not an SDK machine);
 *                        0 past the last;
 *   dev_fillrect         the firmware's fillrect(bmp, x0, y0, x1, y1, on);
 *   dev_active           the active track's machine, or 0, on a device whose value formatter is given
 *                        no knob;
 *   SDK_ALPHA_VOICES     the voices it renders, when it renders. */
#include "dev.h"

struct sdk_alpha_entry sdk_alpha_entries[SDK_ALPHA_MACHINES];
int32_t sdk_alpha_count = -1;

/* The checks the builder makes too, once: a machine refused here is not an SDK machine. The entry is
 * written only with its final values, which a reentrant run writes again. */
static int index_machine(struct sdk_alpha_entry *e, const struct elek_machine *m)
{
    uint32_t count = m->knob_count, pages = 0;
    if (count > SDK_ALPHA_KNOBS || (count && !m->knobs))
        return 0;
    while (pages < ELEK_UI_PAGE_MAX && m->pages[pages].label)
        ++pages;
    for (uint32_t page = 0; page < ELEK_UI_PAGE_MAX; ++page)
        for (uint32_t encoder = 0; encoder < 8; ++encoder) {
            const struct elek_knob *knob = m->pages[page].encoder[encoder];
            if (knob && (page >= pages || knob < m->knobs || knob >= m->knobs + count))
                return 0;
        }
    for (uint32_t page = 0; page < ELEK_UI_PAGE_MAX; ++page)
        for (uint32_t encoder = 0; encoder < 8; ++encoder) {
            const struct elek_knob *knob = m->pages[page].encoder[encoder];
            e->encoder[page][encoder] = knob ? (int8_t)(knob - m->knobs) : -1;
        }
    for (uint32_t page = 0; page < pages; ++page)
        e->title[page] = m->pages[page].label;
    if (!pages)
        e->title[pages++] = m->name;
    e->count = count;
    e->pages = pages;
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
int32_t sdk_alpha_map(const struct elek_knob *knob, int32_t raw)
{
    uint32_t span = (uint32_t)knob->max - (uint32_t)knob->min;
    if (raw < 0) raw = 0;
    if (raw > SDK_ALPHA_RAW_MAX) raw = SDK_ALPHA_RAW_MAX;
    uint32_t offset = (span / SDK_ALPHA_RAW_MAX) * (uint32_t)raw +
        ((span % SDK_ALPHA_RAW_MAX) * (uint32_t)raw + SDK_ALPHA_RAW_MAX / 2) / SDK_ALPHA_RAW_MAX;
    return (int32_t)((uint32_t)knob->min + offset);
}

int32_t sdk_alpha_default(const struct elek_knob *knob)
{
    int32_t def = knob->def < knob->min ? knob->min : knob->def > knob->max ? knob->max : knob->def;
    int32_t lo = 0, hi = SDK_ALPHA_RAW_MAX;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (sdk_alpha_map(knob, mid) < def) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void sdk_alpha_number(int32_t value, char *buf, int wide)
{
    char digits[10];
    uint32_t n = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int count = 0, pos = 0;
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    if (!wide && count + (value < 0) > SDK_ALPHA_VALUE_MAX) { buf[0] = '#'; buf[1] = 0; return; }
    if (value < 0) buf[pos++] = '-';
    while (count) buf[pos++] = digits[--count];
    buf[pos] = 0;
}

#ifdef dev_active
static void value_text(uint32_t k, int32_t raw, char *buf)
{
    const struct sdk_alpha_entry *e = dev_active();
    if (e && k < e->count)
        sdk_alpha_value(e, k, raw, buf, 0);
    else
        sdk_alpha_number(raw >> 8, buf, 0);
}
#define VALUE_TEXT(k) static void value_text##k(int32_t raw, char *buf) { value_text(k, raw, buf); }
VALUE_TEXT(0) VALUE_TEXT(1) VALUE_TEXT(2) VALUE_TEXT(3) VALUE_TEXT(4) VALUE_TEXT(5) VALUE_TEXT(6)
VALUE_TEXT(7) VALUE_TEXT(8) VALUE_TEXT(9) VALUE_TEXT(10) VALUE_TEXT(11) VALUE_TEXT(12) VALUE_TEXT(13)
VALUE_TEXT(14)
_Static_assert(SDK_ALPHA_KNOBS == 15, "one value_text per knob");
void (*const sdk_alpha_value_texts[SDK_ALPHA_KNOBS])(int32_t raw, char *buf) = {
    value_text0, value_text1, value_text2, value_text3, value_text4, value_text5, value_text6,
    value_text7, value_text8, value_text9, value_text10, value_text11, value_text12, value_text13,
    value_text14,
};
#endif

char sdk_alpha_labels[SDK_ALPHA_KNOBS][ELEK_UI_LABEL_MAX + 1];
uint8_t sdk_alpha_visibility[SDK_ALPHA_KNOBS];
const struct elek_glyph *sdk_alpha_glyphs[SDK_ALPHA_KNOBS];
const struct sdk_alpha_entry *sdk_alpha_seen;
int32_t sdk_alpha_seen_page = ELEK_UI_NO_PAGE;
static uint32_t seen_track;
static int32_t seen_raw[SDK_ALPHA_KNOBS];

static struct elek_scratch scratch;     /* the UI task's: one .present call at a time */

/* Each .present call starts from the declaration, with no text built. */
static void start(struct elek_view *view, const struct elek_machine *m, uint32_t n)
{
    for (uint32_t k = 0; k < n; ++k) {
        view[k].label = m->knobs[k].label;
        view[k].hidden = 0;
        view[k].glyph = m->knobs[k].glyph;
        view[k].value = 0;
    }
    scratch.used = 0;
}

/* text's first ELEK_UI_LABEL_MAX characters into out, nonprintable ones as ?; -> nonzero when out changed. */
static int32_t copy_text(char *out, const char *text)
{
    int32_t changed = 0;
    uint32_t c = 0;
    for (; c < ELEK_UI_LABEL_MAX && text[c]; ++c) {
        unsigned char ch = text[c];
        char next = ch >= 32 && ch <= 126 ? ch : '?';
        changed |= out[c] != next;
        out[c] = next;
    }
    changed |= out[c] != 0;
    out[c] = 0;
    return changed;
}

int32_t sdk_alpha_present(const struct sdk_alpha_entry *e, uint32_t track, int32_t page,
                          const int32_t *raw, int force)
{
    const struct elek_machine *m = e->machine;
    uint32_t n = e->count, k;
    int32_t clamped[SDK_ALPHA_KNOBS], knob[SDK_ALPHA_KNOBS];
    struct elek_view view[SDK_ALPHA_KNOBS];
    int32_t changed = 0;
    if (page < 0 || (uint32_t)page >= e->pages || !m->pages[page].label)
        page = ELEK_UI_NO_PAGE;     /* the blank page of a machine that declares none is no page of its own */
    for (k = 0; k < n; ++k)
        clamped[k] = raw[k] < 0 ? 0 : raw[k] > SDK_ALPHA_RAW_MAX ? SDK_ALPHA_RAW_MAX : raw[k];
    if (!force && e == sdk_alpha_seen && track == seen_track && page == sdk_alpha_seen_page) {
        for (k = 0; k < n && clamped[k] == seen_raw[k]; ++k)
            ;
        if (k == n)
            return 0;
    }
    for (k = 0; k < n; ++k) {
        seen_raw[k] = clamped[k];
        knob[k] = sdk_alpha_map(&m->knobs[k], clamped[k]);
    }
    start(view, m, n);
    sdk_alpha_seen = e;
    seen_track = track;
    sdk_alpha_seen_page = page;
    const struct elek_ui ui = { .knob = knob, .view = view, .page = page, .track = track, .scratch = &scratch };
    if (m->present)
        m->present(&ui);
    for (k = 0; k < n; ++k) {
        changed |= copy_text(sdk_alpha_labels[k], view[k].label ? view[k].label : m->knobs[k].label);
        uint8_t visible = view[k].hidden == 0;
        changed |= sdk_alpha_visibility[k] != visible;
        sdk_alpha_visibility[k] = visible;
        changed |= sdk_alpha_glyphs[k] != view[k].glyph;
        sdk_alpha_glyphs[k] = view[k].glyph;
    }
    return changed;
}

void sdk_alpha_value(const struct sdk_alpha_entry *e, uint32_t k, int32_t raw, char *buf, int wide)
{
    static int32_t knob[SDK_ALPHA_KNOBS];      /* static: formatters run deep in the firmware's UI stack */
    static struct elek_view view[SDK_ALPHA_KNOBS];
    const struct elek_machine *m = e->machine;
    int seen = e == sdk_alpha_seen;
    int32_t value = sdk_alpha_map(&m->knobs[k], raw);
    if (m->present) {
        for (uint32_t i = 0; i < e->count; ++i)
            knob[i] = sdk_alpha_map(&m->knobs[i], seen ? seen_raw[i] : sdk_alpha_default(&m->knobs[i]));
        knob[k] = value;
        start(view, m, e->count);
        const struct elek_ui ui = { .knob = knob, .view = view, .page = seen ? sdk_alpha_seen_page : ELEK_UI_NO_PAGE,
                                    .track = seen ? seen_track : 0, .scratch = &scratch };
        m->present(&ui);
        if (view[k].value && view[k].value[0]) {
            copy_text(buf, view[k].value);
            return;
        }
    }
    sdk_alpha_number(value, buf, wide);
}

const char *sdk_alpha_label(const struct sdk_alpha_entry *e, uint32_t k)
{
    if (k >= e->count)
        return 0;
    return e == sdk_alpha_seen ? sdk_alpha_labels[k] : e->machine->knobs[k].label;
}

const struct elek_glyph *sdk_alpha_glyph(const struct sdk_alpha_entry *e, uint32_t k)
{
    if (k >= e->count)
        return 0;
    return e == sdk_alpha_seen ? sdk_alpha_glyphs[k] : e->machine->knobs[k].glyph;
}

int32_t sdk_alpha_visible(const struct sdk_alpha_entry *e, uint32_t k)
{
    return k < e->count && (e != sdk_alpha_seen || sdk_alpha_visibility[k]);
}

void sdk_alpha_blit(void *bmp, int32_t x, int32_t y, const struct elek_glyph *glyph)
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
    uint8_t rendered;                   /* rendered since its state was cleared: idle means something */
    uint32_t state[ELEK_VOICE_STATE_MAX / 4];
} voices[SDK_ALPHA_VOICES];

void sdk_alpha_select(uint32_t slot, int32_t id)
{
    if (voices[slot].id == id) return;
    for (uint32_t i = 0; i < ELEK_VOICE_STATE_MAX / 4; ++i) voices[slot].state[i] = 0;
    voices[slot].id = id;
    voices[slot].start = 0;
    voices[slot].rendered = 0;
}

void sdk_alpha_voice(const struct sdk_alpha_entry *e, uint32_t slot, struct elek_voice *v,
                     const int32_t *raw, int start, uint32_t idle, elek_sample *out)
{
    int32_t knob[SDK_ALPHA_KNOBS];
    if (start) voices[slot].start = 1;
    for (uint32_t k = 0; k < e->count; ++k)
        knob[k] = sdk_alpha_map(&e->machine->knobs[k], raw[k]);
    v->knob = knob;
    v->start = voices[slot].start;
    v->state = voices[slot].state;
    v->idle = voices[slot].rendered ? idle : 0;
    e->machine->render(v, out);
    voices[slot].start = 0;
    voices[slot].rendered = 1;
}
#endif
