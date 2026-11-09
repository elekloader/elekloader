/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK's machine interface: Digitakt mk1 OS 1.54 and Digitone mk1 OS 1.44 (UI and audio), Digitone II
 * OS 1.12 (UI only). The builder refuses audio on targets without a render backend. */
#ifndef ELEKLOADER_SDK_ALPHA_MACHINE_H
#define ELEKLOADER_SDK_ALPHA_MACHINE_H

#include <stdint.h>

/* The limits, which the builder checks (tests/test_sdk_alpha.py keeps its copy in step). */
#define ELEK_UI_LABEL_MAX 5        /* a knob label's characters: what fits under a knob */
#define ELEK_UI_KNOB_MAX 15        /* a machine's knobs */
#define ELEK_UI_PAGE_MAX 2         /* a machine's pages */
#define ELEK_UI_PAGE_LABEL_MAX 16  /* a page label's characters */
#define ELEK_UI_NAME_MAX 16        /* a machine name's characters */
#define ELEK_UI_SHORT_NAME_MAX 4   /* a machine short name's characters */
#define ELEK_UI_NO_PAGE (-1)
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
    int32_t clicks;                /* encoder clicks from one value to the next; 0 spaces the values
                                    * evenly over the encoder's 128 positions. A knob with more than
                                    * 128 values takes none; clicks x values is at most 128 */
};
struct elek_page {
    const char *label;             /* 1..ELEK_UI_PAGE_LABEL_MAX printable ASCII characters; a page without
                                    * one is absent */
    const struct elek_knob *encoder[8]; /* &knobs[ID] or null for an empty position; pages may share */
};
/* What .present shows for one knob. Each call starts from the knob's declaration: its label, shown,
 * its glyph and no value text. Assign text: a string literal, constant data, or elek_num's or
 * elek_fixed's. At most ELEK_UI_LABEL_MAX characters show; nonprintable ones show as ?. */
struct elek_view {
    const char *label;             /* null shows the declared label */
    uint32_t hidden;               /* nonzero blanks the knob on every page; it keeps its value */
    const struct elek_glyph *glyph; /* null selects the stock graphic; must stay readable */
    const char *value;             /* the text for ui->knob[ID], under the knob and in the pop-up; null or
                                    * empty shows the number */
};
#define ELEK_TEXT_MAX 32        /* the texts elek_num and elek_fixed build in one call */
/* Private: where elek_num and elek_fixed build text, kept until the companion has read the call's views. */
struct elek_scratch {
    uint32_t used;
    char text[ELEK_TEXT_MAX][ELEK_UI_LABEL_MAX + 1];
};
struct elek_ui {
    const int32_t *knob;           /* by id, mapped to min..max: every knob, on every page */
    struct elek_view *view;        /* by id */
    int32_t page;                  /* the page shown, or ELEK_UI_NO_PAGE (e.g. naming LFO destinations),
                                    * also for machines with pages: check before indexing */
    uint32_t track;                /* the track this call is for, from zero: not always the active one */
    struct elek_scratch *scratch;  /* private: elek_num's and elek_fixed's */
};
/* prefix then v / 10^places with places decimals (0..9), as text for a view: elek_fixed(ui, "", 24, 1) is
 * "2.4". The prefix is cut to fit the number; "#" when the number alone needs more than ELEK_UI_LABEL_MAX
 * characters, or past ELEK_TEXT_MAX texts in one call. */
static inline const char *elek_fixed(const struct elek_ui *ui, const char *prefix, int32_t v, int places)
{
    char digits[12], *out;
    uint32_t n = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    int count = 0, pos = 0, size;
    if (places < 0) places = 0;
    if (places > 9) places = 9;
    do digits[count++] = (char)('0' + n % 10); while ((n /= 10) || count <= places);
    size = count + (places > 0) + (v < 0);
    if (size > ELEK_UI_LABEL_MAX || ui->scratch->used >= ELEK_TEXT_MAX)
        return "#";
    out = ui->scratch->text[ui->scratch->used++];
    while (pos < ELEK_UI_LABEL_MAX - size && prefix[pos]) { out[pos] = prefix[pos]; ++pos; }
    if (v < 0) out[pos++] = '-';
    while (count--) {
        if (count + 1 == places) out[pos++] = '.';
        out[pos++] = digits[count];
    }
    out[pos] = 0;
    return out;
}
/* prefix then v: elek_num(ui, "+", 7) is "+7". As elek_fixed with no decimals. */
static inline const char *elek_num(const struct elek_ui *ui, const char *prefix, int32_t v)
{
    return elek_fixed(ui, prefix, v, 0);
}

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
static inline elek_sample elek_from_q15(int32_t v)
{
    return (v < -32768 ? -32768 : v > 32767 ? 32767 : v) / 32768.0f;
}
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
/* A signed Q15 integer, -32768..32767 (clamped), as a sample: -32768 is -1.0. */
static inline elek_sample elek_from_q15(int32_t v)
{
    return (int32_t)((uint32_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v) << 16);
}
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
    uint32_t idle;                 /* frames the voice was not rendered (silent) since its last render, up to
                                    * UINT32_MAX; 0 in the first render after its state is zeroed */
    /* cc[n], n 0..127: the value, 0..127, of MIDI CC n if one came on the voice's track since this voice's
     * last render (while it was silent too), else -1. Several of the same CC read as the last one; which
     * of two different CCs came first is not kept. The stock applies the CC as it would anyway. Valid
     * during the call only. */
    const int32_t *cc;
};
/* The voice's state as a type*, which must fit in ELEK_VOICE_STATE_MAX bytes (a compile error if not). */
#define ELEK_STATE(type, v) \
    ((void)sizeof(char[sizeof(type) <= ELEK_VOICE_STATE_MAX ? 1 : -1]), (type *)(v)->state)

