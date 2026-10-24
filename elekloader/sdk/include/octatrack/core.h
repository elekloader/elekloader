/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.8 for the Octatrack (OS 1.40C): what the core knows and exports, for C mods.
 * The SDK puts this folder on the include path:
 *
 *     #include "octatrack/core.h"
 *
 * - the firmware's current context (which track, Part and pattern the panel is on, which Part each
 *   track plays, whether the sequencer runs): fixed locations, read through the macros below;
 * - the transport events' arguments (docs/ADAPTING.md, the hook bus on the Octatrack);
 * - the frame budget meter, which the core keeps in every build;
 * - ot_page2_write: a page-2 knob's value into the Part, its copy, the engine and the dirty flags;
 * - ot_midi_send_paced: MIDI out that cannot hang the unit or flood a controller.
 * - the project's lifecycle events, mods' project and card data, battery RAM (0.6);
 * - the voice events: what the frame interrupt does to a track's voice (0.7).
 * - ot_boot_info: what the core's boot check found (0.7.1).
 * - the edits a mod's data must follow (the clipboard, the undo buffer, the Parts), the voice's
 *   reset, the boot's start, the track icons and the SETUP knobs (0.8).
 *
 * Locations are for OS 1.40C; a mod that uses only these names rebuilds for another OS by the core's
 * port, not by a change here. Where octabam's docs/firmware says how a location was found, the
 * name is in the comment.
 */
#ifndef ELEKLOADER_OCTATRACK_CORE_H
#define ELEKLOADER_OCTATRACK_CORE_H

#include <stdint.h>

/* ---- the context --------------------------------------------------------------------------------
 * Two copies of the "where is the panel" state exist and the firmware keeps them equal: the UI's
 * (battery-backed SRAM at 0x100b14cc..) and the engine's (0x80000000..). The editors, the MIDI
 * thread and the sequencer read the engine's; the page resolver reads the UI's Part. */
#define OT_B(a)             (*(volatile uint8_t *)(uintptr_t)(a))
#define OT_L(a)             (*(volatile uint32_t *)(uintptr_t)(a))

#define OT_UI_TRACK         OT_B(0x100b14cc)    /* 0-7: the audio track the panel is on */
#define OT_UI_BANK          OT_B(0x100b14ce)    /* 0-15 */
#define OT_UI_PART          OT_B(0x100b14cf)    /* 0-3 */
#define OT_TRACK            OT_B(0x80000000)    /* the engine's copy of OT_UI_TRACK */
#define OT_BANK             OT_B(0x80000002)    /* the current bank, 0-15 */
#define OT_PART             OT_B(0x80000003)    /* the current Part, 0-3: the one page edits write to */
#define OT_PATTERN          OT_B(0x80000004)    /* the current pattern in the bank, 0-15 */

/* The Part and bank each track is playing (a pattern change queued for later has not reached
 * them; a Part edit goes to OT_PART, which may not be the one a track plays). 0xff for a track
 * the frame interrupt has not yet rewritten since the sequencer last switched patterns. */
#define OT_PLAYING_BANK(t)  OT_B(0x8000182a + (t))
#define OT_PLAYING_PART(t)  OT_B(0x80001832 + (t))

/* The sequencer's word. */
#define OT_PLAYING          OT_L(0x800065b8)
enum {
    OT_PLAYING_STOPPED = 0,
    OT_PLAYING_RUNNING = 1,
    OT_PLAYING_STOP    = 2,     /* what the STOP routine (the STOP key, MIDI stop) leaves it at */
};

/* ev_transport(what, playing): what the sequencer just did, and the word as stored. */
enum {
    OT_TRANSPORT_START  = 0,    /* the cold start (PLAY from stopped, MIDI start) */
    OT_TRANSPORT_RESUME = 1,    /* the start from where it stopped (PLAY after STOP, MIDI continue) */
    OT_TRANSPORT_STOP   = 2,    /* the STOP routine, at its head */
};

