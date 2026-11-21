/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Core 1.2 for the Digitakt II (OS 1.17), in C: what core exports and the events it calls, for a mod
 * that links more mods on the unit (TELOS). The SDK puts this folder on the include path:
 *
 *     #include "digitakt-mk2/core.h"
 *
 * Every name is resolved by the linker against the core in the build. Functions that return a pointer
 * are declared to return uint32_t: the firmware returns it in d0, where gcc for m68k looks in a0.
 * mods/core-dt2/README.md has each site and routine. */
#ifndef ELEKLOADER_DIGITAKT_MK2_CORE_H
#define ELEKLOADER_DIGITAKT_MK2_CORE_H

#include <stdint.h>

/* ---- the hook bus (core 1.0), as on the mk1 ------------------------------------------------------- */
typedef void (*ev_tick_fn)(void *ctrl);                 /* ctrl + 0x20 = 1: the frame is drawn again */
typedef void (*ev_draw_fn)(void *bmp, void *ctrl);      /* the 128x64 frame */
typedef int32_t (*ev_key_fn)(void *brain, void *ev);    /* nonzero takes the key */
typedef int32_t (*ev_enc_fn)(void *brain, void *ev);
typedef void (*ev_settings_fn)(void *menu);             /* add rows with core_additem(menu, row) */
extern void core_additem(void *menu, const void *row);


/* ---- core 1.1: events (subscribe in mod.json) ---------------------------------------
 * ev_frame_params  void f(int16_t *values, int stride): interrupt level, each
 *                  frame; track t's value j at values[stride * t + j]
 *                  (stride 71), added to before the stock modulators.
 * ev_key_leds      void f(void *mgr): the LED frame, before the views set
 *                  theirs; set an LED with fw_led_set(mgr, logical, 2, value). */
extern void fw_led_set(void *mgr, int logical, int layer, int value);

/* ---- project data: the project file's header (+0x1c .. +0x200) -------------
 * Contribute a pointer to the descriptor to the table core_projdata and claim
 * projdata:<tag>, as on core-dn1 2.1. Saved with every save of the project,
 * taken out after a load (loaded(1)), or zeros and loaded(0) when the project
 * has none (saved without the mod). */
typedef struct {
    char tag[4];
    uint32_t size;               /* bytes, a multiple of 4 */
    void *data;                  /* the mod's copy in RAM */
    void (*loaded)(int found);   /* or 0 */
} core_projdata_t;

/* ---- kit data: per kit, after each kit record's end mark ------------------
 * Contribute a pointer to the descriptor to the table core_kitdata and claim
 * kitdata:<tag>. data holds CORE_KITS blocks of size bytes, kit n's at
 * data + n * size (kit n is pattern n's). Core saves each kit's blocks with
 * the kit record (a project save, SAVE KIT, ...), takes them out when one is
 * loaded, and copies them when the firmware copies a kit in RAM (pattern
 * copy and paste, PASTE KIT, undo), through the clipboard and undo buffers
 * too. Then it calls changed(kit, found): found 0 when the kit had none for
 * this layout (saved without the mod, or with another set of kit-data mods),
 * and the block holds zeros. changed may run in any task: keep it short. The
 * blocks of every mod share the room a record has (191 bytes); a block that
 * does not fit is left out (core_kitdata_room() tells). */
#define CORE_KITS 128
typedef struct {
    char tag[4];
    uint32_t size;                           /* bytes */
    void *data;                              /* CORE_KITS blocks */
    void (*changed)(int kit, int found);     /* or 0 */
} core_kitdata_t;

int core_kit_on(void);                    /* the kit on (the pattern on's), -1 none */
uint32_t core_kitdata_room(void);         /* bytes a kit record holds for the blocks */
/* Times a save or a load met bytes in the mods' areas that are not core's (another
 * tool's, a later OS's): core left them as they were. */
extern volatile uint32_t core_store_conflicts;

/* ---- value rows in SETTINGS > PERSONALIZE (and SETTINGS) --------------------
 * From ev_personalize (or ev_settings): core_addvalue(menu, &row) adds a row
 * that shows label and, on the right as LED INTENSITY does, values[get()].
 * Only LEFT and RIGHT step it (YES does nothing); each change calls set(new) and
 * redraws the menu. row stays the mod's (static). */
