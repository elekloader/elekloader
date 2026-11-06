/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private experiment: Digitakt mk1 OS 1.54 and Digitone mk1 OS 1.44 (UI and audio); Digitone II
 * OS 1.12 (UI only). The builder refuses audio on targets without a render backend. */
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
    struct elek_ui_bound def;      /* optional mapped default, min when omitted */
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
    int32_t page;                  /* displayed machine page, or ELEK_UI_NO_PAGE (e.g. LFO labels),
                                    * also for machines with pages: check before indexing */
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
    const struct elek_ui_glyph *glyph; /* descriptor's default each call; null selects stock.
                                        * Drawn after return: never automatic storage */
};
struct elek_ui_presentation {
    uint32_t knob_count;
    struct elek_ui_knob_presentation *const *knob; /* logical IDs; defaults reset each call */
    void *shared;                  /* this track's writable shared snapshot; null without .shared */
};

/* Sound. render fills one voice's block. The same C builds for every device: samples are the
 * device's own format, so touch them only through the helpers below, write constants with
 * ELEK_SAMPLE, and do not divide them. ELEK_TARGET_SHARC is set when the SDK builds for the SHARC. */
#define ELEK_SAMPLE_RATE 48000
#define ELEK_VOICE_FRAMES 32          /* samples a render call */
#define ELEK_VOICE_START 1u           /* elek_voice.flags: the note's first block */
#define ELEK_VOICE_STATE_MAX 256      /* .voice_state's limit: bytes a voice keeps between calls */
#define ELEK_SHARED_MAX 64            /* .shared's limit: bytes a track's block holds */
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
    uint32_t slot;                 /* the voice, 0-based: not the track */
    uint32_t track;                /* the track that played it, from zero */
    int32_t pitch;                 /* semitones, 16.16, MIDI numbering */
    int32_t velocity;              /* 1..127 */
    uint32_t gate;                 /* held note; DT1 currently reports AMP active (docs/SDK.md) */
    uint32_t flags;                /* ELEK_VOICE_START */
    void *state;                   /* zeroed at boot/machine change; kept between calls and notes */
};
struct elek_audio_context {
    uint32_t knob_count;
    const int32_t *knob;           /* by logical ID: this voice's values, mapped to the knob's bounds */
    const void *shared;            /* the voice's track's .shared block (written by .present), or null */
};

struct elek_machine {
    int32_t id;                    /* native machine slot, 4..127 (Digitone II: 5..127); declared only here */
    const char *name, *short_name; /* 1..16 and 1..4 ASCII characters */
    const struct elek_ui_page *pages; /* at most two pages and fifteen logical knobs */
    uint32_t page_count;
    uint32_t _reserved[2];         /* zero */
    /* ctx and its values are read-only snapshots, valid only during this call. */
    void (*present)(const struct elek_ui_context *, struct elek_ui_presentation *);
    /* Optional: the machine's sound, one voice at a time (without it the device's own plays).
     * It may read only its arguments, its state, .shared and constant data. */
    void (*render)(const struct elek_audio_context *, const struct elek_voice *, elek_sample *out);
    uint32_t voice_state;          /* 0..ELEK_VOICE_STATE_MAX bytes a voice keeps; needs render */
    uint32_t shared;               /* 0..ELEK_SHARED_MAX bytes a track shares with render; needs render */
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