/* ---- the frame budget meter ---------------------------------------------------------------------
 * The core times every frame interrupt (2,756 a second, 16 samples) on DMA timer 3, a free-running
 * counter of the 132 MHz bus clock (the OS only reads it; it wraps in 32.5 s, so take differences).
 * A frame is 47,891 counts (362.8 us) at 44.1 kHz. A frame's interrupt takes from its first
 * instruction to its last, so it includes every mod's handlers and the DMA interrupt nested in it.
 * Read the fields in a task; ot_meter_reset() starts the maxima and counts again. */
struct ot_frame_meter {
    uint32_t frames;            /* frame interrupts measured since boot or the reset */
    uint32_t last;              /* the last one's length, in counts */
    uint32_t worst;             /* the longest since the reset */
    uint32_t overruns;          /* those longer than a frame (tpf): the next frame's interrupt ran late */
    uint32_t late;              /* frames that began more than 1.5 frames after the one before */
    uint32_t tpf;               /* counts a frame takes, measured over the last 1,024 frames */
    uint32_t total;             /* the sum of the lengths (wraps): total/frames is the mean */
    uint32_t private_[4];
};
extern volatile struct ot_frame_meter ot_meter;
#define OT_DTCN3            OT_L(0xfc07c00c)    /* the counter itself */
#define OT_FRAME_COUNTS     47891u              /* tpf until the first 1,024 frames are measured */
void ot_meter_reset(void);

/* ---- the boot check ----------------------------------------------------------------------------
 * .boot copies the RAM image the linker built into the reserve and hashes the copy against the
 * linker's __run_hash (link.py, run_hash: a rolling x33 over its 32-bit words, seed 5381). When
 * they differ it halts rather than run mod code that is not the code that was linked. A mod that
 * runs at all has therefore seen state == OT_BOOT_OK and want == got. The same record is left in
 * the reserve's .bss when it does halt, for an emulator or a debugger to read. */
#define OT_BOOT_OK          0x424f4f54u         /* 'BOOT': the copy matched */
#define OT_BOOT_FAIL        0x4641494cu         /* 'FAIL': the copy did not match; the boot halted */
struct ot_boot_info {
    uint32_t state;             /* OT_BOOT_OK, or 0 before .boot has run */
    uint32_t want;              /* the linker's hash of the RAM image */
    uint32_t got;               /* the hash of the copy in the reserve */
};
extern volatile struct ot_boot_info ot_boot_info;

/* ---- the Parts ----------------------------------------------------------------------------------
 * The current bank's RAM is OT_BANK_RAM; its four Parts (6,322 bytes each) start at +0x8ed80. In a
 * Part, track t's effect on FX1 is byte t, its effect on FX2 byte 8 + t, its machine (0 STATIC, 1 FLEX,
 * 2 THRU, 3 NEIGHBOR, 4 PICKUP) byte 0x22 + t. Effect 0 is NONE. */
#define OT_BANK_RAM         OT_L(0x46c82456)
#define OT_PART_SIZE        6322u
#define OT_PART_BASE(p)     ((volatile uint8_t *)(uintptr_t)(OT_BANK_RAM + 0x8ed80u + (p) * OT_PART_SIZE))
#define OT_PART_FX1(p, t)   OT_PART_BASE(p)[t]
#define OT_PART_FX2(p, t)   OT_PART_BASE(p)[8 + (t)]
#define OT_PART_MACHINE(p, t) OT_PART_BASE(p)[0x22 + (t)]

/* ---- page 2 -------------------------------------------------------------------------------------
 * Each effect, machine and AMP has a second page of six knobs (slot 0-5) that stock reaches only
 * through the panel. ot_page2_write makes the stores the panel's editor makes: the Part's byte (the
 * current Part, OT_PART), its battery-backed copy, the part-changed bits, the bank's and the
 * project's dirty flags, the engine's live lane (the engine ships it to the DSP every frame), and
 * the slot's redraw marker. Without the dirty flags the Part's byte is lost on a Part change, and
 * without the lane the sound does not change. The editor's other effect, a redraw of the page it is
 * on, is not made: a page that is on screen shows the new value when it next draws.
 *
 * The value is clamped to the knob's range, which the firmware keeps in the descriptor of the
 * track's effect or machine (page OT_P2_PLAYBACK: the machine, and its own six bytes in the Part).
 * An effect with no real page-2 knob has a placeholder descriptor that allows 0-127 in every slot,
 * so look at OT_PART_FX1/FX2 first when it matters (CC MAP skips NONE). Returns the value stored,
 * or OT_P2_REFUSED for a track above 7, a page above 3, a slot above 5, or a knob that has no
 * range. Call it from a task. */