typedef struct {
    const char *label;
    const char *const *values;
    uint32_t count;
    uint32_t (*get)(void);
    void (*set)(uint32_t value);
} core_value_t;
void core_addvalue(void *menu, const core_value_t *row);

/* ---- the UI task (core 1.2; fw_op_new and fw_invalidate since 1.1) ---------------------------------------------------------------------- */
extern void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
/* printf text (128 bytes); flags 0 plain, bit 1 centre, bit 2 right-align. Not the mk1's maxlen. */
extern int32_t fw_textf(void *bmp, const void *font, int32_t x, int32_t y, uint32_t flags, const char *fmt, ...);
extern const char fw_font5[];                           /* the stock small font */
extern void fw_blit(void *dst, const void *src, int32_t x, int32_t y, int32_t centre);
extern uint32_t fw_op_new(uint32_t size);               /* the firmware's heap */
extern void fw_invalidate(void *view);                  /* the view is drawn again */

/* ---- the system's moments (core 1.2) -------------------------------------------------------------- */
/* ev_card_ready: the +Drive is up (status 0; nonzero, it failed), nothing else is: the init task, before
 * the project and the UI task. A handler must not take the firmware's heap. */
typedef void (*ev_card_ready_fn)(int32_t status);
/* ev_fs_ready: after ev_card_ready, the +Drive's file system mounted (nonzero) or not; still before the
 * project and the UI task. fw_fs_* work from here on, from a task, never an interrupt. */
typedef void (*ev_fs_ready_fn)(int32_t mounted);
/* A handle: {node, position (set it to seek), writing, open}. mode "r", or "w" (made if missing, never
 * truncated). read/write -> bytes, or -1; the rest 0 or -1. Writes go through to the card. */
struct fw_fs_file { uint32_t node, pos, writing, open; };
extern int32_t fw_fs_open(const char *path, const char *mode, struct fw_fs_file *f);
extern int32_t fw_fs_read(void *buf, uint32_t len, struct fw_fs_file *f);
extern int32_t fw_fs_write(const void *buf, uint32_t len, struct fw_fs_file *f);
extern int32_t fw_fs_close(struct fw_fs_file *f);
extern int32_t fw_fs_unlink(const char *path);
extern int32_t fw_fs_mkdir(const char *path);
extern int32_t fw_fs_rename(const char *from, const char *to);
/* Project slots, in a background worker task. ev_project_slot_write: slot (0-127; 128 the working copy)
 * is about to be written: a SAVE, a SAVE AS, or the working copy's after a load. ev_project_saved: that
 * write returned (ok nonzero: written). ev_project_loaded: a LOAD PROJECT finished (err 0: LOAD OK), slot
 * the project's; the boot's own load of the working copy does not call it. */
typedef void (*ev_project_slot_write_fn)(void *project, int32_t slot, int32_t progress);
typedef void (*ev_project_saved_fn)(void *project, int32_t slot, int32_t ok);
typedef void (*ev_project_loaded_fn)(int32_t slot, int32_t err);
/* ev_crash: an exception the firmware does not handle, interrupts masked; the stock printer runs after the
 * handlers. A handler that wants the unit back calls core_restart_now. */
typedef void (*ev_crash_fn)(void *frame);
/* ev_sysex: each whole SysEx message, F0 to F7, before the stock routing, which always goes on (stock
 * ignores F0 7D). source 2/4 USB, 8/16 DIN. The MIDI task: a handler that blocks holds every MIDI input. */
typedef void (*ev_sysex_fn)(const uint8_t *msg, uint32_t len, uint32_t source);
extern void core_restart(void) __attribute__((noreturn));       /* the firmware's own restart, from a task */
extern void core_restart_now(void) __attribute__((noreturn));   /* at once: from ev_crash, or interrupts masked */
/* port 1 the settings' output, 2 USB, 3 DIN, 4 both; nonzero: sent */
extern int32_t fw_sysex_send(const uint8_t *buf, int32_t len, volatile uint8_t *abort, int32_t port);
/* The OS's fifteen static tasks: {count, the init task's row, the TCB's saved-SP offset}, then {TCB,
 * stack's low end, stack size} each. */
struct core_task { unsigned long tcb, stack, size; };
struct core_tasks { unsigned long n, init, sp_at; struct core_task t[16]; };
extern const struct core_tasks core_tasks;

#endif
