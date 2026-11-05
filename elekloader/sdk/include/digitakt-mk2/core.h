/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What core-dt2 1.1 (Digitakt II 1.17) offers mods besides 1.0's hook bus
 * (docs/ADAPTING.md, "Project data, kit data and value rows (core-dt2 1.1, Digitakt II)"). */
#ifndef DIGITAKT_MK2_CORE_H
#define DIGITAKT_MK2_CORE_H
#include <stdint.h>

/* ---- events (subscribe in mod.json) ---------------------------------------
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

#endif