enum {
    OT_P2_PLAYBACK = 0,         /* the SRC page: the track's machine decides what the six are */
    OT_P2_AMP      = 1,
    OT_P2_FX1      = 2,
    OT_P2_FX2      = 3,
};
#define OT_P2_REFUSED       ((int32_t)0x80000000)
int32_t ot_page2_write(unsigned trk, unsigned page, unsigned slot, int32_t val);

/* ---- paced MIDI out -----------------------------------------------------------------------------
 * The firmware's midi_send (0x40010bc8) has no flow control: it waits, with interrupts masked, for
 * room in the 4 KB DIN ring, which only an interrupt empties, so a burst larger than the ring
 * hangs the unit; and a controller (a BCR2000) that gets hundreds of messages in a second locks up.
 * ot_midi_send_paced copies the message (1-255 bytes: a whole channel message or SysEx) into a
 * queue of 512 bytes and returns 1; core sends one message per 120 Hz tick (ev_tick120) while the
 * DIN ring has room and the engine is idle (a send during a project load reorders the tasks).
 * Returns 0, and queues nothing, when the queue has no room or the length is wrong. It goes through
 * midi_send, so ev_midi_out handlers (USB MIDI) see it. Callable from any task or interrupt. */
int ot_midi_send_paced(const uint8_t *msg, unsigned len);
unsigned ot_midi_paced_space(void);     /* bytes the queue can take: a message needs len + 1 */

/* ==== core-ot 0.6: the project, the card and CS1 ================================================== */

/* ---- ev_project(how, dropped), as core-ar2's and core-a4's ------------------------------------------
 * After the firmware's engine task has loaded a whole project (LOAD PROJECT, the power-up's load,
 * RELOAD) or made a new one (CREATE EMPTY PROJECT), when the job has ended: the banks, the Parts and
 * the mods' blocks below are in place. It runs in the engine task: a mod may use the file calls. */
#define CORE_PROJECT_NEW    0
#define CORE_PROJECT_LOADED 1
#define OT_DROPPED_PROJECT  1u          /* dropped: the project's blocks hold zeros (no file, or new) */
#define OT_DROPPED_CARD     2u          /* the card's blocks hold zeros (the first event after boot only) */

/* The Octatrack's own lifecycle events, handlers void f(unsigned a, unsigned b), in the task that
 * made the call (the engine task's for the first four):
 *   ev_banks_load(pre, mask)     a load of banks (mask: bit n, bank n): 1 before, 0 after
 *   ev_banks_write(pre, mask)    the bank writer (background save, SAVE PROJECT, SAVE AS, a new
 *                                project): 1 before the first bank file, 0 after
 *   ev_project_store(pre, 0)     the project store (SAVE PROJECT: each .work copied to .strd)
 *   ev_project_reload(pre, 0)    the project reload (each .strd copied back to .work)
 *   ev_cs1_bank(to, bank)        a bank's working copy into battery RAM (to 1, before) or back from
 *                                it at power-up (to 0, after)
 *   ev_part_saved(part, 0)       Part Save (the current bank; Part Clear ends in it too)
 *   ev_part_cleared(part, 0)     Part Clear, before */

/* ---- project and card data: the Digitone's descriptor --------------------------------------------
 * A mod contributes to core_projdata (kept with the project) or core_carddata (kept on the card, for
 * every project) a pointer to one of these, and claims "projdata:<tag>" / "carddata:<tag>":
 *
 *     "contribute": [{"to": "core_projdata", "order": 50, "data": "00000000",
 *                     "relocs": [[0, "abs32", "sym:my_block", 0]]}],
 *
 * After each project load core puts the block's saved bytes in data and calls loaded(1); with none
 * saved (a new project, one saved without the mod, or a block of another size) data holds zeros and
 * loaded(0). A mod changes data as it likes and calls core_projdata_dirty(tag): core writes the
 * file about 2 s after the last change, and at SAVE PROJECT, from the engine task. */
