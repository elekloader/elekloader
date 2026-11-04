/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Core 3.0 for the Digitakt mk1, in C: the firmware locations core exports, and the page a machine
 * describes for the machine-pages mod (docs/ADAPTING.md, "Firmware locations" and "Machine pages").
 * The SDK puts this folder on the include path:
 *
 *     #include "digitakt-mk1/core3.h"
 *
 * Every name here is resolved by the linker against the core (and machine-pages) in the build, so a mod
 * that uses only these rebuilds for another OS version without a change. digitakt-mk1/core3.inc has the
 * same for assembly. Functions that return a pointer are declared to return uint32_t: the firmware and
 * core return it in d0, where gcc for m68k looks in a0. */
#ifndef ELEKLOADER_DIGITAKT_MK1_CORE3_H
#define ELEKLOADER_DIGITAKT_MK1_CORE3_H

#include <stdint.h>

/* ---- the render: interrupt level, 1500 blocks a second; keep it short, call nothing ---------------- */
extern int32_t fw_track_blocks[];               /* [32 * t + i]: track t's block, 32 Q31 samples */
extern volatile uint8_t fw_render_machine[];    /* [t]: the machine the track renders as */
extern volatile int16_t fw_voice_params[];      /* [53 * t + k]: the machine's parameters A-H (k 0-7), 8.8 */
extern volatile int32_t fw_voice_note[];        /* [t]: the trig's note, 16.16 (60 << 16 with none) */
extern volatile int16_t fw_voice_vel[];         /* [t]: its velocity, 8.8 */
extern volatile uint32_t fw_voice_start;        /* bit t: track t starts a voice this block */
extern volatile int32_t fw_amp_env[];           /* [3 * t]: the AMP envelope's phase, [3 * t + 1] its level */
extern const uint32_t fw_pitch_tab[];           /* 2^((i - 10752) / 2048), Q29, i = 0-14848 */

/* ---- the UI task --------------------------------------------------------------------------------- */
extern volatile uint32_t fw_active_track;       /* 0-7 */
extern uint8_t *volatile fw_kit;                /* the UI kit: track t's sound at + 0x20 + 0xa2 t */
#define FW_SOUND(kit, t)    ((kit) + 0x20 + 0xa2 * (t))
#define FW_SOUND_MACHINE    0x7e                /* a sound's byte: its machine */
#define FW_SOUND_PARAM(k)   (0x36 + 2 * (k))    /* its word: machine parameter A-H (slot 17 + k), 8.8 */
extern uint32_t fw_slice_layout[];              /* SLICE's SRC page layout: 11 longs, the ids at [2 + k] */
extern void fw_set_param(int32_t value, int32_t track, int32_t slot);   /* a knob's path into the engine */
extern const char fw_bitmap_vt[];               /* a Bitmap's vtable: an icon's first long */
extern void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
extern void fw_framerect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
extern int32_t fw_textf(void *bmp, const void *font, int32_t x, int32_t y, int32_t maxlen, const char *fmt, ...);
extern const char fw_font5[];
extern void fw_blit(void *dst, const void *src, int32_t x, int32_t y, int32_t centre);
extern uint32_t fw_op_new(uint32_t size);       /* the firmware's heap; 0 when it is full */
extern void fw_invalidate(void *view);          /* core 4.1: the view is drawn again */

/* ---- SRC machines (core 2.1), and the page a machine describes (core 3.0 + machine-pages) -------- */
#define CM_UI_TAG   0x55493330u                 /* "UI30": a descriptor's tail */

enum {
    CM_HIDDEN     = 1,      /* the knob is blank on the SRC page: it shows and turns nothing */
    CM_RANGE      = 2,      /* min and max replace the stock range */
    CM_DEFAULT    = 4,      /* def replaces the stock default (a switch to the machine, a new sound) */
    CM_NOT_SAMPLE = 8,      /* knob D: turning it changes it, and does not open the sample list; from
                               machine-pages 1.3, SRC keeps a one-page machine's page (no waveform view) */
    CM_DRAW       = 16,     /* draw, not gfx: the machine draws the knob's graphic (a page naming cm_ui_v31;
                               on a cm_ui_v3 page the stock graphic is drawn and +36 is not called) */
};

