/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private experiment: Digitakt mk1 OS 1.54 and Digitone mk1 OS 1.44 (UI and audio); Digitone II
 * OS 1.12 (UI only). The builder refuses audio on targets without a render backend. */
#ifndef ELEKLOADER_SDK_ALPHA_MACHINE_H
#define ELEKLOADER_SDK_ALPHA_MACHINE_H

#include <stdint.h>

#define ELEK_UI_NO_PAGE (-1)
#define ELEK_UI_LABEL_MAX 5   /* a knob label's characters: what fits under a knob on the Digitakt and Digitone */
#define ELEK_UI_RAW_MAX (127 << 8) /* Native 8.8 units: 0..32512, independent of mapped bounds. */
enum { ELEK_A, ELEK_B, ELEK_C, ELEK_D, ELEK_E, ELEK_F, ELEK_G, ELEK_H };   /* the encoders */

/* 16 x 16 monochrome pixels: top row first, bit 15 leftmost; 1 lights a pixel.
 * Glyphs must remain readable while displayed (normally static const storage). */
struct elek_glyph { uint16_t rows[16]; };
/* A knob: its index in the machine's .knobs is its id. Every knob is an LFO target; presentation
 * labels never change its id. */
struct elek_knob {
    const char *label;             /* 1..ELEK_UI_LABEL_MAX printable ASCII characters */
    int32_t min, max;              /* the mapped range; max is required and above min */
    int32_t def;                   /* the value it starts at, clamped into min..max (omitted: 0) */
    const struct elek_glyph *glyph; /* null uses the stock encoder graphic */
};
struct elek_page {
    const char *label;             /* 1..16 printable ASCII characters; a page without one is absent */
    const struct elek_knob *encoder[8]; /* &knobs[ID] or null for an empty position; pages may share */
};
struct elek_ui_value {
    int32_t value;                 /* signed integer, nearest; exact halves round toward max */
    int32_t raw_value;             /* native 8.8, 0..ELEK_UI_RAW_MAX */
};
struct elek_ui_context {
    int32_t page;                  /* displayed machine page, or ELEK_UI_NO_PAGE (e.g. LFO labels) */
    uint32_t page_count, knob_count;
    const struct elek_ui_value *const *knob; /* indexed by machine-wide logical ID, all pages */
    uint32_t track;                /* the track this call is for, from zero: not always the active one */
};
/* Labels may use literals or caller-owned storage readable after callback return.
 * The SDK copies labels immediately: at most ELEK_UI_LABEL_MAX characters, nonprintable bytes become ?.
 * Automatic callback-local arrays have expired by then; use static/caller-owned buffers. */
struct elek_ui_knob_presentation {
    const char *label;
    uint32_t visible;              /* defaults to 1; zero blanks every position, retaining its value */
    const struct elek_glyph *glyph; /* descriptor's default each call; null selects stock */
};
struct elek_ui_presentation {
    uint32_t knob_count;
    struct elek_ui_knob_presentation *const *knob; /* logical IDs; defaults reset each call */
};

/* Sound. render fills one voice's block. The same C builds for every device: samples are the
 * device's own format, so touch them only through the helpers below, write constants with
 * ELEK_SAMPLE, and do not divide them. ELEK_TARGET_SHARC is set when the SDK builds for the SHARC. */
#define ELEK_SAMPLE_RATE 48000
#define ELEK_VOICE_FRAMES 32          /* samples a render call */
#define ELEK_VOICE_STATE_MAX 256      /* the bytes of a voice's state */
#ifdef ELEK_TARGET_SHARC
typedef float elek_sample;            /* -1.0..1.0 */
#define ELEK_SAMPLE(x) ((float)(x))
static inline elek_sample elek_mul(elek_sample a, elek_sample b) { return a * b; }
static inline elek_sample elek_add(elek_sample a, elek_sample b) { return a + b; }
static inline elek_sample elek_clip(elek_sample a) { return a > 1.0f ? 1.0f : a < -1.0f ? -1.0f : a; }
#else
typedef int32_t elek_sample;          /* Q31 */
/* A constant only: a variable here would need floating point, which the ColdFire lacks. */
#define ELEK_SAMPLE(x) ((int32_t)((x) >= 1.0 ? 2147483647.0 : (x) <= -1.0 ? -2147483648.0 : (x) * 2147483648.0))
/* Q31 x Q31, rounded to nearest (halves away from zero), saturating; no 64-bit runtime. */
static inline elek_sample elek_mul(elek_sample a, elek_sample b)
{
    uint32_t ua = a < 0 ? 0u - (uint32_t)a : (uint32_t)a, ub = b < 0 ? 0u - (uint32_t)b : (uint32_t)b;
    uint32_t al = ua & 0xffff, ah = ua >> 16, bl = ub & 0xffff, bh = ub >> 16;
    uint32_t ll = al * bl, lh = al * bh, hl = ah * bl;
    uint32_t mid = (ll >> 16) + (lh & 0xffff) + (hl & 0xffff);
    uint32_t hi = ah * bh + (lh >> 16) + (hl >> 16) + (mid >> 16), lo = mid << 16 | (ll & 0xffff);
    uint32_t r = (hi << 1 | lo >> 31) + (lo >> 30 & 1);
    if ((a ^ b) < 0)
        return (int32_t)(0u - r);
    return r > 0x7fffffffu ? 0x7fffffff : (int32_t)r;
}
static inline elek_sample elek_add(elek_sample a, elek_sample b)
{
    int32_t s = (int32_t)((uint32_t)a + (uint32_t)b);
    return ((a ^ s) & (b ^ s)) < 0 ? (a < 0 ? INT32_MIN : INT32_MAX) : s;
}
static inline elek_sample elek_clip(elek_sample a) { return a; }   /* Q31 cannot exceed full scale */
#endif
/* value 0..max as a level, 0 to full scale (values outside are clamped); max > 0. */
static inline elek_sample elek_level(int32_t value, int32_t max)
{
    if (value < 0) value = 0;
    if (value > max) value = max;
#ifdef ELEK_TARGET_SHARC
    return (float)value / (float)max;
#else
    return (int32_t)(0x7fffffffu / (uint32_t)max) * value;
#endif
}
/* A phase step a sample (2^32 a cycle) at ELEK_SAMPLE_RATE for a pitch in semitones, 16.16, MIDI
 * numbering (69 << 16 is 440 Hz), clamped to notes 0..127. Integer only: the same on every device,
 * within 0.05 cent. */