struct core_projdata {              /* core_projdata and core_carddata */
    uint32_t tag;                   /* +0  four characters */
    uint32_t size;                  /* +4  bytes, a multiple of 4 */
    void *data;                     /* +8  the mod's copy */
    void (*loaded)(int32_t found);  /* +12 after a load (engine task); or 0 */
} __attribute__((aligned(4)));
void core_projdata_dirty(uint32_t tag);
void core_carddata_dirty(uint32_t tag);
#define OT_TAG(a, b, c, d)  ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* ---- battery RAM --------------------------------------------------------------------------------
 * CS1 keeps the firmware's working copy of the project across a power cycle; 0x100f859c..0x100fff00
 * (31,076 bytes) is a range stock never reads or writes. A mod contributes to ot_sram a pointer to an
 * ot_sram claim: at main's end (before ev_boot_done) core writes the claim's address to *where, or 0
 * when the range is full. The bytes are kept from one boot to the next while the build keeps the
 * same claims in the same order; a claim that moved is zeroed. Until then *where is 0. */
struct ot_sram {
    uint32_t tag;                   /* four characters */
    uint32_t size;                  /* bytes */
    void **where;                   /* core writes the address here */
};

/* ---- the engine task ----------------------------------------------------------------------------
 * The firmware does its card I/O in its engine task (priority 1), one job at a time; a file call from
 * the UI task races it. ot_engine_post queues fn(arg) to run there, between the firmware's jobs, and
 * returns 1, or 0 when core's queue (8) is full. Call it from a task, not an interrupt. */
int ot_engine_post(void (*fn)(uint32_t arg), uint32_t arg);

/* The firmware's buffered file calls, for the engine task (and ev_project, ev_banks_*,
 * ev_project_store and ev_project_reload, which run there). Paths are absolute ("/SET/PROJECT/x").
 * ot_file_read and ot_file_write return 1 when they moved all n bytes. */
struct ot_file { uint8_t obj[24]; uint8_t iob[4096]; } __attribute__((aligned(4)));
int ot_file_open(struct ot_file *f, const char *path, const char *mode);   /* "r", "w": <0 failed */
int ot_file_read(struct ot_file *f, void *dst, uint32_t n);
int ot_file_write(struct ot_file *f, const void *src, uint32_t n);
void ot_file_close(struct ot_file *f);
int ot_file_copy(const char *dst, const char *src);                          /* <0 failed */
int ot_mkdir(const char *path);
const char *ot_project_dir(void);   /* the loaded project's folder, "" with none */

/* What core's store has done, for a diagnostics page. */
enum {
    OTS_C_LOADED, OTS_C_NEW, OTS_C_WRITES, OTS_C_STRD, OTS_C_WRITE_ERR, OTS_C_RECOVERED,
    OTS_C_REFUSED, OTS_C_FRESH, OTS_C_KEPT_FULL, OTS_C_JOBS_FULL, OTS_C_CS1_FULL,
    OTS_C_LAST_ERR,                 /* the last failed open's result */
    OTS_C_N
};
extern uint32_t ots_counts[16];
extern char ots_last_path[64];      /* and its path */

/* ---- settings -----------------------------------------------------------------------------------
 * A mod contributes to ot_settings a pointer to each of its settings. The variable holds the
 * default and the mod reads it as it likes; with the settings companion (mods/settings-ot) in the
 * build the user edits it on the unit and it is kept on the card or with the project; without it
 * it stays the default. Core only declares the table. */