/* A value's text: write it into buf and return nonzero, or return 0 for the stock text. ctx is 0 for the
 * value under a turning knob (about 5 characters) and 1 for the pop-up (up to 15); value is 8.8. */
typedef int32_t (*cm_fmt)(char *buf, int32_t value, int32_t ctx, int32_t machine);
/* The value the knob's graphic shows, from the one the firmware gives it; both are 8.8. */
typedef int32_t (*cm_gfx)(int32_t value);
/* With CM_DRAW: draw the knob's graphic into bmp, the page's Bitmap, and return nonzero, or return 0 for the
 * stock graphic. It spans x + 1 to x + 17 and y to y + 16 (y = 0 is the screen's bottom row); value is 8.8,
 * and flag is the firmware's, for a stock drawer it calls. */
typedef int32_t (*cm_draw)(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag);

struct cm_knob {                    /* 40 bytes; knobs A-H by their place on the page */
    const char *name;               /* +0  its label on the SRC page; 0 for the stock one */
    const char *lname;              /* +4  its long name (the pop-up); 0 for the stock one */
    const char *lfo;                /* +8  its name in the LFO page's DEST box; 0 for name */
    const char *lfo_long;           /* +12 in its DEST list; 0 for lname */
    uint8_t look;                   /* +16 the parameter id whose UI record and graphic it borrows; 0 its own */
    uint8_t flags;                  /* +17 CM_HIDDEN, CM_RANGE, CM_DEFAULT, CM_NOT_SAMPLE */
    uint8_t cc;                     /* +18 cm_ui_v33, an extra value only: the MIDI CC that sets it on its track's
                                       channel, one no stock parameter of a track uses; 0 none */
    uint8_t reserved;               /* +19 0 */
    int32_t min, max, def;          /* +20 8.8, with CM_RANGE and CM_DEFAULT */
    cm_fmt fmt;                     /* +32 its value's text; 0 for the stock one */
    union {
        cm_gfx gfx;                 /* +36 its graphic's value; 0 for the value itself */
        cm_draw draw;               /* +36 with CM_DRAW: its graphic, drawn by the machine */
    };
} __attribute__((aligned(4)));

struct cm_ui {                      /* 336 bytes */
    const void *abi;                /* +0  cm_ui_v3, cm_ui_v31 for CM_DRAW, cm_ui_v32 for extra values (struct cm_ui32),
                                       cm_ui_v33 for extra values with CCs:
                                       links only beside a machine-pages that reads it */
    uint8_t page_from;              /* +4  the stock machine whose SRC page it copies: 0 ONESHOT ... 3 SLICE */
    uint8_t flags;                  /* +5  0 */
    uint16_t reserved;              /* +6  0 */
    const char *group;              /* +8  the LFO page's group for its parameters; 0 for the stock one */
    void (*on_switch)(int32_t track, int32_t from, uint8_t *sound);  /* +12 a track turns to it; 0 none */
    struct cm_knob knob[8];         /* +16 */
} __attribute__((aligned(4)));

struct cm_machine {                 /* what a mod contributes to core_machines points to */
    int32_t id;                     /* +0  4-127, claimed as machine:<id> */
    const char *name, *sname;       /* +4, +8 */
    const void *icon;               /* +12 an 11 x 7 Bitmap, or 0 */
    int32_t params, render;         /* +16, +20 */
    uint32_t tag;                   /* +24 CM_UI_TAG (core 3.0; core 2.1 reads to +20 only) */
    const struct cm_ui *ui;         /* +28 */
} __attribute__((aligned(4)));

/* machine-pages 1.2: up to seven more SRC values, on a second page. They live in the sound's slots
 * 46-52 (parameter ids 164-170), the seven the firmware leaves unregistered. Slot 0 is not one of
 * them: it is kept for the machine's id. */
#define CM_EXTRAS    7
#define CM_SRC_BLANK 255
/* A page's knob may instead be a stock sound parameter, by its id: one of the filter's (30-43), the
 * amp's (44-51) or the LFOs' (52-69). It is the firmware's own knob there (its label, range, locks,
 * LFO and saving), only shown on this page too. */
