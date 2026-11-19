/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.6: screen calls (docs/ADAPTING.md, "Screen calls (core-a4mk2
 * 0.6, Analog Four MKII)"), core-ar2 0.13's on the A4. Text in stock's own
 * fonts, a key handed back to stock, whether a stock overlay is up, the
 * track level readout and stock's progress box. A mod that uses them asks
 * for "core": "0.6".
 *
 * All of them are for the UI task: ev_tick, ev_draw, ev_key, ev_enc and
 * what those call.
 */
#ifndef ELEKLOADER_A4_SCREEN_H
#define ELEKLOADER_A4_SCREEN_H

#include <stdint.h>

/* ---- text in stock's fonts ----
 * A bitmap is stock's 28-byte record. ev_draw's bmp is one (the frame,
 * 128 x 64); core_bitmap_init makes one over a mod's own buffers, w x h
 * pixels in columns of h / 8 bytes (128 x 64: 1 KB), mask 0 or a buffer of
 * the same size. The record doesn't own the buffers and needs no undoing.
 * y counts from the bottom (0 the lowest row), as in every bitmap call:
 * for text whose top is on glass row t, pass y = 64 - core_font_height(f)
 * - t. */
#define CORE_BITMAP_SIZE 28
void core_bitmap_init(void *bmp, int w, int h, void *bits, void *mask);

/* fonts 0..16, stock's 17, in the Rytm's order; another number draws
 * nothing. core_font_height and
 * core_font_descent give each one's size. */
#define CORE_FONTS 17
#define CORE_FONT_MAIN 16

/* str at (x, y) in font: align CORE_ALIGN_LEFT starts at x, _CENTRE
 * centres on x, _RIGHT ends at x; | CORE_TEXT_INVERT inverts the text's box
 * after drawing (light text on a set bar, as stock's title bar). The box
 * (width x height + descent) is cleared first. str is drawn as it is, at
 * most 127 characters: a '%' in it is a '%'. */
#define CORE_ALIGN_LEFT 0
#define CORE_ALIGN_CENTRE 1
#define CORE_ALIGN_RIGHT 2
#define CORE_TEXT_INVERT 0x10
void core_text_draw(void *bmp, int font, int x, int y, int align, const char *str);
int core_text_width(int font, const char *str);
int core_font_height(int font);
int core_font_descent(int font);

/* ---- stock key pass-through ----
 * ev (a KeyEvent, CORE_KEY_EVENT_SIZE bytes: +12 the key, +16 the flags)
 * to stock alone: from inside ev_key it skips the other mods' handlers;
 * later (a press the mod held back, then let go) it is as if the panel had
 * sent it then. Keep your own copy of an event you replay: ev_key's is
 * gone when the handler returns. Returns 0 once stock had it (the A4's
 * Brain::key doesn't pass on whether a view took it), or -1 before the
 * boot's first key event (core learns where stock's key handler is from
 * it). */
#define CORE_KEY_EVENT_SIZE 24
int core_key_native(void *ev);

/* ---- overlays ----
 * What covers stock's main screen now, as bits:
 *   CORE_OVERLAY_VIEW    a stock menu or overlay view stacked over it (a
 *                        ...MenuView: TEMPO, SCALE, the FUNC + key menus,
 *                        TOTAL RECALL; a browser, ...); the quick mute and
 *                        keyboard modes' views are not
 *   CORE_OVERLAY_WINDOW  a stock window: a popup, a confirm box, a value
 *                        popup, a progress box (core's too)
 *   CORE_OVERLAY_SCREEN  the main screen isn't up at all (a whole-screen
 *                        menu such as GLOBAL, or the boot)
 * Classes are known by their names, so an overlay of a class not in the
 * lists counts as none. 0 until the first ev_tick. */
#define CORE_OVERLAY_VIEW 1
#define CORE_OVERLAY_WINDOW 2
#define CORE_OVERLAY_SCREEN 4
int core_stock_overlay(void);

/* ---- the track level readout ----
 * The Rytm's: whether and where the A4 shows its track level after a LEVEL
 * turn is not yet seen on a unit (the emulator shows none). Core keeps the
 * Rytm's field, glass rows 0..8, columns 0..69. core_level_readout is the
 * ticks left of core's window for it (47 ticks, about 2.35 s, from the last
 * LEVEL turn that reached stock; 0: none). Stock draws its whole screen
 * before ev_draw, so a mod that draws over the header keeps the readout by
 * saving the field before it draws and putting it back after:
 *
 *     uint8_t keep[CORE_LEVEL_FIELD_SIZE];
 *     int lev = core_level_readout();
 *     if (lev) core_level_field_save(bmp, keep);
 *     ... draw the page ...
 *     if (lev) core_level_field_put(bmp, keep);
 *
 * Both take 128 x 64 bitmaps only. */
#define CORE_LEVEL_FIELD_SIZE 140
#define CORE_LEVEL_FIELD_W 70
#define CORE_LEVEL_FIELD_H 9
int core_level_readout(void);
void core_level_field_save(void *bmp, void *keep);
void core_level_field_put(void *bmp, const void *keep);

/* ---- stock's progress box ----
 * The box SAVE PROJECT shows: a title and a bar. core_progress_open
 * shows one and returns its handle, or -1 (four already open, or before
 * the first ev_tick). core_progress_set fills the bar to done of total.
 * core_progress_close takes it away and frees the handle. The box takes no
 * keys: catch NO in your ev_key and close it. It counts as CORE_OVERLAY_WINDOW. */
int core_progress_open(const char *title);
void core_progress_set(int handle, int done, int total);
void core_progress_close(int handle);

#endif