#define OT_SETTING_ABI      1u
enum { OT_SET_BINARY, OT_SET_OPTION, OT_SET_NUMBER };
enum { OT_SET_CARD = 0, OT_SET_PROJECT = 1, OT_SET_EARLY = 0x80 };   /* scope; | EARLY: also in CS1 */
struct ot_setting {
    uint32_t abi;                   /* OT_SETTING_ABI */
    uint32_t tag;                   /* the mod's four characters: with key, the setting's identity */
    uint16_t key;                   /* 1..65535, never reused for another meaning */
    uint8_t kind, scope;
    const char *group;              /* the menu's heading: the mod's name */
    const char *name;               /* the row's label */
    int32_t min, max;               /* NUMBER's range; OPTION: 0..labels-1; BINARY: 0..1 */
    const char *const *labels;      /* OPTION's labels (append new ones at the end), or 0 */
    volatile int32_t *value;        /* the mod's variable */
    int32_t def;                    /* its default */
    void (*changed)(const struct ot_setting *s);    /* after an edit or a load; or 0 */
};

/* ==== core-ot 0.7: voices ========================================================================= */

/* The frame interrupt's builder turns the sequencer's, the panel's and MIDI's requests (each track's
 * mailbox word) into the DSP command byte, the live lanes and the records the DSP receives; the voice
 * struct's routines reset and bind a track's sample. Octabam's synth machines, MUTE MODES, TEMPO SYNC,
 * PLOCKS P2 and the recorder fixes each detour a few of those places. Core gives each place an event.
 * Handlers use the C convention (d0-d1/a0-a1 are free). Except ev_voice_kill (tasks reach it too, with
 * every interrupt masked) they run in the frame interrupt (level 5, 362.8 us a frame, the worst near
 * 92%): keep them to a few dozen instructions. None may use the EMAC: the interrupt has
 * put the interrupted task's state away, but its builder goes on using the EMAC itself after them
 * (ev_frame and ev_frame_end, at the end of the interrupt, are the events where a handler may).
 *
 *   unsigned ev_voice_start(trk, cmd)    a DSP command was posted for the track this frame (cmd != 0,
 *                                        the byte at OT_DSP_CMD(trk)); return the command to use: the
 *                                        next handler gets it, and so do the packer and the DSP
 *   void ev_voice_release(trk, word)     the AMP release (OT_MBX_RELEASE in the track's mailbox word);
 *                                        the release bytes 0x80001828/9 are read after the handlers
 *   void ev_voice_kill(trk)              the voice was reset (the sequencer's STOP and pattern change,
 *                                        the sample loaders, the preview, a voice that has played to its
 *                                        end, a track with no sample): with every interrupt masked, from
 *                                        a task or the frame interrupt
 *   int ev_voice_bind(trk, voice)        the binder is about to write the voice's two arena pointers
 *                                        (OT_V_ARENA0/1); return nonzero to leave them as they were. The
 *                                        voice is then unbound: the renderer finds nothing left to play
 *                                        and resets it (ev_voice_kill), once or twice a frame
 *   int ev_voice_seek(trk, voice, same)  the binder is about to count a (re)start of the voice
 *                                        (OT_V_STARTS), which the frame builder reads as "new voice";
 *                                        same: the sample is the one the voice already has. Return
 *                                        nonzero to leave the counter alone (the DSP seeks the voice)
 *   void ev_trig_apply(trk, flags)       a trig's locks reached the track's live lane (the restore and
 *                                        apply paths joined); flags: bit 31 no apply, bit 6 no restore,
 *                                        bit 2 keep the trig's record (PLOCKS P2's reading of them)
 *   int ev_dsp_trig(trk, flags)          the voice record writer is about to build the track's per-trig
 *                                        flag bits in its DSP frame word (flags: bits 0-1 a trig, bit 2 a
 *                                        release; the byte at 0x80004858 + 8 t); return nonzero to build
 *                                        the word as for a frame with no trig
 *   void ev_dsp_record(trk, rec)         the writer has put the FX ids into the track's 64-byte record
 *                                        for the DSP (32 halfwords): a store here lands last in the frame
 */
#define OT_DSP_CMD(t)       OT_B(0x46104d15u + (t))     /* the DSP command byte posted for track t; 0: none */
#define OT_MAILBOX(t)       OT_L(0x46c80354u + 4 * (t)) /* what the builder is asked to do on track t */
#define OT_MBX_START        (1u << 2)
#define OT_MBX_RELEASE      (1u << 6)