#define CM_SRC_STOCK(knob) (1u << (knob))
struct cm_src_page {                /* 16 bytes: fixed logical parameter references */
    const char *title;
    uint8_t parameter[8];           /* 0-14 (8-14 = extra[0-6]), CM_SRC_BLANK, or with stock a stock id */
    uint8_t stock;                  /* CM_SRC_STOCK(k): parameter[k] is a stock parameter's id */
    uint8_t reserved[3];            /* zero */
} __attribute__((aligned(4)));

struct cm_ui32 {                    /* params=3 only (SLICE's bank): fifteen values across up to two pages */
    struct cm_ui base;              /* +0: abi=cm_ui_v32, or cm_ui_v33 for extras with CCs; the eight */
    struct cm_knob extra[CM_EXTRAS]; /* +336: parameters 8-14; look 1-163, CM_RANGE|CM_DEFAULT, 0..0x7f00 */
    uint8_t page_count;             /* +616: 1 or 2 */
    uint8_t reserved[3];            /* zero */
    const struct cm_src_page *pages; /* +620 */
} __attribute__((aligned(4)));

extern const char cm_ui_v32[];                      /* machine-pages 1.2's: a page with extra values names it */
extern const char cm_ui_v33[];                      /* machine-pages 1.4's: one whose extra values have CCs (cc) */
/* machine-pages 1.5, UI task, inside a knob's gfx or draw: value k (0-7 the knobs, 8-14 the extras) of
 * the sound the SRC page shows, 8.8, as the page shows it: a held step's lock, a sound lock, or the
 * sound's own. CM_NO_VALUE outside a graphic call, for a k the page has no value for, or when no page
 * view draws the knob. */
#define CM_NO_VALUE ((int32_t)0x80000000)
extern int32_t cm_src_shown(int32_t parameter);
/* Render task: final values after native locks, smoothing and LFOs, for parameters 0-14. Invalid
 * track/index, or an extra parameter of a machine without them, returns 0. */
extern int32_t cm_src_value(int32_t track, int32_t parameter);

extern const char cm_ui_v3[];                       /* machine-pages' marker (cm_ui.abi) */
extern const char cm_ui_v31[];                      /* machine-pages 1.1's: a page with CM_DRAW names it */
extern volatile uint8_t core_track_machine[8];      /* each track's own machine, as the render last took it */
extern uint32_t core_machine(int32_t id);           /* -> its struct cm_machine, or 0 */
extern uint32_t core_machine_ui(int32_t id);        /* core 3.0: -> its struct cm_ui, or 0 */

/* machine-pages' render event: after playback has written every track's block, before the overdrive.
 * Subscribe with order 10-49 to make a track's sound, 50-89 to take or change one already made. */
typedef void (*ev_render_voices_fn)(int32_t *blocks);   /* blocks = fw_track_blocks */

/* ---- core 4.1: the master output and the master pages (FUNC+LFO) --------------------------------- */
/* ev_render_master: the block's master pair before it goes to the outputs and the USB stream, 32 frames of
 * {L, R}, changed in place. The render, at interrupt level: keep it short. The EMAC's MACSR is 0xA0 there
 * (fractional, saturating); core puts it back and clears ACC0 and ACC1 after the handlers. */
typedef void (*ev_render_master_fn)(int32_t *pair);
/* ev_fxmix: SETTINGS > GLOBAL FX/MIX is built; add rows with core_additem(menu, row), as for ev_settings. */
typedef void (*ev_fxmix_fn)(void *menu);

/* What a mod contributes to core_mpages points to (in .data: core writes view and pos). Core puts the page
 * in the master view's list after the stock page `after` (11 COMP, 12 internal mixer, 13 external mixer; any
 * other: last), names it and, while it is shown, calls draw (after the stock draw: the title bar, rows 53-63,
 * and below it kind 0's eight empty knob boxes, to clear where the page draws; bmp is 128 x 64, y 0 the
 * bottom row), key and enc (the view's events: nonzero takes one, 0 leaves it
 * to the stock handler), and clear when the page is cleared (FUNC + held LFO + PLAY). Any of the four may be 0.
 * The UI task. */
