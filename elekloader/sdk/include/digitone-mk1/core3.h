/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-dn1 3.0 to 3.3 for the Digitone mk1, in C: the firmware locations core exports, and core-dn1's
 * own tables and calls (docs/ADAPTING.md, "Firmware locations", "Parameter slots", "Mod pages", "Project
 * data", "The Mod Menu" and "Machines and stock parameters"). The SDK puts this folder on the include
 * path:
 *
 *     #include "digitone-mk1/core3.h"
 *
 * Every fw_ name here is resolved by the linker against the core in the build, so a mod that uses only
 * these rebuilds for another OS version without a change (its port in mod.json is empty). Declaring one
 * makes the mod need core-dn1 3.0, or 3.1, 3.2 or 3.3 for the names marked so: say so with "resources":
 * {"core": "3.0"} (or "3.1", "3.2", "3.3"). digitone-mk1/core3.inc has the same for assembly. Functions that return a pointer
 * are declared to return uint32_t: the firmware returns it in d0, where gcc for m68k looks in a0. */
#ifndef ELEKLOADER_DIGITONE_MK1_CORE3_H
#define ELEKLOADER_DIGITONE_MK1_CORE3_H

#include <stdint.h>

/* ---- the render: interrupt level, 1500 blocks a second; keep it short --------------------------- */
extern volatile uint32_t fw_voice_pitch[];      /* [v]: voice v's pitch word, the note << 16 (ev_voice_on) */
extern volatile int16_t fw_voice_params[];      /* the render's copy of each voice's sound: */
#define FW_VOICE_PARAM(v, k)  (fw_voice_params[9 + 79 * (v) + (k)])   /* slot k (0-78) of voice v, 8.8 */
extern volatile int32_t fw_voice_len[];         /* [v]: its length left in timeline units, 0 none; the
                                                   render releases the voice when it runs out */
extern volatile uint32_t fw_gate_off;           /* bit v: voice v was released in the last block */
extern volatile uint32_t fw_timeline;           /* 5,400,000 units a 16th (900,000 a 24 PPQN tick) */
extern volatile int32_t fw_transpose[];         /* [0] the transposition, [1 + t] track t's */
extern volatile int32_t fw_lfo_state[];         /* each voice's LFOs, 0x50 bytes a voice: */
#define FW_LFO(v, i)          (fw_lfo_state + 20 * (v) + 10 * (i))    /* LFO i (0, 1) of voice v */
#define FW_LFO_DEST           16                /* [16] its destination slot */
#define FW_LFO_MOD            17                /* [17] its modulation, 8.8 parameter units, signed */

/* ---- the note queue: mask interrupts around these --------------------------------------------- */
extern uint32_t fw_ev_alloc(void);              /* a note event (0x48 bytes), or 0 when the pool is empty */
extern void fw_ev_free(uint32_t *event);        /* and its p-lock list's reference */
extern void fw_ev_queue(uint32_t *event, uint32_t time);    /* at a time on the timeline */
extern uint32_t fw_lock_alloc(void);            /* a p-lock list; no check: test fw_locks_free first */
extern volatile uint32_t fw_locks_free;         /* the free p-lock lists, 0 for none */
extern volatile uint32_t fw_nodes_free;         /* the queue's free times: with none, queueing at a new
                                                   time never returns */

/* ---- the UI task ------------------------------------------------------------------------------ */
extern uint8_t *volatile fw_kit;                /* the active kit: */
#define FW_SOUND_SLOT(kit, t, k)  (*(volatile int16_t *)((kit) + 0x2c + 326 * (t) + 2 * (k)))
/* To change a sound slot, call core_sound_set (from 3.1): a voice keeps the sound it loaded last and
 * loads it again only for another sound, so a write to the kit alone is not heard on a voice that has
 * played the track before, until a stock edit or a sound change reloads it. core_sound_set writes the kit
 * as a stock edit does, and then the track's voices. Track 0-3, slot 0-78, value 8.8; the UI task. */
extern void core_sound_set(int32_t track, int32_t slot, int32_t value);
extern volatile uint32_t fw_slot_ids[];         /* [k]: sound slot k's parameter id */
extern const char fw_str_amp[];                 /* "Amp", a parameter group's name */
extern const char fw_str_empty[];               /* "", a parameter record's +60 */
extern void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
extern void fw_framerect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
extern int32_t fw_textf(void *bmp, const void *font, int32_t x, int32_t y, int32_t maxlen, const char *fmt, ...);
extern const char fw_font5[];
extern const char fw_font_label[];             /* 3.2: the stock pages' labels (FREQ, RESO), capitals 4 x 5 */
extern const char fw_font_title[];             /* 3.2: their title bar (Amplitude (1/2)), 6 pixels */
extern void fw_blit(void *dst, const void *src, int32_t x, int32_t y, int32_t centre);
extern uint32_t fw_op_new(uint32_t size);       /* the firmware's heap; 0 when it is full */
/* 3.3: textbox clears a box as wide as tmpl (fillrect colour 0), then draws fmt at x: align 0 left,
 * 1 centred, 2 right; y as fw_textf's. What the stock menus draw their values with. deco: 0. */
extern void fw_textbox(void *bmp, const void *font, int32_t x, int32_t y, int32_t align, int32_t deco,
                       const char *tmpl, const char *fmt, ...);
extern volatile uint32_t fw_transport;          /* 3.3: 0 stopped, 1 playing, 2 paused */
extern uint8_t *volatile fw_project;            /* 3.3: the project */
extern uint8_t *volatile fw_pattern;            /* 3.3: the active pattern's data, 976 bytes a track: */
#define FW_TRACK(pat, t)         ((pat) + 976 * (t))
#define FW_STEP(trk, s)          (*(volatile uint16_t *)((trk) + 2 * (s)))   /* step s's word: */
#define FW_STEP_TRIG             1u             /* a note trig */
#define FW_STEP_LOCK_TRIG        2u             /* a lock trig (no note) */
#define FW_TRACK_NOTE(trk)       ((trk)[0x3c0]) /* NOTE, a MIDI note */
#define FW_TRACK_ROOT(trk)       ((trk)[0x3cb]) /* the keyboard's ROOT, a MIDI note */
#define FW_TRACK_SCALE(trk)      ((int8_t)(trk)[0x3ce])  /* its SCALE: -1 chromatic, 0-34 */
#define FW_TRACK_LENGTH(trk)     ((trk)[0x3c5] << 8 | (trk)[0x3c6])  /* in per-track mode: 1-64 */

/* ---- 3.3: the firmware's C library (newlib's and libgcc's, with their contracts); any task ------- */
extern uint32_t fw_memcpy(void *dst, const void *src, uint32_t n);   /* -> dst */
extern uint32_t fw_memset(void *p, int32_t c, uint32_t n);            /* -> p */
extern uint32_t fw_memchr(const void *p, int32_t c, uint32_t n);      /* -> the byte, or 0 */
extern int32_t fw_strcmp(const char *a, const char *b);
extern int32_t fw_strncmp(const char *a, const char *b, uint32_t n);
extern uint32_t fw_strchr(const char *s, int32_t c);                  /* -> the char, or 0 */
extern uint32_t fw_strncpy(char *dst, const char *src, uint32_t n);   /* -> dst */
extern uint32_t fw_crc32(uint32_t crc, const void *p, uint32_t n);    /* zlib's: crc32(0, p, n) */
/* libgcc's 64-bit shifts, __ashldi3, __lshrdi3 and __ashrdi3, are exported under their own names:
 * gcc calls them for a 64-bit value shifted by a variable count, and those calls now link with no
 * libgcc in the mod. So a mod must not define them itself: every global a mod defines is an export,
 * and the linker refuses two exports of one name. */
extern volatile int32_t fw_active_track;        /* 3.1: the active track, 0-3 the synth tracks */
extern const uint8_t fw_params[];               /* 3.1: the parameter records, 60 bytes an id (slot at +4,
                                                   min +8, max +12, default +16, names +40 +44 +48) */
extern uint8_t fw_uirecs[];                     /* 3.1: their UI records, 84 bytes an id */

/* ---- 3.1: the voices, for machines (ev_render_voices) ------------------------------------------- */
extern int32_t fw_voices[];                     /* [32 * v + i]: the DSP's voice v this block, Q1.31 */
extern volatile int32_t fw_voice_track[];       /* [v]: the voice's track */
extern volatile uint32_t fw_gate_on;            /* bit v: voice v started in the last block */
/* Each block, after the DSP's voices land in fw_voices and before the render's filters: a handler may
 * write any voice's 32 samples, which are then that voice's sound through its filter and the mix. The
 * DSP applies the amp envelope before its output: what is written here has none. Interrupt level. */
typedef void (*ev_render_voices_fn)(int32_t *voices);

/* ---- 3.2: exclusive audio (core_audio) ------------------------------------------------------------ */
extern volatile int32_t fw_tempo;               /* 3.2: the tempo x 120 (87.0 BPM: 10440), as the sequencer
                                                   runs it; the timeline moves twice this every block */
#define CORE_AUDIO_MUTE_VOICES  1u
#define CORE_AUDIO_INSERT       2u      /* 3.3 */
/* A mod that wants the whole output contributes a pointer to one of these to the table core_audio and
 * may have its buffers in the profile's bulk area (a region it claims). A build may hold several such
 * mods: each block, before the voices' filters, core takes the first record whose on is set (the table's
 * order), and while there is one, the render's master stage (the inputs' mix, chorus, delay, reverb and
 * drive) does not run. render(out, in) gets the block's input, 32 frames
 * L,R in Q1.31 (the codec's 24 bits, left first on a Keys too), and writes all 64 words of out, 32 frames
 * L,R in Q1.31: core sends them to the codec and to USB's main pair. With CORE_AUDIO_MUTE_VOICES the
 * voices' filters are skipped too, so the synths are silent and are not filtered (the second CPU still
 * renders them, and the voice mix still runs: skipping it would leave a note that started meanwhile
 * silent after the owner lets go, until its next note). Interrupt level, inside the render: keep it
 * short, and keep MACSR as it was if the EMAC is used. Set and clear on from the UI task; it takes effect
 * at the next block.
 * With CORE_AUDIO_INSERT (3.3) the master stage runs as stock, and in is what it wrote to the output
 * (the voices, the inputs as the mixer has them, chorus, delay, reverb, the master drive), in Q1.31: an
 * effect on everything the Digitone plays. What render returns goes to the codec and to USB's main pair
 * in its place; the rest of the USB block and a Keys' second buffer stay the stage's. The voices always
 * play then (CORE_AUDIO_MUTE_VOICES is ignored), and the stock render's own share of the block stays:
 * an insert has what is left. A mod may switch its flags from the UI task; core reads them each block. */
struct core_audio_owner {
    volatile int32_t on;            /* +0 nonzero: this mod has the output */
    void (*render)(int32_t *out, const int32_t *in);    /* +4 each block it has it */
    uint32_t flags;                 /* +8 CORE_AUDIO_MUTE_VOICES, CORE_AUDIO_INSERT */
} __attribute__((aligned(4)));

/* ---- core-dn1's tables (core 2.1-2.3): what a mod contributes points to one of these ------------ */
struct core_param {                 /* core_params: parameter slots 182-184 */
    int32_t id;                     /* +0  182-184, claimed as param:<id> */
    int32_t group;                  /* +4  the page group: 1 Amp, 2 Filter, ... */
    int32_t slot;                   /* +8  the sound parameter it edits, 0-78 */
    int32_t min, max, def;          /* +12 8.8 */
    int32_t flags;                  /* +24 0 */
    int32_t cc;                     /* +28 MIDI CC (MSB << 16 | LSB), or -1 */
    int32_t r32, r36, r40;          /* +32 -1, -1, 0 */
    const char *name, *group_name, *short_name;     /* +44 */
    void (*format)(int32_t value, char *buf);       /* +56 the value's text */
    const char *empty;              /* +60 fw_str_empty */
    int32_t look;                   /* +64 the stock parameter whose knob it borrows, 0 for a plain one */
} __attribute__((aligned(4)));

struct core_page {                  /* core_pages: pages 27-30 */
    int32_t idx;                    /* +0  27-30, claimed as page:<idx> */
    int32_t after;                  /* +4  the stock page it follows: 9 makes it AMP's third */
    const char *short_title, *title;    /* +8 */
    int32_t ids[8];                 /* +16 the knobs' parameter ids, A-H; 0 an empty box */
    int32_t kind;                   /* +48 9 */
    void *view;                     /* +52 0: core writes it */
    int32_t pos;                    /* +56 core writes it */
} __attribute__((aligned(4)));

struct core_projdata {              /* core_projdata: saved and loaded with the project */
    uint32_t tag;                   /* +0  four characters, claimed as projdata:<tag> */
    uint32_t size;                  /* +4  bytes, a multiple of 4 */
    void *data;                     /* +8  the mod's copy */
    void (*loaded)(int32_t found);  /* +12 after a load; found 0: none, data holds zeros */
} __attribute__((aligned(4)));

#define CORE_MENU_ICON  0x49434F4Eu /* "ICON" (core-dn1 2.3) */
struct core_menu_item {             /* core_menu: the Mod Menu's entries */
    const char *name;               /* +0  its label, 14 characters */
    void (*open)(void *brain, void *event, int32_t track);  /* +4 picked */
    uint32_t tag;                   /* +8  CORE_MENU_ICON when an icon follows (2.3) */
    const uint16_t *icon;           /* +12 -> its icon, 16 rows of 16 pixels, the top row first and
                                       bit 15 the left pixel; 0 for core's own */
} __attribute__((aligned(4)));

/* 3.1: from an entry's open(), show another list in the Mod Menu's grid (a submenu). list: entries as
 * core_menu's, ending in 0; sel: the one to select first; a pick calls pick(brain, event, track, index)
 * in place of the entry's open(), the menu closed first (so pick may open another list). */
extern void core_menu_open(const void *const *list, int32_t sel,
                           void (*pick)(void *brain, void *event, int32_t track, int32_t index));

/* core_param_override: a mod answering for a stock parameter, while it wants to (from 3.1). ui returns a UI
 * record (84 bytes, the stock layout: the value text's formatter at +0x14, the knob graphic) or 0 for
 * the stock one; name returns the label for field 0x30 or 0. Either may be 0; the first answer wins. */
struct core_param_override {
    void *(*ui)(int32_t id);
    const char *(*name)(int32_t id, int32_t field);
} __attribute__((aligned(4)));

/* Make a UI record for an override's ui() (from 3.1): stock parameter id's, which says how a turn moves
 * its value (its steps and speed), with the knob graphic of the stock parameter look (0: PTIM, a plain
 * knob; CORE_LOOK_NONE: id 0's, the firmware's 'none') and fmt as its value text (0 keeps id's). ui: 84
 * bytes, 4-aligned, the mod's; build it once, outside the render. */
#define CORE_LOOK_NONE  (-1)
extern void core_param_ui_make(void *ui, int32_t id, int32_t look, void (*fmt)(int32_t value, char *buf));

/* Open a mod page from a key handler (its key's code: AMP 24, say); 0 when the view was never built. */
extern int32_t core_page_open(void *brain, void *event, int32_t key, void *page);
/* 1 when the page is on screen, with no overlay over it. */
extern int32_t core_page_shown(void *brain, void *page);

/* Core-dn1's own events, beside the hook bus's (docs/ADAPTING.md, "The hook bus"). */
typedef void (*ev_voice_on_fn)(int32_t voice, int32_t track, void *event);  /* in the render */
typedef int32_t (*ev_hold_fn)(void *brain, void *event, int32_t track);     /* nonzero: taken */
/* 3.2: each MIDI CC the unit receives on a track's channel or the auto channel, before the stock applies
 * it. track 0-8 (with MIDI CONFIG's default channels, channels 1-9); a CC on the auto channel comes as
 * the active track, with CORE_MIDI_CC_AUTO in flags. cc and value 0-127. Nonzero takes it: the stock
 * does not apply it. The MIDI task, not interrupt level. A mod that patches the router's entry
 * (Tone+FX) sees a CC first. */
#define CORE_MIDI_CC_AUTO  1u
typedef int32_t (*ev_midi_cc_fn)(int32_t track, int32_t cc, int32_t value, int32_t flags);
/* 3.3: the key-LED frame, every frame of the UI task, after the LEDs are cleared and before the stock views
 * set theirs: the first setter of an LED wins, so what a handler sets stays over the stock view's. mgr:
 * the LedManager. */
typedef void (*ev_key_leds_fn)(void *mgr);
/* 3.3: each block's sound parameter words (8.8) before the controller stage and the LFOs ride on them:
 * voice v's slot k at words[CORE_PARAMS_VOICE * v + k], the global FX's (chorus, delay, reverb, master)
 * at record CORE_PARAMS_FX; the layout of fw_voice_params past its 18 bytes, which holds the targets the
 * smoother glides these words to. The smoother writes them again every block, so a change lasts one
 * block and never builds up; the kit and the targets are untouched. Interrupt level, inside the render,
 * ~1500 blocks a second; fw_voice_track[v] is voice v's track. */
#define CORE_PARAMS_VOICE  79
#define CORE_PARAMS_FX     8
typedef void (*ev_render_params_fn)(int16_t *words);

#endif