/* The voice struct of track t (168 bytes), as longs: the arena entries the sample lives in, the count of
 * starts the frame builder compares, and the position the voice starts from. */
#define OT_VOICES           0x800049d8u
#define OT_VOICE_SIZE       168u
#define OT_VOICE(t)         ((volatile uint32_t *)(uintptr_t)(OT_VOICES + OT_VOICE_SIZE * (t)))
enum { OT_V_ARENA0 = 1, OT_V_ARENA1 = 2, OT_V_STARTS = 36, OT_V_SEEKPTR = 38 };      /* +4, +8, +144, +152 */

/* ==== core-ot 0.8: edits, Parts, the voice's reset, the boot, the screen ========================= */

/* ---- ev_clip: a pattern or an audio track through the clipboard or the undo buffer -----------------
 * The firmware moves a whole pattern (OT_PATTERN_SIZE bytes) or an audio track (OT_TRACK_SIZE) of the
 * current bank into its clipboard (FUNC+REC: COPY) or its undo buffer (the SNAPSHOT a paste or a
 * clear takes first), and back (PASTE, UNDO). A mod that keeps data per pattern or per track keeps a
 * clipboard and an undo copy of its own the same way, from these calls:
 *   int ev_clip(unsigned pre, const struct ot_clip *c)    in the sys task; pre 1 before, 0 after
 * Before a PASTE or an UNDO, a handler may return nonzero to refuse it: the bank is left as it was,
 * no later handler is asked, no "after" call comes, and stock goes on as when there is nothing to
 * paste or undo (an undo that finds nothing pastes the clipboard instead: that paste comes here too).
 * Any other return is ignored, and so is a refusal of a call stock did not make itself. A handler
 * must not call the copy and store routines. MIDI tracks and the other clipboard kinds (trigs,
 * Parts, slots) do not come here. */
enum { OT_CLIP_PATTERN = 0, OT_CLIP_TRACK = 1 };
enum {
    OT_CLIP_COPY     = 0,       /* bank -> clipboard */
    OT_CLIP_SNAPSHOT = 1,       /* bank -> undo buffer, before stock changes the pattern or track */
    OT_CLIP_PASTE    = 2,       /* clipboard -> bank */
    OT_CLIP_UNDO     = 3,       /* undo buffer -> bank */
};
struct ot_clip {
    uint8_t what;               /* OT_CLIP_PATTERN or OT_CLIP_TRACK */
    uint8_t how;                /* OT_CLIP_COPY .. OT_CLIP_UNDO */
    uint8_t bank;               /* the current bank, 0-15 */
    uint8_t pattern;            /* 0-15 */
    uint8_t track;              /* 0-7; 0xff for a pattern */
    uint8_t op;                 /* SNAPSHOT and UNDO: why stock took the snapshot, the number it keeps
                                   with the undo buffer (0x11 a pattern paste, 0x12 a pattern clear,
                                   7 a track paste); 0 for COPY and PASTE */
    uint8_t pad[2];
    void *buf;                  /* OT_CLIPBOARD or OT_UNDO_BUFFER */
};
#define OT_CLIPBOARD        ((void *)0x460c8122u)
#define OT_UNDO_BUFFER      ((void *)0x460bf218u)
#define OT_PATTERN_SIZE     0x8ed8u
#define OT_TRACK_SIZE       0x91au

/* ---- the Parts, and the project's start (0.8) ------------------------------------------------------
 * Handlers void f(unsigned a, unsigned b), in the sys task unless said:
 *   ev_part_saving(part, 0)      Part Save is about to copy the current bank's Part to its saved copy
 *                                (SAVE, SAVE ALL, and Part Clear's end); ev_part_saved follows
 *   ev_part_reload(how, part)    Part Reload (RELOAD, FUNC+CUE): how 1 before; after, 0 when it
 *                                reloaded and 2 when the Part had no saved copy ("SAVE PART FIRST!")
 *   ev_part_init(pre, part)      a Part is set to its defaults (part: a pointer, OT_PART_SIZE bytes in
 *                                any bank's RAM): the bank init of the engine task's project jobs, and
 *                                Part Clear. pre 1 before, 0 after, before stock's Part validator
 *   ev_project_init(job, 0)      in the engine task: it is about to empty every bank and set the
 *                                project's defaults, at the start of LOAD PROJECT (job 4), a new project
 *                                (9, 10, 13) and job 12, before anything is loaded; job: the stock job */