struct core_mpage {
    int32_t after;                                  /* +0 */
    const char *short_title, *title;                /* +4, +8: "EQ", "Master EQ" */
    void (*draw)(void *view, void *bmp);            /* +12 */
    int32_t (*key)(void *view, void *event);        /* +16 event +12 the key's id, +16 flags (bit 0 pressed) */
    int32_t (*enc)(void *view, void *event);        /* +20 event +12 knob 1-8 = A-H, 9 LEVEL; +16 the turn */
    void *view;                                     /* +24 core writes the view whose list has the page */
    int32_t pos;                                    /* +28 core writes its place in that list */
    void (*clear)(void *view);                      /* +32 FUNC + held LFO + PLAY (CLEAR): after the stock's pop-up */
} __attribute__((aligned(4)));
/* The mod page the view shows, or 0 (returned in d0 and a0): for a mod that reads keys or knobs itself. */
extern struct core_mpage *core_mpage_shown(void *view);
/* The master view, once drawn with a page in core_mpages, and a count of its draws since: a tick that sees the
 * count move knows the view is on the screen. */
extern void *volatile core_mview;
extern volatile uint32_t core_mview_draws;

/* ---- core 4.2: the sequencer's time (say "resources": {"core": "4.2"}) ----------------------------- */
extern volatile int32_t fw_tempo;               /* the tempo x 120 (87.0 BPM: 10440), as the sequencer runs it */
extern volatile uint32_t fw_timeline;           /* 5,400,000 units a 16th; the render moves it twice the tempo a block */
extern volatile uint32_t fw_transport;          /* 0 stopped, 1 playing */

/* ---- core 4.2: exclusive audio (core_audio) and the memory it lends -------------------------------- */
/* A mod that wants the audio contributes a pointer to one of these to the table core_audio. A build may hold
 * several: each block core takes the first record whose on is set (the table's order). The Digitakt renders
 * everything on one CPU, so while there is an owner its engine stops: the sound of the tracks, the filters, the
 * delay, reverb and compressor, and the mixer with its master stage (ev_render_master does not run meanwhile). The
 * sequencer, MIDI, USB and the sampler go on (the MIDI tracks keep playing, and the
 * sampler is handed the input and, as the main mix, what the owner plays). render(out, in) gets the block's input,
 * 32 frames L,R in Q1.31, and writes all 64 words of out, 32 frames L,R in Q1.31, in the input's polarity: core
 * sends them to the codec (as the stock's mixer would the inputs) and to USB's main pair. Interrupt level, inside the
 * render: keep it short, and keep MACSR as it was if the EMAC is used. Set and clear on from the UI task.
 * The memory: while a record has the audio, core lends it the delay's line, CORE_LEND_LO to CORE_LEND_HI (4 MB, a
 * region its mod claims in the profile's 'lend' area: the regions of several such mods may overlap there). Core
 * clears the line before it hands it over and again after the owner lets go, a part a block (about 85 ms each way,
 * the output silent meanwhile); core_audio_live names the record that has it, from the block its render first runs
 * to the moment on is cleared. A mod touches the line only while core_audio_live is its record. */
#define CORE_AUDIO_LEND         4u              /* core_audio_caps: the line is lent */
#define CORE_LEND_LO            0x47C00000u
#define CORE_LEND_HI            0x48000000u
extern const uint32_t core_audio_caps;
extern void *volatile core_audio_live;          /* the record that has the audio and the line, or 0 */
struct core_audio_owner {
    volatile int32_t on;            /* +0 nonzero: this mod wants the audio */
    void (*render)(int32_t *out, const int32_t *in);    /* +4 each block it has it */
    uint32_t flags;                 /* +8 0 */
} __attribute__((aligned(4)));

/* ---- core 4.2: the Mod Menu (core_menu), core-dn1's on the Digitakt --------------------------------- */
#define CORE_MENU_ICON  0x49434F4Eu /* "ICON" */
struct core_menu_item {             /* core_menu: the Mod Menu's entries */
    const char *name;               /* +0  its label, 14 characters */
    void (*open)(void *brain, void *event, int32_t track);  /* +4 picked */
    uint32_t tag;                   /* +8  CORE_MENU_ICON when an icon follows */
    const uint16_t *icon;           /* +12 -> its icon, 16 rows of 16 pixels, the top row first and bit 15 the left
                                       pixel; 0 for core's own */
} __attribute__((aligned(4)));
/* Show the Mod Menu: core_menu's entries from the root, at the entry it had (machine-pages' PEDALS row in the machine
 * menu calls it). The UI task. track: what the entries' open() get. */
