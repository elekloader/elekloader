/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What core-dt2 1.1 (Digitakt II 1.17) and core-dn2 0.3 (Digitone II 1.12)
 * offer mods besides the hook bus of 1.0 / 0.2 (docs/ADAPTING.md, "Frames,
 * LEDs, project and kit data, value rows (core-dt2 1.1, core-dn2 0.3)").
 * Firmware routines return pointers in d0, where GCC reads a pointer result
 * from a0: the ones that return a pointer are declared returning uintptr_t. */
#ifndef MK2_CORE_H
#define MK2_CORE_H
#include <stdint.h>

/* ---- events (subscribe in mod.json) ---------------------------------------
 * ev_frame_params  void f(int16_t *values, int stride): interrupt level, each
 *                  frame; track t's value j at values[stride * t + j], added
 *                  to before the stock modulators. stride: DT2 71, DN2 101.
 * ev_key_leds      void f(void *mgr): the LED frame, before the views set
 *                  theirs; set with fw_led_set(mgr, logical, 2, value). */

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
 * blocks of every mod share the room a record has (DT2 191 bytes, DN2 334);
 * a block that does not fit is left out (core_kitdata_room() tells). */
#define CORE_KITS 128
typedef struct {
    char tag[4];
    uint32_t size;                           /* bytes */
    void *data;                              /* CORE_KITS blocks */
    void (*changed)(int kit, int found);     /* or 0 */
} core_kitdata_t;

int core_kit_on(void);                    /* the kit on (the pattern on's), -1 none */
int core_kit_index(const void *kit);      /* a kit in RAM -> its number, -1 not the project's */
uint32_t core_kitdata_room(void);         /* bytes a kit record holds for the blocks */
/* Times a save or a load met bytes in the mods' areas that are not core's (another
 * tool's, a later OS's): core left them as they were. */
extern volatile uint32_t core_store_conflicts;

/* ---- value rows in SETTINGS > PERSONALIZE (and SETTINGS) --------------------
 * From ev_personalize (or ev_settings): core_addvalue(menu, &row) adds a row
 * that shows label and, on the right as LED INTENSITY does, values[get()].
 * YES cycles it, LEFT and RIGHT step it; each change calls set(new) and
 * redraws the menu. row stays the mod's (static). */
typedef struct {
    const char *label;
    const char *const *values;
    uint32_t count;
    uint32_t (*get)(void);
    void (*set)(uint32_t value);
} core_value_t;
void core_addvalue(void *menu, const core_value_t *row);

/* ---- the UI ----------------------------------------------------------------- */
void core_notice(const char *msg);        /* the header's message, as a page's title; UI task */
uint32_t core_view_top(void);             /* the vtable of the view on top of the stack */

/* ---- firmware locations (mods/core-dt2/fw.s has the addresses) ------------- */
extern void fw_fillrect(void *bmp, int x0, int y0, int x1, int y1, int colour);
extern int fw_textf(void *bmp, const void *font, int x, int y, unsigned flags, const char *fmt, ...);
extern int fw_textbox(void *bmp, const void *font, int x, int y, int align, int deco, const char *tmpl,
                      const char *fmt, ...);
extern const char fw_font[], fw_font_narrow[], fw_font_menu[];
extern void fw_invalidate(void *view);
extern int fw_key_down(int code);
extern void fw_led_set(void *mgr, int logical, int layer, int value);
extern void fw_enc_init(void *block);
extern int fw_enc_filter(void *block, int knob, int count, const void *cfg, int pushed);
extern int fw_enc_list(void *block, void *event, const void *cfg);
extern const char fw_enc_param[], fw_enc_depth[], fw_enc_steps[];
extern uintptr_t fw_ui_states(void);
extern void fw_toast(void *ui, const char *fmt, ...);
extern uintptr_t fw_brain(void);
extern void fw_view_push(void *controller, void *shared_ptr, int flags);
extern void fw_name_view(void *view, void *title, void *name, int a, int len, int b, void *done);
extern uintptr_t fw_op_new(uint32_t bytes);
extern void fw_str_new(void *str, const char *cstr, void *alloc);
extern void fw_str_free(void *str);
extern void fw_sp_release(void *control);
extern const char fw_sp_inplace[], fw_vt_main[], fw_vt_func[], fw_vt_trk[];
extern int fw_card_read(uint32_t sector, uint32_t bytes, void *buf);
extern int fw_card_write(uint32_t sector, uint32_t bytes, void *buf);
extern void *fw_memcpy(void *dst, const void *src, uint32_t n);
extern char fw_project[], fw_patterns[], fw_kits[];
extern const char fw_pattern_size[], fw_kit_size[];   /* as numbers: (uint32_t)fw_kit_size */

#endif