static inline uint32_t elek_phase_inc(int32_t pitch)
{
    static const uint32_t step[49] = {   /* note 0 upward, a quarter semitone apart */
        731558u, 742199u, 752994u, 763947u, 775059u, 786332u, 797770u,
        809374u, 821146u, 833090u, 845208u, 857501u, 869974u, 882628u,
        895466u, 908491u, 921705u, 935112u, 948713u, 962513u, 976513u,
        990717u, 1005127u, 1019747u, 1034579u, 1049628u, 1064895u, 1080384u,
        1096099u, 1112042u, 1128217u, 1144627u, 1161276u, 1178167u, 1195304u,
        1212690u, 1230329u, 1248225u, 1266381u, 1284800u, 1303488u, 1322448u,
        1341683u, 1361199u, 1380998u, 1401085u, 1421464u, 1442140u, 1463116u,
    };
    if (pitch < 0) pitch = 0;
    if (pitch > 127 << 16) pitch = 127 << 16;
    uint32_t q = (uint32_t)pitch >> 14, k = q % 48, f = (uint32_t)pitch & 0x3fff;
    return (step[k] + (((step[k + 1] - step[k]) * f) >> 14)) << (q / 48);
}
struct elek_voice {
    const int32_t *knob;           /* by id: this voice's values, mapped, with its locks and LFOs */
    uint32_t track;                /* the track that played it, from zero */
    uint32_t start;                /* nonzero in a note's first block */
    int32_t pitch;                 /* semitones, 16.16, MIDI numbering */
    int32_t velocity;              /* 1..127; the Digitone's AMP already applies it: use it for timbre */
    void *state;                   /* ELEK_VOICE_STATE_MAX bytes, zeroed at boot and when the voice changes
                                    * machine, kept between calls and notes: read it with ELEK_STATE */
};
/* The voice's state as a type*, which must fit in ELEK_VOICE_STATE_MAX bytes (a compile error if not). */
#define ELEK_STATE(type, v) \
    ((void)sizeof(char[sizeof(type) <= ELEK_VOICE_STATE_MAX ? 1 : -1]), (type *)(v)->state)

struct elek_machine {
    int32_t id;                    /* native machine slot, 4..127 (Digitone II: 5..127); declared only here */
    const char *name, *short_name; /* 1..16 and 1..4 ASCII characters */
    const struct elek_knob *knobs; /* at most fifteen: give them with ELEK_KNOBS */
    uint32_t knob_count;
    struct elek_page pages[2];     /* a page without a label is absent: the first, if any, has one */
/* Called when the machine, track, page or a value changed, and maybe again with the same inputs:
     * keep it pure. ctx and its values are read-only snapshots, valid only during this call. */
    void (*present)(const struct elek_ui_context *, struct elek_ui_presentation *);
    /* Optional: the machine's sound, one voice at a time (without it the device's own plays).
     * It may read only its arguments, its state and constant data. */
    void (*render)(const struct elek_voice *, elek_sample *out);
};

#define ELEK_KNOBS(k) .knobs = (k), .knob_count = sizeof(k) / sizeof((k)[0])

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
extern const char sdk_alpha_dt1_ui_v1[];

#define ELEK_REGISTER_MACHINE(machine) \
    static const struct sdk_alpha_machine_ui sdk_alpha_ui_##machine = { \
        sdk_alpha_dt1_ui_v1, &(machine) \
    }; \
    const struct sdk_alpha_registration sdk_alpha_registration_##machine = { \
        .id = (machine).id, .name = (machine).name, .sname = (machine).short_name, \
        .params = 3, .render = (machine).id, .tag = SDK_ALPHA_UI_TAG, \
        .ui = &sdk_alpha_ui_##machine \
    }

#endif