extern void core_menu_show(int32_t track);
/* From an entry's open(), show another list in the same grid (a submenu): entries as core_menu's, ending in 0; sel:
 * the one to select first; a pick calls pick(brain, event, track, index) in place of the entry's open(), the menu
 * closed first (so pick may open another list). */
extern void core_menu_open(const void *const *list, int32_t sel,
                           void (*pick)(void *brain, void *event, int32_t track, int32_t index));

/* ---- core 5.0: the firmware's moments (say "resources": {"core": "5.0"}) --------------------------------- */
/* Each event is a list of handlers the core calls at one stock moment (mods/core/events.s and system.s say
 * where); with none, the stock runs as it was. A handler typed to return int32_t "may take": nonzero stops the
 * stock (the routine does not run, or the call is skipped). The first argument of the edit events is the
 * firmware's object for a pattern's track: its index is the long at +0x38. */

/* The system. ev_card_ready: the init task has the +Drive up (status 0) or not, before the project loads and
 * before the UI task exists. ev_boot_ui: the boot is done, the UI task is about to start. ev_idle: over and over
 * in the init task once the UI task is up (the lowest priority; never at interrupt level; a handler must not
 * wait for anything). ev_usb: a host configured the USB device (1), or its frames stopped (0); may be interrupt
 * level. ev_crash: an exception nothing handles, interrupts masked; call core_restart_now to get the unit back,
 * or return and the stock prints the crash and halts. */
typedef void (*ev_card_ready_fn)(int32_t status);
typedef void (*ev_boot_ui_fn)(void);
typedef void (*ev_idle_fn)(void);
typedef void (*ev_usb_fn)(int32_t configured);
typedef void (*ev_crash_fn)(void *frame);
/* Reboot from a task: the firmware's own restart (interrupts off, the panel link and the UART drained). */
extern void core_restart(void);
/* The same without the drain, for an exception handler or code with interrupts masked. Never returns. */
extern void core_restart_now(void);

/* Storage (the UI side). A project is about to be written to a slot (0-127; 128 the working copy: a SAVE writes
 * slot N, then 128), then was (ok nonzero: written); a project load finished (err 0: LOAD OK); the working
 * project was made a new one. A kit, pattern or sequence is about to be saved (op 0) or reloaded (1): temp 1
 * the working copy; part 0 what 1 the kit, part 1 what 1 the pattern, part 1 what 0 the sequence. A sample was
 * put in a slot of the project's list (slot below 0: the first free one). */
typedef void (*ev_project_saving_fn)(void *self, int32_t slot, void *ctx);
typedef void (*ev_project_saved_fn)(void *block, int32_t slot, int32_t ok);
typedef void (*ev_project_loaded_fn)(void *job, int32_t err);
typedef void (*ev_project_new_fn)(void *project);
/* A project slot is about to be cleared (how 0: MANAGE PROJECTS > CLEAR, the slot made a new project) or deleted
 * (1), once per ticked slot: a mod drops what it keeps for that slot. */
#define CORE_ERASE_CLEAR        0
#define CORE_ERASE_DELETE       1
typedef void (*ev_project_erase_fn)(int32_t slot, int32_t how);
#define CORE_PATTERN_SAVE       0
#define CORE_PATTERN_RELOAD     1
typedef void (*ev_pattern_op_fn)(int32_t op, int32_t temp, int32_t part, int32_t what);
typedef void (*ev_sample_assign_fn)(const char *name, int32_t slot, int32_t ok);
/* A sound or a kit about to be read from its record (a project load: 128 kits) or being written to it (a save,
 * a paste). */
typedef void (*ev_sound_read_fn)(void *sound, const void *buffer);
typedef void (*ev_sound_write_fn)(void *dest, void *sound, void *ctx);
typedef void (*ev_kit_read_fn)(void *kit, const void *buffer);
typedef void (*ev_kit_write_fn)(void *dest, void *kit, void *ctx);

