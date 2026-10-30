/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private experiment: Digitakt mk1 OS 1.54 only. */
#ifndef ELEKLOADER_SDK_ALPHA_MACHINE_H
#define ELEKLOADER_SDK_ALPHA_MACHINE_H

#include "digitakt-mk1/core3.h"

#define ELEK_UI_NO_PAGE (-1)
#define ELEK_UI_RAW_MAX (127 << 8) /* Native 8.8 units: 0..32512, independent of mapped bounds. */
#define ELEK_UI_BOUND(v) { .value = (v), .supplied = 1 }
enum { ELEK_ENCODER_A, ELEK_ENCODER_B, ELEK_ENCODER_C, ELEK_ENCODER_D,
       ELEK_ENCODER_E, ELEK_ENCODER_F, ELEK_ENCODER_G, ELEK_ENCODER_H };

/* An omitted bound is distinct from ELEK_UI_BOUND(0). Defaults: min 0, max 127. */
struct elek_ui_bound { int32_t value; uint32_t supplied; };
struct elek_ui_knob {
    uint32_t id;                   /* machine-wide logical ID, contiguous from zero */
    const char *label;             /* 1..8 printable ASCII characters */
    struct elek_ui_bound min, max;
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
    int32_t page;                  /* zero-based displayed page, or ELEK_UI_NO_PAGE */
    uint32_t page_count, knob_count;
    const struct elek_ui_value *const *knob; /* indexed by machine-wide logical ID, all pages */
};
struct elek_ui_presentation {
    uint32_t knob_count;
    void *knob;                    /* null; presentation assignments belong to the next slice */
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
extern const char sdk_alpha_dt1_empty_ui[], sdk_alpha_dt1_ui_v3[];

#define ELEK_REGISTER_MACHINE(machine) \
    static const struct sdk_alpha_machine_ui sdk_alpha_ui_##machine = { \
        sdk_alpha_dt1_ui_v3, &(machine) \
    }; \
    const struct cm_machine sdk_alpha_registration_##machine = { \
        .id = (machine).id, .name = (machine).name, .sname = (machine).short_name, \
        .params = 3, .render = (machine).id, .tag = CM_UI_TAG, \
        .ui = (const struct cm_ui *)&sdk_alpha_ui_##machine \
    }

#endif
