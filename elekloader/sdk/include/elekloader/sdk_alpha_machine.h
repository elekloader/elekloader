/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private experiment: Digitakt 1.54, empty machines only. */
#ifndef ELEKLOADER_SDK_ALPHA_MACHINE_H
#define ELEKLOADER_SDK_ALPHA_MACHINE_H

#include "digitakt-mk1/core3.h"

#define ELEK_UI_NO_PAGE (-1)

struct elek_ui_context {
    int32_t page;                   /* ELEK_UI_NO_PAGE for an empty machine */
    uint32_t page_count, knob_count;
    const void *knob;              /* null; controls belong to the next slice */
};

struct elek_ui_presentation {
    uint32_t knob_count;
    void *knob;                    /* null */
};

struct elek_machine {
    int32_t id;                     /* native machine slot, 4..127; declared only here */
    const char *name, *short_name;  /* 1..16 and 1..4 ASCII characters */
    const void *pages;
    uint32_t page_count;
    const void *knobs;
    uint32_t knob_count;
    void (*present)(const struct elek_ui_context *, struct elek_ui_presentation *);
};

/* Private adapter; the existing core descriptor and UI layouts stay intact. */
struct sdk_alpha_machine_ui {
    const void *abi;
    const struct elek_machine *machine;
};
extern const char sdk_alpha_dt1_empty_ui[];

#define ELEK_REGISTER_MACHINE(machine) \
    static const struct sdk_alpha_machine_ui sdk_alpha_ui_##machine = { \
        sdk_alpha_dt1_empty_ui, &(machine) \
    }; \
    const struct cm_machine sdk_alpha_registration_##machine = { \
        .id = (machine).id, .name = (machine).name, .sname = (machine).short_name, \
        .params = 3, .render = (machine).id, .tag = CM_UI_TAG, \
        .ui = (const struct cm_ui *)&sdk_alpha_ui_##machine \
    }

#endif