/* void ev_part_select(unsigned phase, const struct ot_part_select *s): the panel's Part change, in the
 * sys task, when the Part does change. BEGIN before anything moves (OT_PART is still the old Part);
 * APPLIED when every track has gone to the new Part, before the screen is redrawn; DONE after the
 * redraw and the track's machine page. machines: the old Part's eight machine bytes, from APPLIED. */
enum { OT_PSEL_BEGIN = 0, OT_PSEL_APPLIED = 1, OT_PSEL_DONE = 2 };
struct ot_part_select {
    uint8_t from, to;           /* the old Part and the new, 0-3 */
    uint8_t pad[2];
    const int8_t *machines;     /* 0 at BEGIN */
};

/* ---- the voice's reset, and the boot's start (0.8) --------------------------------------------------
 *   int ev_voice_reset(trk)      the track's voice is about to be reset (the routine ev_voice_kill fires
 *                                in, at its entry): return nonzero to leave the voice as it is. From the
 *                                frame interrupt and from tasks, with interrupts not yet masked: a
 *                                handler must be quick and may be entered again from the interrupt
 *   void ev_boot_early(void)     once, at boot: the core's code and data are in place and the on-chip
 *                                SRAM (0x80000000-0x80007fff) has its stock values. No task, interrupt
 *                                or OS call exists yet, and stock clears 0x46025de0-0x4763d580 next.
 *                                ev_boot_done comes later, with the tasks */

/* ---- the track icons (0.8) --------------------------------------------------------------------------
 * The eight audio tracks' boxes on the screen: each shows a state, which stock draws as a glyph and
 * draws again when it changes. In the task that draws the screen.
 *   unsigned ev_track_icon(trk, state)        return the state the box shows (chained). Stock's: bit 0
 *                                             the track plays, bits 1-2 its recorder; it has glyphs for
 *                                             0-3 and draws an empty box for the rest
 *   int ev_track_icon_draw(trk, state, x, y)  a state above 3: draw it (ot_blit at x, y; stock draws its
 *                                             7-wide glyphs at x, its 3-wide ones at x + 2) and return
 *                                             nonzero, or return 0 to leave the box empty */
struct ot_bitmap {
    uint32_t w, h;              /* at most 32 high */
    uint32_t one;               /* 1 */
    const uint32_t *bits;       /* w longs, a column each, its top pixel at bit 31 */
    const uint32_t *mask;       /* the same shape: the pixels drawn (set or cleared) */
};
#define OT_SCREEN_PLANE     ((void *)0x400bf10au)
#define ot_blit             ((void (*)(const struct ot_bitmap *, void *, int, int))0x400128a8u)

/* ---- the SETUP windows' knobs (0.8) -----------------------------------------------------------------
 * void ev_setup_dial(struct ot_dial *d): the SETUP window of AMP, FX1 or FX2 (the page-2 knobs) is
 * about to draw knob d->slot; a handler may change what it shows: the value, the widget's flags,
 * the widget itself. Handlers are chained. In the task that draws the screen, once a knob a draw. */
struct ot_dial {
    uint8_t page;               /* OT_P2_AMP, OT_P2_FX1 or OT_P2_FX2 */
    uint8_t slot;               /* 0-5 */
    uint8_t track, part;        /* OT_TRACK and OT_PART */
    int32_t value;              /* the value the knob shows: a signed byte (stock: the Part's) */
    uint32_t flags;             /* the widget's flags: stock passes 0 or 8 */
    const void *fmt;            /* the knob's formatter */
    void *widget;               /* widget(x, y, knob, value, flags, fmt, canvas) */
};

#endif
