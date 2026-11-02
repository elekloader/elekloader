/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private experiment: Digitakt mk1 OS 1.54 and Digitone mk1 OS 1.44 (behind SYN2 on the Digitone). */
#ifndef ELEKLOADER_SDK_ALPHA_MACHINE_H
#define ELEKLOADER_SDK_ALPHA_MACHINE_H

#include <stdint.h>

#define ELEK_UI_NO_PAGE (-1)
#define ELEK_UI_LABEL_MAX 5   /* a knob label's characters: what fits under a knob on the Digitakt and Digitone */
#define ELEK_UI_RAW_MAX (127 << 8) /* Native 8.8 units: 0..32512, independent of mapped bounds. */
#define ELEK_UI_BOUND(v) { .value = (v), .supplied = 1 }
enum { ELEK_ENCODER_A, ELEK_ENCODER_B, ELEK_ENCODER_C, ELEK_ENCODER_D,
       ELEK_ENCODER_E, ELEK_ENCODER_F, ELEK_ENCODER_G, ELEK_ENCODER_H };

/* An omitted bound is distinct from ELEK_UI_BOUND(0). Defaults: min 0, max 127. */
struct elek_ui_bound { int32_t value; uint32_t supplied; };
/* 16 x 16 monochrome pixels: top row first, bit 15 leftmost; 1 lights a pixel.
 * Glyphs must remain readable while displayed (normally static const storage). */
struct elek_ui_glyph { uint16_t rows[16]; };
/* Every logical knob is an LFO target; presentation labels never change its ID. */
struct elek_ui_knob {
    uint32_t id;                   /* machine-wide logical ID, contiguous from zero */
    const char *label;             /* 1..ELEK_UI_LABEL_MAX printable ASCII characters (sdk_alpha_build refuses more) */
    struct elek_ui_bound min, max;
    const struct elek_ui_glyph *glyph; /* null uses the stock encoder graphic */
};
struct elek_ui_page {
    const char *label;             /* 1..16 printable ASCII characters */
    const struct elek_ui_knob *knob[8]; /* shared knob descriptors; null is empty */
};
struct elek_ui_value {
    int32_t value;                 /* signed integer, nearest; exact halves round toward max */
    int32_t raw_value;             /* native 8.8, 0..ELEK_UI_RAW_MAX */
};
struct elek_ui_context {
    int32_t page;                  /* displayed SRC page, or ELEK_UI_NO_PAGE (e.g. LFO labels) */
    uint32_t page_count, knob_count;
    const struct elek_ui_value *const *knob; /* indexed by machine-wide logical ID, all pages */
    uint32_t track;                /* the track this call is for (the active track), from zero */
};
/* Labels may use literals or caller-owned storage readable after callback return.
 * The SDK copies labels immediately: at most ELEK_UI_LABEL_MAX characters, nonprintable bytes become ?.
 * Automatic callback-local arrays have expired by then; use static/caller-owned buffers. */
struct elek_ui_knob_presentation {
    const char *label;
    uint32_t visible;              /* defaults to 1; zero blanks every position, retaining its value */
    const struct elek_ui_glyph *glyph; /* descriptor's default each call; null selects stock */
};
struct elek_ui_presentation {
    uint32_t knob_count;
    struct elek_ui_knob_presentation *const *knob; /* logical IDs; defaults reset each call */
};
struct elek_machine {
    int32_t id;                    /* native machine slot, 4..127; declared only here */
    const char *name, *short_name; /* 1..16 and 1..4 ASCII characters */
    const struct elek_ui_page *pages; /* at most two pages and fifteen logical knobs */
    uint32_t page_count;
    uint32_t _reserved[2];         /* zero; preserves the original empty-machine ABI */
    /* ctx and its values are read-only snapshots, valid only during this call. */
    void (*present)(const struct elek_ui_context *, struct elek_ui_presentation *);
};

/* Private adapter; the existing core descriptor and UI layouts stay intact. */
struct sdk_alpha_machine_ui {
    const void *abi;
    const struct elek_machine *machine;
};
/* The registration the builder finds: the Digitakt's struct cm_machine layout, which machine-pages
 * reads as is; another device's backend reads id, name and ui. No device header, so one source builds
 * for every device. */
#define SDK_ALPHA_UI_TAG 0x55493330u   /* CM_UI_TAG */
struct sdk_alpha_registration {
    int32_t id;
    const char *name, *sname;
    const void *icon;
    int32_t params, render;
    uint32_t tag;
    const struct sdk_alpha_machine_ui *ui;
};
extern const char sdk_alpha_dt1_empty_ui[], sdk_alpha_dt1_ui_v3[], sdk_alpha_dt1_ui_v4[];

#define ELEK_REGISTER_MACHINE(machine) \
    static const struct sdk_alpha_machine_ui sdk_alpha_ui_##machine = { \
        sdk_alpha_dt1_ui_v4, &(machine) \
    }; \
    const struct sdk_alpha_registration sdk_alpha_registration_##machine = { \
        .id = (machine).id, .name = (machine).name, .sname = (machine).short_name, \
        .params = 3, .render = (machine).id, .tag = SDK_ALPHA_UI_TAG, \
        .ui = &sdk_alpha_ui_##machine \
    }

#endif
