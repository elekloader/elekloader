/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-dn1 3.0 to 5.1 for the Digitone mk1, in C: the firmware locations core exports, and core-dn1's
 * own tables and calls (docs/ADAPTING.md, "Firmware locations", "Parameter slots", "Mod pages", "Project
 * data", "The Mod Menu" and "Machines and stock parameters"). The SDK puts this folder on the include
 * path:
 *
 *     #include "digitone-mk1/core3.h"
 *
 * Every fw_ name here is resolved by the linker against the core in the build, so a mod that uses only
 * these rebuilds for another OS version without a change (its port in mod.json is empty). Declaring one
 * makes the mod need core-dn1 3.0, or 3.1, 3.2, 3.3, 4.0, 4.1 or 4.2 for the names marked so: say so with
 * "resources": {"core": "3.0"} (or "3.1", "3.2", "3.3", "4.0", "4.1", "4.2"). From 4.0 the mods' code, tables and variables
 * have 1.25 MB (from 0x43EC0000) in place of 128 KB: a build that needs more than 128 KB needs core 4.0. digitone-mk1/core3.inc has the same for assembly. Functions that return a pointer
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
#define CORE_AUDIO_INSERT       2u      /* 4.0 */
/* 4.0: the flags core_audio takes beyond CORE_AUDIO_MUTE_VOICES (CORE_AUDIO_INSERT). A mod that runs on 3.2 too
 * lists it under "weak" in mod.json: with an older core it then reads 0, and the mod offers no insert. */
extern const uint32_t core_audio_caps;
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
 * With CORE_AUDIO_INSERT (4.0) the master stage runs as stock, and in is what it wrote to the output
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
struct core_param {                 /* core_params: parameter slots 182-191 (182-184 before 5.1) */
    int32_t id;                     /* +0  182-191, claimed as param:<id> */
    int32_t group;                  /* +4  the page group: 1 Amp, 2 Filter, ..., 16 Trig */
    int32_t slot;                   /* +8  the sound parameter it edits, 0-78; 5.1: CORE_SLOT_LANE */
    int32_t min, max, def;          /* +12 8.8 */
    int32_t flags;                  /* +24 0 */
    int32_t cc;                     /* +28 MIDI CC (MSB << 16 | LSB), or -1 */
    int32_t r32, r36, r40;          /* +32 -1, -1, 0 */
    const char *name, *group_name, *short_name;     /* +44 */
    void (*format)(int32_t value, char *buf);       /* +56 the value's text */
    const char *empty;              /* +60 fw_str_empty */
    int32_t look;                   /* +64 the stock parameter whose knob it borrows, 0 for a plain one */
    int32_t lane_flags;             /* +68 5.1: a lane's CORE_LANE_ flags (read only for a lane) */
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

/* 4.2: in a core_projdata's size, a block that saves only what it holds (the rest of size is the most it may
 * hold): the first long of its data is the bytes it holds now, that long included (rounded up to 4; 0, or
 * more than the most, saves all of it). A load copies what was saved and zeroes the rest, as for any block. */
#define CORE_PROJDATA_VARIABLE  0x80000000u
struct core_projdata {              /* core_projdata: saved and loaded with the project */
    uint32_t tag;                   /* +0  four characters, claimed as projdata:<tag> */
    uint32_t size;                  /* +4  bytes, a multiple of 4 (| CORE_PROJDATA_VARIABLE, 4.2) */
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
 * the stock one. name returns the short label for field 0x30, or 0 for stock. In 5.2 the LFO list and
 * DEST box also ask for 0x28 (destination name) and 0x2c (group). With no destination-name override the
 * box and an over-wide list row keep their stock short name. Either callback may be
 * 0; the first answer wins. These change text only: native slots and destination order stay intact. */
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

/* 5.2: the page list of the page key whose stock list starts with page first (SYN1 2, SYN2 4, FLTR 6, AMP 8,
 * LFO 10), replaced by list[0..n-1] (page indices: stock's 1-26, or a mod page's), or put back with n 0.
 * Never longer than the stock list. The page shown goes back to the first when it is past the new end; the
 * caller redraws (ev_tick's ctrl). 1, or 0 when that view is not built yet or the list is too long. From the
 * UI task (ev_tick, ev_key); the last call wins, so one mod answers for a key. */
extern int32_t core_page_list(int32_t first, const int32_t *list, int32_t n);

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
/* 4.0: each program change the unit receives, on any channel, before the stock applies it: channel 0-15,
 * program 0-127; flags CORE_MIDI_CC_AUTO when it came on the auto channel, and CORE_MIDI_PC_STOCK when the
 * stock would apply it (MIDI CONFIG receives program changes and it is on PROG CH IN's channel). Nonzero
 * takes it: the stock does not change the pattern. The MIDI task, not interrupt level. */
#define CORE_MIDI_PC_STOCK  2u
typedef int32_t (*ev_midi_pc_fn)(int32_t channel, int32_t program, int32_t flags);
/* 4.1: each event a note source queues for the render (the sequencer's fires, a trig's or the keyboard's note, MIDI
 * in, the arpeggiator), with the time it plays, before it is queued. The sequencer fires a step about one step
 * before it plays and queues its event for the step's time (microtiming and swing in); the render then starts its
 * voices with its p-locks, or puts a lock trig's locks into the track's voices. A mod that plays a track's locks
 * itself (an effect's p-locks, stored as a track's) takes the step's locks here and applies them when fw_timeline
 * reaches time.
 * Interrupt level or a task, with the interrupts masked: keep it short, only read the event, call no firmware. */
typedef void (*ev_note_queue_fn)(void *event, uint32_t time);
#define FW_EVENT_KIND(ev)       (((volatile uint32_t *)(ev))[0])    /* +0: what it is: */
#define FW_EVENT_FIRE           0u                                  /*   a step's fire (its trig, note or lock) */
#define FW_EVENT_FIRST          1u                                  /*   the first step's, made before PLAY: it
                                                                     *   has no p-lock list (and a lock trig there
                                                                     *   makes no event): read that step's locks
                                                                     *   from the lock table */
                                                                    /*   others (5, 6): the arpeggiator's and
                                                                     *   the sequencer's own, not trigs */
#define FW_EVENT_TRACK(ev)      (((volatile int32_t *)(ev))[2])     /* +8: its track, 0-3 the synth tracks */
#define FW_EVENT_STEP(ev)       (((volatile uint32_t *)(ev))[10])   /* +0x28: a fire's step word: */
#define FW_EVENT_NOTE_TRIG      1u                                  /*   a note trig */
#define FW_EVENT_LOCK_TRIG      2u                                  /*   a lock trig */
#define FW_EVENT_LOCKS(ev)      (((uint8_t *volatile *)(ev))[14])   /* +0x38: its p-lock list, or 0: */
#define FW_LOCKS_COUNT(l)       (*(volatile uint32_t *)((l) + 8))
#define FW_LOCK_SLOT(l, i)      (*(volatile uint16_t *)((l) + 0x14 + 8 * (i)))      /* a sound slot, 0-78 */
#define FW_LOCK_VALUE(l, i)     (*(volatile int16_t *)((l) + 0x16 + 8 * (i)))       /* its value, 8.8 */
#define FW_EVENT_MODE(ev)       (((volatile int32_t *)(ev))[3])     /* +0xc: where it came from: */
#define FW_EVENT_KEYBOARD       2                                   /*   the trig keys in KEYBOARD mode */

/* ---- 4.2: a starting voice's locks, and the parameter pages ------------------------------------ */
/* Each voice as it starts a note, once it holds its sound and the event's p-locks (after ev_voice_on, in the
 * same pass of the render): what the voice has here is what plays. voice 0-7, track, the note event (its
 * p-locks at FW_EVENT_LOCKS, FW_EVENT_MODE where it came from); the note is fw_voice_pitch[voice] >> 16.
 * Interrupt level, inside the render: keep it short. */
typedef void (*ev_voice_ready_fn)(int32_t voice, int32_t track, void *event);
/* The firmware's own: a p-lock list (the layout of FW_EVENT_LOCKS's: +8 the count, from +0x14 {slot, value}
 * 8 bytes apart) into a starting voice, as the render puts a step's locks there: each slot's target in
 * fw_voice_params, the smoother's value (so the note starts at it) and the slot's bit in fw_voice_locked.
 * params: fw_voice_params. From ev_voice_ready. */
extern void fw_voice_locks(int32_t voice, const void *list, volatile int16_t *params);
/* Each voice's slots that locks set as it started: 3 longs a voice, slot k at bit k & 31 of long k >> 5. A
 * voice's next note of the same sound loads these slots from the sound again (a new sound loads them all),
 * so a mod that writes a voice's parameters outside its start sets the slot's bit for it to come back. */
extern volatile uint32_t fw_voice_locked[];
#define FW_VOICE_LOCKED(v, k)   (fw_voice_locked[3 * (v) + ((k) >> 5)])
/* The UI record the pages use for parameter id (fw_uirecs', or a parameter slot's, 182-184): bit 0 of its
 * first long set when the parameter takes no p-locks. The UI task. */
extern uint32_t fw_param_uirec(int32_t id);
#define CORE_PARAM_LOCKABLE(id) ((*(volatile uint32_t *)fw_param_uirec(id) & 1) == 0)

/* A knob turn on a parameter page (a stock page or a mod's) that the stock would put into the track's sound:
 * no trig is held (GRID REC's held steps take their turns into their p-locks, as stock, and are never asked).
 * view: the page; id: the parameter; delta: the step the stock would add (8.8, a coarse parameter's whole
 * step 256, acceleration in); knob 0-7 for A-H. Nonzero takes it: the stock neither edits the sound nor
 * shows its pop-up. The UI task. */
typedef int32_t (*ev_param_turn_fn)(void *view, int32_t id, int32_t delta, int32_t knob);
/* The value a page shows on a knob for parameter id, asked every frame it draws its knobs. Nonzero answers:
 * *value (8.8) is shown, drawn inverted as a held step's p-lock when *locked is 1 (core clears it before
 * asking). Otherwise the stock answers: the held steps' lock, or the sound's value. Core also asks where the
 * pages read the track's sound for what they draw themselves (SYN2's envelopes, SYN1's algorithm, the knob
 * handlers' redraws: the views' value routine) and *locked is not used there; and on SYN2's envelope page,
 * which draws its knobs itself, *locked inverts the knob as on the other pages. The UI task. */
typedef int32_t (*ev_param_value_fn)(void *view, int32_t id, int32_t *value, uint8_t *locked);
/* A page's own answers, for these handlers (its C++ methods; the same in 1.43 and 1.44): the parameter on
 * knob 0-7, and the value the stock would show with no trig held (the track's sound). The UI task. */
#define CORE_VIEW_KNOB_ID(view, knob) \
    (((int32_t (*)(void *, int32_t))(*(void *const *const *)(view))[0xa0 / 4])((view), (knob)))
#define CORE_VIEW_SOUND_VALUE(view, id) \
    (((int32_t (*)(void *, int32_t, int32_t))(*(void *const *const *)(view))[0x58 / 4])((view), (id), -1))

/* ---- 5.0: the firmware's moments --------------------------------------------------------------------- */
/* The system. ev_card_ready: the init task has the +Drive up (status 0) or not (1, 6-9), before the project
 * loads and before the UI and MIDI tasks exist; the card reads and writes here. ev_boot_ui: the boot is done,
 * the UI task is about to start (the views are built in it next). ev_usb: the host configured the USB
 * device (1) or the configuration was dropped (0); may be interrupt level. ev_crash: an exception nothing
 * handles, interrupts masked, the frame (format/status word, PC, then the stack); call core_restart_now to
 * get the unit back, or return and the stock prints the crash and halts. */
typedef void (*ev_card_ready_fn)(int32_t status);
typedef void (*ev_boot_ui_fn)(void);
typedef void (*ev_usb_fn)(int32_t configured);
typedef void (*ev_crash_fn)(void *frame);
/* Reboot from a task: waits for the +Drive and the panel link, then resets (DDR word 0 cleared, RCM.RCR). */
extern void core_restart(void);
/* The same at once, for an interrupt or ev_crash. Never returns. */
extern void core_restart_now(void);
/* The +Drive, 512-byte sectors, from a task only (never an interrupt): 0, or < 0. */
extern int32_t fw_card_read(uint32_t sector, uint32_t bytes, void *buf);
extern int32_t fw_card_write(uint32_t sector, uint32_t bytes, const void *buf);
extern int32_t fw_card_erase(uint32_t first, uint32_t count);
extern int32_t fw_card_sectors(void);           /* -1 before the card is up */

/* Storage. A project is about to be written to a slot (0-127, 128 the working project), then was (ok
 * nonzero: written). The save task or the UI task. */
typedef void (*ev_project_saving_fn)(void *store, int32_t slot, int32_t arg3);
typedef void (*ev_project_saved_fn)(void *block, int32_t slot, int32_t ok);

/* The sequencer. ev_transport: about to start (what 1; arg 0 PLAY or MIDI Start, 1 MIDI Continue), stop
 * (0; the STOP key) or pause (2; PLAY while playing, MIDI Stop). Nonzero takes it: the sequencer does not.
 * ev_seq_tick: each tick of the sequencer (about 140 a second), before it runs: interrupt.
 * ev_pattern_request: a pattern change is asked for, before the stock queues it. The UI task. */
#define CORE_TRANSPORT_STOP     0
#define CORE_TRANSPORT_START    1
#define CORE_TRANSPORT_PAUSE    2
typedef int32_t (*ev_transport_fn)(int32_t what, int32_t arg);
typedef void (*ev_seq_tick_fn)(void);
typedef void (*ev_pattern_request_fn)(void *ui, void *request, int32_t arg3);
/* ev_tempo: the tempo is about to be set, in hundredths of a BPM (the setter clamps it to 3600-36000).
 * Nonzero takes it: the tempo stays. ev_pattern_switch: a pattern change asked for earlier is about to be
 * applied (pending: the firmware's word for it). */
typedef int32_t (*ev_tempo_fn)(int32_t bpm100);
typedef void (*ev_pattern_switch_fn)(int32_t pending);

/* MIDI. ev_midi_in: every message the MIDI task dispatches (channel messages, real time, SysEx), before
 * ev_midi_cc, ev_midi_pc and ev_sysex; source 2 or 4 USB, 8 or 0x10 DIN. ev_sysex: each SysEx but F0 7E.
 * ev_midi_out: each message or SysEx piece the port switch sends (port 1 the default, 2 USB, 3 DIN, 4 both).
 * Nonzero takes it: the stock does not see it (does not send it). The MIDI task, or the sender's. */
typedef int32_t (*ev_midi_in_fn)(const uint8_t *msg, int32_t len, int32_t source);
typedef int32_t (*ev_sysex_fn)(const uint8_t *msg, int32_t len, int32_t source);
typedef int32_t (*ev_midi_out_fn)(const uint8_t *buf, int32_t len, int32_t port);
/* Send SysEx (or any bytes) from a task: 144 bytes at a time; it busy-waits while the USB ring is full, so
 * keep replies small. abort: a byte the caller may set to stop between pieces (or 0). -> 1 sent. */
extern int32_t fw_sysex_send(const uint8_t *buf, int32_t len, volatile uint8_t *abort, int32_t port);

/* Voices. A voice is released (its note or note-off ran out, it was retriggered, its track stopped).
 * Interrupt level, the render. */
typedef void (*ev_voice_free_fn)(int32_t voice);
/* An arpeggiator step is due on a track (the render, before the stock counts it); and a busy voice is taken
 * for a new note, more notes than voices (old_track the track it played for). Interrupt level, the render. */
typedef void (*ev_arp_step_fn)(int32_t track, void *event);
typedef void (*ev_voice_steal_fn)(int32_t voice, int32_t old_track);
/* A trig key plays a note in KEYBOARD mode (these notes do not pass the note queue): pressed (on nonzero; a
 * handler that returns nonzero takes it, and no note plays) or released (an observer). state: the
 * firmware's. The UI task. */
typedef int32_t (*ev_keyboard_note_fn)(void *state, int32_t on, int32_t track, int32_t note);
/* A track's mute is about to be set (muted 1) or cleared (FUNC + a track key). The UI task. */
#define CORE_MUTE_GLOBAL        0       /* FUNC + a track key */
#define CORE_MUTE_PATTERN       1       /* quick mute in "Pattern mutes" mode */
#define CORE_MUTE_SONG_ROW      2       /* a song row's mutes; row says which */
typedef void (*ev_mute_fn)(int32_t where, int32_t track, int32_t muted, int32_t row);

/* The UI task's messages, each as it takes it, before it switches on msg[0] (a type past 34 is dropped). */
typedef void (*ev_ui_msg_fn)(uint8_t *msg);

/* Sequencer edits (the UI task): a step's flags (mask 1, 2: its trigs), a step's note slot (note -1 clears),
 * a parameter lock set (live record or an edit), one lock removed, all of a step's locks removed, a track's
 * length (2-64). The first argument is the firmware's track (or pattern) object. */
typedef void (*ev_trig_set_fn)(void *track, int32_t step, int32_t mask, int32_t on);
typedef void (*ev_step_note_fn)(void *track, int32_t step, int32_t note, int32_t slot);
typedef void (*ev_plock_store_fn)(void *pattern, int32_t track, int32_t step, int32_t slot, int32_t value);
typedef void (*ev_plock_clear_fn)(void *track, int32_t step, int32_t slot);
typedef void (*ev_step_clear_fn)(void *track, int32_t step);
typedef void (*ev_track_length_fn)(void *track, int32_t length);

/* Sound parameters: the stock setter's commit (lock nonzero: into the held steps' locks), and a sound slot's
 * value going to the voices playing the track (also core_sound_set's). The UI task. */
typedef void (*ev_param_set_fn)(void *page, int32_t id, int32_t value, int32_t a4, int32_t a5, int32_t lock);
typedef void (*ev_sound_live_fn)(int32_t value, int32_t track, int32_t slot);

/* The master's effect stages, each block (interrupt): stage 0-2 the three send effects (chorus, delay,
 * reverb, by their place on the bus), 3 the master drive. Nonzero takes it: the handler wrote out and the
 * stock stage is skipped. */
#define CORE_FX_CHORUS          0
#define CORE_FX_DELAY           1
#define CORE_FX_REVERB          2
#define CORE_FX_DRIVE           3
typedef int32_t (*ev_render_fx_fn)(int32_t stage, int16_t *out, int16_t *in, void *params);

/* The pattern menu's two workers, at their entry (UI task), before their write-protect and project checks:
 * FUNC+YES saves, FUNC+NO reloads; what is 1 the kit, 2 the sequence, 3 both. */
#define CORE_PATTERN_SAVE       0
#define CORE_PATTERN_RELOAD     1
#define CORE_PATTERN_KIT        1
#define CORE_PATTERN_SEQ        2
typedef void (*ev_pattern_op_fn)(int32_t op, int32_t index, int32_t what);

/* A held track key + NO reloads the track's sound from the saved pattern, once per track (UI task), before
 * the project check. params is the parameter list (begin, end pointers): empty = the whole sound. */
typedef void (*ev_track_reload_fn)(int32_t track, int32_t pattern, void *params);

/* The SOUND MANAGER (SETTINGS > SOUNDS) running an action on the ticked sounds (UI task), before it runs.
 * The copies: a = the menu view, b = the selection. Delete, send, protect: a = the selection (delete: b
 * nonzero once confirmed). The selection points at a vector of the ticked sounds' indices. */
#define CORE_SOUND_COPY_POOL    0
#define CORE_SOUND_COPY_BANK    1       /* to the +Drive or one of its banks */
#define CORE_SOUND_COPY_OTHER   2       /* from the pool view (LEFT > VIEW POOL) to the +Drive or a bank */
#define CORE_SOUND_DELETE       3
#define CORE_SOUND_SEND_SYSEX   4
#define CORE_SOUND_PROTECT      5       /* toggle write protection */
#define CORE_SOUND_PASTE        6       /* the SOUND BROWSER's FUNC+STOP into a pool slot (b: confirmed) */
#define CORE_SOUND_SAVE         7       /* the pool view's SAVE TO HERE: the track's sound into the slot */
typedef void (*ev_sound_op_fn)(int32_t op, void *a, void *b);

/* Before the kit puts a sound on a track (UI task): the SOUND BROWSER's FUNC+YES load. sound is the
 * incoming 0x146-byte sound; the track's slot is kit + 0x60 + track * 0x328. */
typedef void (*ev_sound_load_fn)(int32_t track, void *sound, void *kit);

/* Before a track's sound is initialised and named (UI task): SOUNDS > CLEAR TRACK SOUND. slot is the
 * track's sound, kit + 0x60 + track * 0x328. */
typedef void (*ev_sound_clear_fn)(void *slot, const char *name);

/* Over and over in the init task once the UI task is up: the lowest priority task that runs (RTOS priority 1),
 * so only while no other task is ready. Never at interrupt level; a handler must not wait for anything. */
typedef void (*ev_idle_fn)(void);

/* The view stack, before it changes (UI task). Push: a = &new view, b = nonzero to first close views of the
 * same name; replace (the page keys): a = &old, b = &new; pop (NO, FUNC release): a = the matcher.
 * container is MainScreen+0x40 for the main stack. fw_view() is the view on top (keys go to it), or 0. */
#define CORE_VIEW_PUSH          0
#define CORE_VIEW_REPLACE       1
#define CORE_VIEW_POP           2
typedef void (*ev_view_fn)(int32_t op, void *container, void *a, void *b);
extern void *fw_view(void);

/* ---- 5.0.1 ---------------------------------------------------------------------------------------- */
/* The sequencer's MIDI out: each message its MIDI engine queues for DIN and USB (the MIDI tracks' notes,
 * CCs, program changes, pitch bend, aftertouch; All Sound Off at STOP), whole, before it goes into one of
 * the engine's two buffers (buffer 0 or 1). These do not pass the port switch, so ev_midi_out does not see
 * them. Nonzero takes the message: it is not sent. Interrupt level (the engine's tick): keep it short. */
typedef int32_t (*ev_seq_midi_fn)(const uint8_t *msg, int32_t len, int32_t buffer);
/* A whole MIDI message played into the unit as if it came in on DIN: ev_midi_in's handlers (a nonzero
 * return takes it: -> 1), then the MIDI task's handler for its status, so ev_midi_cc and ev_midi_pc see it
 * and the stock applies it by MIDI CONFIG's channels (INPUT FROM is not asked). From a task, never at
 * interrupt level. -> 0. */
extern int32_t core_midi_receive(const uint8_t *msg, int32_t len);
/* The project's pool sound index (0-127) put on track (0-3) as the SOUND BROWSER's FUNC+YES does (the
 * handlers of ev_sound_load see it). The UI task. -> 0 loaded, 1 the slot took nothing, 2 no such track,
 * -1 no such pool sound. */
extern int32_t core_pool_load(int32_t track, int32_t index);
/* The global mutes (FUNC + a track key): bit t for track t, 0-3 the synth tracks, 4-7 the MIDI tracks
 * (0 with no pattern); and one track's mute set (muted 1) or cleared as FUNC + its key does, which
 * ev_mute's handlers see (where 0). The UI task. */
extern uint32_t core_mutes(void);
extern void core_mute_set(int32_t track, int32_t muted);
/* The stock key dispatch, Brain::key, where ev_key hands a key on: a handler may pass an event of its own
 * (a copy of the one it got, with +12 the key and +16 its flags changed) to replay a stock key or combo.
 * The UI task. */
extern int32_t fw_brain_key(void *brain, void *event);
#define CORE_KEY_ID(ev)     (*(volatile int32_t *)((uint8_t *)(ev) + 12))
#define CORE_KEY_FLAGS(ev)  (*(volatile uint32_t *)((uint8_t *)(ev) + 16))
#define CORE_KEY_DOWN       1u      /* pressed */
#define CORE_KEY_FUNC       2u      /* with FUNC held */
#define CORE_KEY_REPEAT     8u      /* held: about 0.4 s in, then repeating */
#define CORE_KEY_UP         0x10u   /* released */
/* The kit's sound load: kit = fw_project + 0x250, sound 0x146 bytes, track 0-3. -> as core_pool_load's. */
extern int32_t fw_sound_load(void *kit, const void *sound, int32_t track);
/* MIDI CONFIG > CHANNELS: track t's channel (0-15), a long, t 0-3 the synth tracks. */
extern volatile int32_t fw_midi_chan[];

/* ---- 5.1 ---------------------------------------------------------------------------------------------- */
/* Lane parameters (docs/ADAPTING.md, "Lane parameters"): a core_params entry whose slot is CORE_SLOT_LANE
 * keeps its values in core, not in a sound: per pattern a value for each track (as NOTE or VELOCITY) and a
 * p-lock on any step, saved with the pattern as lock rows the stock OS skips. Whole numbers: the record's
 * min, max and default >> 8 (min 0 or more); its knob turns and shows them as any parameter's, with trig
 * keys held in GRID REC the held steps' locks. CORE_LANE_NOLOCK: a track value only. The value a getter
 * gives is a whole number, -1 for none; the pattern is the one the sequencer plays (in ev_note_built, the
 * one that note came from). */
#define CORE_SLOT_LANE          (-1)
#define CORE_LANE_NOLOCK        1
#define CORE_PARAM_FIRST        182     /* the mod parameter ids, 182-191 */
#define CORE_PARAM_LAST         191
extern int32_t core_lane_lock(int32_t id, int32_t track, int32_t step);    /* the step's p-lock, or -1 */
extern int32_t core_lane_track(int32_t id, int32_t track);                 /* the track's value */
extern int32_t core_lane_value(int32_t id, int32_t track, int32_t step);   /* the lock, else the track's */
/* The UI task: step -1 sets the track's value (value -1: the default again), a step its lock (value -1 clears
 * it). -> 0 when it could not be kept: no such lane, no pattern, or no room (core keeps 384 locked
 * track-lanes for the project). */
extern int32_t core_lane_set(int32_t id, int32_t track, int32_t step, int32_t value);

/* Knobs on pages: a core_knobs entry points to one of these, and core puts id on that page's knob when its
 * box is empty (claim it as the resource knob:<page>:<knob>). page 1-26 the stock pages (19 the MIDI tracks'
 * TRIG page, whose E, G and H are empty), or CORE_PAGE_TRIG2: core's second TRIG page (30) for the synth
 * tracks, after TRIG's first ("Trig Parameters (2/2)"), shown once a mod puts a knob on it. knob 0-7, A-H. */
struct core_knob {
    int32_t page, knob, id;
} __attribute__((aligned(4)));
#define CORE_PAGE_TRIG          1
#define CORE_PAGE_MIDI_TRIG     19
#define CORE_PAGE_TRIG2         30
extern struct core_page core_trig2;     /* its descriptor, for core_page_open (TRIG's key is 20) */

/* Each note the sequencer builds, as it builds it, before it is queued: track 0-3 a synth track's step, 4-7
 * a MIDI track's; event the firmware's note event. A handler may change it: CORE_EVENT_SOUND is the sound a
 * synth track's note plays (the track's, or a pool sound for a step's sound lock: fw_pool_sounds + 0x146 n).
 * Interrupt level (the sequencer's tick, or PLAY's first step): keep it short. */
typedef void (*ev_note_built_fn)(int32_t track, int32_t step, void *event);
/* The step a note event came from, for ev_voice_on and ev_voice_ready: -1 for a note the sequencer did not
 * build (the keys, MIDI, the arpeggiator). Core remembers the last 16 notes built, and knows a copy of one
 * (PLAY's first step is built ahead while stopped and copied as the sequencer starts) by its track, notes,
 * flags and sound. Any level. */
extern int32_t core_note_step(void *event);
#define CORE_EVENT_SOUND(ev)    (*(const uint8_t **)((uint8_t *)(ev) + 0x2c))
#define FW_POOL_SOUND_SIZE      0x146
extern uint8_t fw_pool_sounds[];        /* the pool's sounds, sound n at + 0x146 n */
extern int32_t core_pool_count(void);   /* pool sounds 0 to this less one exist */
extern uint8_t fw_patterns[];           /* the 128 live patterns, k at + 0x1611d k (fw_pattern is one) */

/* ---- 5.3: the played notes, as the Digitakt's core 4.2 has them --------------------------------------- */
/* ev_live_note: a note played live on a track reaches it before its voice (KEYBOARD mode's trig keys, a MIDI
 * note on a track's channel): on 1, vel its velocity; on 0 its release (vel: the routine's third argument,
 * the source). Nonzero takes it, press or release: the key is not counted and the track does not play (or
 * release) it. The UI or the MIDI task; the sequencer's trigs do not pass here. */
typedef int32_t (*ev_live_note_fn)(int32_t track, int32_t note, int32_t vel, int32_t on, int32_t source);
/* The stock routines past the event, to play a note again: skip nonzero counts the key without playing it;
 * KEYBOARD mode passes sound -1. Each counts the key held on its track, so pair them. */
extern void fw_note_on(int32_t track, int32_t note, int32_t vel, int32_t source, int32_t skip, int32_t sound);
extern void fw_note_off(int32_t track, int32_t note, int32_t source);

#endif