/* The sequencer. ev_transport: about to start (what 1: PLAY, from stop or pause, MIDI Start and Continue; arg
 * the routine's, 0 from the key), stop (0: STOP, MIDI, a project load; arg the routine's first) or pause (2:
 * PLAY while playing); nonzero takes it: the sequencer does not. An external clock's stop: what 0, arg -1, not
 * asked (interrupt level). ev_seq_tick: each sequencer tick, about 157 a second, playing or not: interrupt.
 * ev_pattern_request: a pattern (or a chain of count) is asked for, before the stock queues it.
 * ev_pattern_switch: the sequencer takes up another pattern (a queued one at the end of the current: interrupt
 * level; or on STOP and while stopped). ev_tempo: the sequencer's tempo changed (BPM x 120, as fw_tempo). */
#define CORE_TRANSPORT_STOP     0
#define CORE_TRANSPORT_START    1
#define CORE_TRANSPORT_PAUSE    2
typedef int32_t (*ev_transport_fn)(int32_t what, int32_t arg);
typedef void (*ev_seq_tick_fn)(void);
typedef void (*ev_pattern_request_fn)(void *list, int32_t count);
typedef void (*ev_pattern_switch_fn)(void *pattern);
typedef void (*ev_tempo_fn)(int32_t tempo, int32_t old);

/* MIDI (the MIDI task, or the sender's). ev_midi_in: every message the MIDI task dispatches (channel, real
 * time, SysEx), before ev_midi_cc, ev_midi_pc and ev_sysex; source 2 or 4 USB, 8 DIN. ev_sysex: each whole
 * SysEx, F0 7E included. ev_midi_cc: a control change on a track's channel (track 16: the auto channel).
 * ev_midi_pc: a program change (flags 0). ev_midi_out: each message or SysEx piece sent (port 1 the default),
 * and each real-time byte (len 1, port -1). Nonzero takes it: the stock does not see it (does not send it). */
typedef int32_t (*ev_midi_in_fn)(const uint8_t *msg, int32_t len, int32_t source);
typedef int32_t (*ev_sysex_fn)(const uint8_t *msg, int32_t len, int32_t source);
typedef int32_t (*ev_midi_cc_fn)(int32_t track, int32_t cc, int32_t value, int32_t flags);
typedef int32_t (*ev_midi_pc_fn)(int32_t channel, int32_t program, int32_t flags);
typedef int32_t (*ev_midi_out_fn)(const uint8_t *buf, int32_t len, int32_t port);
/* Send SysEx (or any bytes) from a task, 144 bytes at a time. abort: a byte the caller may set to stop between
 * pieces (or 0). -> nonzero sent. */
extern int32_t fw_sysex_send(const uint8_t *buf, int32_t len, volatile uint8_t *abort, int32_t port);

/* Notes and voices. ev_live_note: a played note reaches a track before its voice (the trig keys, keyboard mode,
 * the preview, a MIDI note on a track's channel): on 1, vel its velocity; on 0 its release (vel: the routine's
 * third argument). Nonzero takes it: the track does not play (or release) it. The UI or the MIDI task; the
 * sequencer's trigs do not pass here. ev_voice_on: a track's voice starts (voice = track: one a track; event
 * the trig's record), in the render. ev_render_params: each render pass, the engine's parameter words once
 * complete (track t's 53 at words + 0x35 t); a handler may change them for this pass. ev_sound_live: a
 * parameter written into the engine's copy (value 8.8, track 0-7, 16 the master). Interrupt level but
 * ev_live_note and ev_sound_live: keep the others short. */
typedef int32_t (*ev_live_note_fn)(int32_t track, int32_t note, int32_t vel, int32_t on);
typedef void (*ev_voice_on_fn)(int32_t voice, int32_t track, void *event);
typedef void (*ev_render_params_fn)(int16_t *words);
typedef void (*ev_sound_live_fn)(int32_t value, int32_t track, int32_t slot);

