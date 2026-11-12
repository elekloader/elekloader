/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1: the standard events, one prototype for each on every core that maps it. A mod opts in with
 * "events": "std1" in its mod.json; its "subscribe" entries then name these events, and the linker
 * finds each one's table in the core's "std" map (docs/ADAPTING.md, "Standard events (std1)"). A
 * mod without the flag gets the core's own events, as before; those never change.
 *
 *     #include <std1.h>
 *     static void my_tick(void) { ... }          "subscribe": [{"event": "ev_tick", "fn": "my_tick"}]
 *
 * Each event has one return kind and one context:
 *   observer   returns void.
 *   may take   returns int: nonzero and the stock does nothing, and later handlers are not called.
 *              An event with a pre argument takes only at pre 1.
 *   chained    the value comes as a pointer; each handler changes what the one before it left.
 *   BOOT before the OS. UI the task that draws and handles keys. TASK a named task (file calls
 *   allowed). MIDI the MIDI task. ISR an interrupt: keep it short and call no firmware. AUDIO the
 *   render interrupt.
 * pre: 1 before the change, 0 after. Tracks count from 0, the audio tracks first. A device's own
 * numbers (keys, LEDs, parameters, rates) are in its keys.h, leds.h, params.h and ports.h. Each
 * core's README says which of these events it maps and what it reports; a std mod that subscribes
 * to one its core does not map is refused when it is linked.
 *
 * The typedefs are named std1_<event>_fn, apart from the cores' own headers' <event>_fn.
 *
 * Not here yet: ev_transport, ev_tempo, ev_store, step fire, ev_enc, ev_param_value, ev_render_out
 * and ev_live_note, whose prototypes are still being settled. No core maps them under std1. */
#ifndef ELEKLOADER_STD1_H
#define ELEKLOADER_STD1_H

#include <stdint.h>

/* MIDI ports: the `port` of the MIDI events */
#define PORT_DIN   1u
#define PORT_USB   2u
#define PORT_PANEL 4u
#define PORT_ANY   0xffu

/* ev_key's flags */
#define KEY_PRESS   0x01u
#define KEY_FUNC    0x02u                   /* FUNC is held */
#define KEY_DOUBLE  0x04u
#define KEY_REPEAT  0x08u
#define KEY_RELEASE 0x10u

/* -- helpers every core that maps std1 events provides (ev_settings' and ev_draw's below)
 * core_redraw   any context: the frame is composed again at the next ev_tick.
 * core_post     any context, interrupts included: fn(arg) runs once, soon, in a task (the UI task
 *               on the cores so far); 0, or -1 when the queue is full.
 * core_timer    fn runs in that task every 2^k ticks of CORE_TICK_HZ, mask a single bit 1 << k
 *               (1 every tick, 2 every second, ...), as the kernels' soft timers take it. Again
 *               with the same fn changes its mask; CORE_TIMER_STOP removes it. 0, or -1 when
 *               full or mask is not one bit. */
void core_redraw(void);
int core_post(void (*fn)(void *arg), void *arg);
#define CORE_TIMER_STOP 0xffffffffu
int core_timer(void (*fn)(void), unsigned mask);

/* -- ev_settings' row: core_setting_add(menu, s) from an ev_settings handler adds one row after
 * the core's last, a separate row for each call. s stays the mod's: keep it (and name) in static
 * storage, since the row reads them for as long as the menu shows. name is the row's text (upper
 * case A-Z 0-9 space and - = & / # @ ? % $ ~, at most 15 characters), read again each time the
 * row is drawn; select runs in the UI task when the row is chosen (YES), with s. */
struct core_setting {
    const char *name;
    void (*select)(const struct core_setting *s);
};
void core_setting_add(void *menu, const struct core_setting *s);

/* -- ev_draw's helpers, for fb, ev_draw's argument. On every core x 0 is the left column and y 0
 * the top row (whatever the frame's own layout), and what falls off the frame is clipped.
 * core_px      one pixel: on 1 sets it, 0 clears it.
 * core_fill    a w x h box from (x, y): set (on 1) or cleared (on 0).
 * core_text    str in the device's own font, its box's top row at y: style is a size, TEXT_SMALL,
 *              _MEDIUM or _LARGE, an alignment, TEXT_LEFT (x the first column), _CENTRE or _RIGHT
 *              (x the last), and TEXT_INVERT for light text on a set box. The box is cleared first.
 *              Returns the text's width in pixels.
 * core_blit    a w x h one-bit image, rows top first, each row (w + 7) / 8 bytes, the left pixel
 *              in its first byte's top bit: 1 sets, 0 clears. */