/* Mono machines (.play and .prio): the Digitone II's TRK SETUP PLAY MODE and MONO NOTE PRIO. */
enum { ELEK_POLY, ELEK_MONO, ELEK_MONO_LEGATO };          /* .play */
enum { ELEK_PRIO_LAST, ELEK_PRIO_LOW, ELEK_PRIO_HIGH };   /* .prio, for ELEK_MONO and ELEK_MONO_LEGATO */

struct elek_machine {
    int32_t id;                    /* native machine slot, 4..127 (Digitone II: 5..127); declared only here */
    const char *name, *short_name; /* 1..ELEK_UI_NAME_MAX and 1..ELEK_UI_SHORT_NAME_MAX ASCII characters */
    const struct elek_knob *knobs; /* at most ELEK_UI_KNOB_MAX: give them with ELEK_KNOBS */
    uint32_t knob_count;
    struct elek_page pages[ELEK_UI_PAGE_MAX]; /* a page without a label is absent: the first, if any, has one */
    /* Optional: called when the machine, track, page or a value changed, and maybe again with the same
     * inputs: keep it pure. ui and its arrays are valid only during the call. */
    void (*present)(const struct elek_ui *ui);
    /* Optional: the machine's sound, one voice at a time (without it the device's own plays).
     * It may read only its arguments, its state and constant data. */
    void (*render)(const struct elek_voice *, elek_sample *out);
    /* Optional: how the track plays live keys. ELEK_POLY (0): every key, as the device allocates voices.
     * ELEK_MONO: one note at a time, the held key .prio chooses, the AMP restarted at each change.
     * ELEK_MONO_LEGATO: the same, but a change while a note sounds moves its pitch, the AMP running on. */
    uint8_t play, prio;
};

#define ELEK_KNOBS(k) .knobs = (k), .knob_count = sizeof(k) / sizeof((k)[0])

/* Registers a machine, at file scope: the builder adds it to the device's companion. */
#define ELEK_REGISTER_MACHINE(machine) \
    static const char *const sdk_alpha_abi_##machine __attribute__((used)) = sdk_alpha_abi_v1; \
    const struct elek_machine *const sdk_alpha_registration_##machine = &(machine)

/* Private: what ELEK_REGISTER_MACHINE needs. Every companion exports the ABI symbol, so a machine built
 * for another ABI fails to link. */
extern const char sdk_alpha_abi_v1[];

/* The layout sdk_alpha_build reads from a ColdFire build (tests/test_sdk_alpha.py keeps its copy in step).
 * A host build, with 8-byte pointers, lays them out differently. */
#if __SIZEOF_POINTER__ == 4
#define ELEK_OFFSET(type, field, n) _Static_assert(__builtin_offsetof(struct type, field) == (n), #type "." #field)
#define ELEK_SIZE(type, n) _Static_assert(sizeof(struct type) == (n), "sizeof " #type)
ELEK_OFFSET(elek_machine, id, 0);
ELEK_OFFSET(elek_machine, name, 4);
ELEK_OFFSET(elek_machine, short_name, 8);
ELEK_OFFSET(elek_machine, knobs, 12);
ELEK_OFFSET(elek_machine, knob_count, 16);
ELEK_OFFSET(elek_machine, pages, 20);
ELEK_OFFSET(elek_machine, render, 96);
ELEK_OFFSET(elek_machine, play, 100);
ELEK_OFFSET(elek_machine, prio, 101);
ELEK_SIZE(elek_machine, 102);
ELEK_OFFSET(elek_page, label, 0);
ELEK_OFFSET(elek_page, encoder, 4);
ELEK_SIZE(elek_page, 36);
ELEK_OFFSET(elek_knob, label, 0);
ELEK_OFFSET(elek_knob, min, 4);
ELEK_OFFSET(elek_knob, max, 8);
ELEK_OFFSET(elek_knob, def, 12);
ELEK_OFFSET(elek_knob, glyph, 16);
ELEK_OFFSET(elek_knob, clicks, 20);
ELEK_SIZE(elek_knob, 24);
ELEK_SIZE(elek_glyph, 32);
#undef ELEK_OFFSET
#undef ELEK_SIZE
#endif

#endif
