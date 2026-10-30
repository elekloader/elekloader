/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private adapter onto machine-pages' existing two-page/fifteen-value backend. */
#include "elekloader/sdk_alpha_machine.h"
#include "src.h"

const char sdk_alpha_dt1_empty_ui[] = "sdk alpha dt1 empty";
const char sdk_alpha_dt1_ui_v3[] = "sdk alpha dt1 pages 3";
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
static char sdk_alpha_labels[15][9];
static uint8_t sdk_alpha_visibility[15];

const struct elek_machine *sdk_alpha_machine(int32_t id)
{
    const struct sdk_alpha_machine_ui *u = (const void *)core_machine_ui(id);
    if (!u || (u->abi != sdk_alpha_dt1_empty_ui && u->abi != sdk_alpha_dt1_ui_v3))
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
        if (k < (uint32_t)knob_count)
            n->name = n->lname = knobs[k]->label;
        else
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
    struct elek_ui_knob_presentation presentation[15];
    struct elek_ui_knob_presentation *output[15];
    struct elek_ui_value values[15];
    const struct elek_ui_value *knobs[15];
    for (uint32_t k = 0; k < (uint32_t)knob_count; ++k) {
        uint8_t locked;
        uint32_t id = k < 8 ? 0x84 + k : MP_EXTRA_ID + k - 8;
        int32_t raw = mp_shown_value(view, id, &locked);
        if (raw < 0) raw = 0;
        if (raw > ELEK_UI_RAW_MAX) raw = ELEK_UI_RAW_MAX;
        values[k].raw_value = raw;
        values[k].value = sdk_alpha_map(descriptors[k], raw);
        knobs[k] = &values[k];
        presentation[k].label = descriptors[k]->label;
        presentation[k].visible = 1;
        output[k] = &presentation[k];
    }
    const struct elek_ui_context ctx = {
        .page = page, .page_count = m->page_count, .knob_count = knob_count,
        .knob = knob_count ? knobs : 0,
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
        for (; n < 8 && label[n]; ++n) {
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