#define TEXT_SMALL  0x000u
#define TEXT_MEDIUM 0x100u
#define TEXT_LARGE  0x200u
#define TEXT_LEFT   0x00u
#define TEXT_CENTRE 0x01u
#define TEXT_RIGHT  0x02u
#define TEXT_INVERT 0x10u
void core_px(void *fb, int x, int y, int on);
void core_fill(void *fb, int x, int y, int w, int h, int on);
int core_text(void *fb, int x, int y, unsigned style, const char *str);
void core_blit(void *fb, int x, int y, int w, int h, const uint8_t *bits);

struct core_clip;                           /* a clipboard operation (ev_clip), given by the core */

/* -- system */
typedef void (*std1_ev_boot_fn)(void);                                   /* BOOT, observer */
typedef void (*std1_ev_boot_done_fn)(void);                              /* TASK, observer */
typedef void (*std1_ev_idle_fn)(void);                                   /* TASK, observer: never block */
typedef void (*std1_ev_tick_fn)(void);                                   /* UI, observer, CORE_TICK_HZ */
typedef void (*std1_ev_crash_fn)(const void *frame);                     /* ISR, observer */
typedef void (*std1_ev_usb_fn)(unsigned configured);                     /* TASK, observer */

/* -- panel */
typedef int  (*std1_ev_key_fn)(unsigned key, unsigned flags);            /* UI, may take */
typedef void (*std1_ev_draw_fn)(void *fb);                               /* UI, observer */
typedef void (*std1_ev_key_leds_fn)(void *mgr);                          /* UI, observer */

/* -- views, menus and parameters */
typedef void (*std1_ev_settings_fn)(void *menu);                         /* UI, observer */
typedef int  (*std1_ev_view_fn)(int op, void *container, void *a, void *b);   /* UI, may take (push) */
typedef void (*std1_ev_mode_fn)(int mode, int old, int new_);            /* UI, observer */
typedef int  (*std1_ev_param_turn_fn)(void *view, int id, int delta, int knob);   /* UI, may take */
typedef void (*std1_ev_param_set_fn)(unsigned track, unsigned id, int32_t value); /* UI, observer */

/* -- MIDI; port is a PORT_* */
typedef int  (*std1_ev_midi_in_fn)(const uint8_t *msg, unsigned len, unsigned port);   /* MIDI, may take */
typedef int  (*std1_ev_sysex_in_fn)(const uint8_t *msg, unsigned len, unsigned port);  /* MIDI, may take */
typedef int  (*std1_ev_midi_cc_fn)(int track, int cc, int value, int flags);           /* MIDI, may take */
typedef int  (*std1_ev_midi_pc_fn)(int channel, int program, int flags);               /* MIDI, may take */
typedef int  (*std1_ev_midi_out_fn)(const uint8_t *msg, unsigned len, unsigned port);  /* ISR, may take */
typedef int  (*std1_ev_seq_midi_fn)(const uint8_t *msg, unsigned len, unsigned buffer);/* ISR, may take */

/* -- sequencer */
typedef void (*std1_ev_seq_tick_fn)(void);                                              /* ISR, observer */
typedef int  (*std1_ev_pattern_fn)(unsigned pre, unsigned pattern, unsigned how);       /* UI, may take at pre 1 */
typedef int  (*std1_ev_mute_fn)(unsigned track, unsigned muted, unsigned scope);        /* UI, may take */

/* -- storage */
typedef void (*std1_ev_project_fn)(int how, unsigned dropped);                          /* TASK, observer */
typedef int  (*std1_ev_clip_fn)(unsigned pre, const struct core_clip *c);               /* UI, may take at pre 1 */

/* -- audio */
typedef void (*std1_ev_render_in_fn)(void);                                             /* AUDIO, observer */
typedef void (*std1_ev_voice_start_fn)(unsigned voice, unsigned track);                 /* AUDIO, observer */
typedef void (*std1_ev_voice_end_fn)(unsigned voice);                                   /* AUDIO, observer */

#endif
