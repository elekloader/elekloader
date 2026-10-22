/* SPDX-License-Identifier: GPL-2.0-or-later */
/* linked-knobs: a knob whose graphic depends on another knob (docs/ADAPTING.md, "Machine pages"). LINK is
 * silent; its SHAPE knob (D) draws a ramp, or a pulse as wide as SHAPE, by its FORM knob (C). A draw gets
 * only its own value, so it asks machine-pages for FORM's with cm_src_shown (machine-pages 1.5): the
 * value the page shows, a held step's lock or a sound lock included.
 *
 * It names no firmware address, so its 1.54 port is empty. Machine 121: ids 120-127 are for examples and
 * experiments. */
#include "digitakt-mk1/core3.h"

#define LINK_ID     121
#define K_FORM      2                   /* knob C: BR's id, 0-1 */
#define K_SHAPE     3                   /* knob D: SAMP's id, 0-127 */

/* What SHAPE's draw read last: FORM, its own value, its own value through cm_src_shown, and a count. The
 * probe reads it (tests/probe_digitakt_shown.py); a machine needs no such thing. */
volatile int32_t link_seen[4];

static int32_t form_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    const char *s = value >= 0x80 ? "PULS" : "RAMP";
    (void)ctx;
    (void)machine;
    for (int i = 0; (buf[i] = s[i]); i++)
        ;
    return 1;
}

static int32_t draw_shape(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    int32_t form = cm_src_shown(K_FORM), width = 1 + (value >> 8) * 14 / 127;
    (void)flag;
    link_seen[0] = form;
    link_seen[1] = value;
    link_seen[2] = cm_src_shown(K_SHAPE);
    link_seen[3]++;
    if (form == CM_NO_VALUE)
        return 0;                                       /* not drawn by the page: the stock graphic */
    if (form < 0x80) {                                  /* a ramp, rising across the knob */
        for (int c = 0; c < 16; c++)
            fw_fillrect(bmp, x + 1 + c, y + 1, x + 1 + c, y + 1 + c * 14 / 15, 1);
    } else {                                            /* a pulse, high for `width` columns */
        fw_fillrect(bmp, x + 1, y + 15, x + width, y + 15, 1);
        fw_fillrect(bmp, x + width, y + 1, x + width, y + 15, 1);
        fw_fillrect(bmp, x + width, y + 1, x + 16, y + 1, 1);
    }
    return 1;
}

static const struct cm_ui link_page = {
    .abi = cm_ui_v31,                                   /* a knob that draws itself (CM_DRAW) */
    .page_from = 3,
    .knob = {
        [1] = { .flags = CM_HIDDEN },
        [K_FORM] = { .name = "FORM", .lname = "Form", .flags = CM_RANGE, .max = 1 << 8, .fmt = form_text },
        [K_SHAPE] = { .name = "SHAP", .lname = "Shape", .look = 0x86,
                      .flags = CM_RANGE | CM_NOT_SAMPLE | CM_DRAW, .max = 127 << 8, .draw = draw_shape },
        [4] = { .flags = CM_HIDDEN },
        [5] = { .flags = CM_HIDDEN },
        [6] = { .flags = CM_HIDDEN },
    },
};

/* params 3 (SLICE's parameters) and render its own id: a voice window nothing fills, so it is silent. */
const struct cm_machine link_machine = { LINK_ID, "LINKED", "LINK", 0, 3, LINK_ID, CM_UI_TAG, &link_page };