/* The UI task. ev_ui_msg: each message it takes, before it switches on msg[0]; nonzero takes it (back to the
 * mailbox). ev_view: the view stack about to change: push (a = &new view, b = nonzero to first close views of
 * the same name), replace (the page keys: a = &old, b = &new), pop (NO, FUNC release); container is the main
 * stack's. fw_view(): the view on top (keys go to it), or 0. ev_mute: a track's mute about to change (track
 * 0-15, muted 1 or 0; row: the song row, else -1). ev_key_leds: each LED frame, before the views set theirs:
 * the first setter of an LED wins; set them with fw_led_set(container - 0x40, led, state, colour). */
typedef int32_t (*ev_ui_msg_fn)(uint8_t *msg);
#define CORE_VIEW_PUSH          0
#define CORE_VIEW_REPLACE       1
#define CORE_VIEW_POP           2
typedef void (*ev_view_fn)(int32_t op, void *container, void *a, void *b);
extern void *fw_view(void);
#define CORE_MUTE_GLOBAL        0       /* FUNC + a trig key */
#define CORE_MUTE_PATTERN       1       /* "Pattern mutes" mode (FUNC + BANK twice) */
#define CORE_MUTE_SONG_ROW      2       /* a song row's mutes; row says which */
typedef void (*ev_mute_fn)(int32_t where, int32_t track, int32_t muted, int32_t row);
typedef void (*ev_key_leds_fn)(void *container);
extern void fw_led_set(void *brain, int32_t led, int32_t state, int32_t colour);

/* Edits (the UI task). A step's trig placed (on 1) or removed (0); a parameter lock written (a held trig and a
 * knob: the first turn makes it with the current value); a track's length set (2-64, in the low word; sixteen
 * calls when set for all tracks); a sound page's knob applied (param the parameter's descriptor; ev_sound_live
 * then sees its slots). The FUNC+RECORD / STOP / PLAY ops about to run, and a held track key + NO's reload. */
typedef void (*ev_trig_set_fn)(void *track, int32_t step, int32_t on);
typedef void (*ev_plock_store_fn)(void *track, int32_t step, int32_t slot, int32_t value);
typedef void (*ev_track_length_fn)(void *track, int32_t length);
typedef void (*ev_param_set_fn)(void **page, void *sound, void *param);
#define CORE_CLIP_COPY          0       /* a = the pattern store, b = the index */
#define CORE_CLIP_PASTE         1       /* a = the track object, b = the menu item (0: a plain paste) */
#define CORE_CLIP_CLEAR         2
#define CORE_CLIP_SOUND_COPY    3       /* TRK held + FUNC+RECORD on the SRC page: a the page; b below 8 the
                                           track's sound, 8-15 a MIDI track's settings, 16 the FX */
#define CORE_CLIP_SOUND_PASTE   4       /* the same, FUNC+STOP */
#define CORE_CLIP_SOUND_CLEAR   5       /* the same, FUNC+PLAY */
typedef void (*ev_clip_op_fn)(int32_t op, void *a, int32_t b);
typedef void (*ev_track_reload_fn)(int32_t track);

/* Song mode switched on (1) or off (0) (the UI task); it takes effect at the current row's end, where
 * ev_pattern_switch gives the row's pattern. ev_beat: each metronome beat while playing, whether the click is on
 * or not (the sequencer's tick: interrupt level); beat: the firmware's beat byte, as the click reads it. */
typedef void (*ev_song_mode_fn)(int32_t on);
typedef void (*ev_beat_fn)(int32_t beat);

/* The sampler (the UI task): armed, recording, stopped, finished (fw_sampler_state 4: the take kept, 0:
 * empty), aborted, a kept take to be saved (the save job picks its slot), the take being written. A threshold
 * start while armed happens in the render: fw_sampler_state goes 1 to 2. */
#define CORE_SAMPLER_ARM        0
#define CORE_SAMPLER_START      1
#define CORE_SAMPLER_STOP       2
#define CORE_SAMPLER_FINISH     3
#define CORE_SAMPLER_ABORT      4
#define CORE_SAMPLER_SAVE       5
#define CORE_SAMPLER_WRITE      6
typedef void (*ev_sampler_fn)(int32_t op);
extern volatile int32_t fw_sampler_state;   /* 0 idle, 1 armed, 2 recording, 3 stopped, 4 a take kept */

#endif
