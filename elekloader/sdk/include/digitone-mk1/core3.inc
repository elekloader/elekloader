| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 3.0 to 4.2 for the Digitone mk1, in assembly: the layouts that go
| with the firmware locations core exports (core3.h has the same in C).
| Assembly names the locations directly: fw_voice_pitch, fw_kit, fw_fillrect,
| ... A mod that uses them needs core-dn1 3.0: "resources": {"core": "3.0"};
| one that uses a 3.1 name (fw_voices, core_sound_set, ...) needs 3.1, a
| 3.2 one (fw_tempo, core_audio, ev_midi_cc) 3.2, and a 3.3 one
| (ev_key_leds, ev_render_params, fw_memcpy, fw_textbox, fw_pattern,
| __ashldi3, ...) 3.3, CORE_AUDIO_INSERT and ev_midi_pc's CORE_MIDI_PC_STOCK 4.0,
| and ev_note_queue's event (FW_EVENT_*) 4.1.
| The SDK puts this folder on the include path:
|
|       .include "digitone-mk1/core3.inc"

        .equ    FW_VOICE_PARAMS_SKIP, 18        | fw_voice_params + 18 + 158 v + 2 k: slot k of voice v
        .equ    FW_VOICE_STRIDE,  158
        .equ    FW_LFO_STRIDE,    0x50          | fw_lfo_state + 0x50 v: LFO1 of voice v; LFO2 0x28 on
        .equ    FW_LFO2,          0x28
        .equ    FW_LFO_DEST,      0x40          | an LFO's destination slot
        .equ    FW_LFO_MOD,       0x44          | its modulation, 8.8, signed
        .equ    FW_SOUND_SLOTS,   0x2c          | (fw_kit) + 0x2c + 326 t + 2 k: track t's sound slot k
        .equ    FW_SOUND_STRIDE,  326
        .equ    CORE_MENU_ICON,   0x49434F4E    | "ICON": a core_menu entry's +8, then +12 -> its icon
| 3.1
        .equ    FW_VOICE_BLOCK,   128           | fw_voices + 128 v: voice v's 32 samples, Q1.31
        .equ    FW_PARAM_REC,     60            | fw_params + 60 id: a parameter record
        .equ    FW_UI_REC,        84            | fw_uirecs + 84 id: its UI record
        .equ    CORE_LOOK_NONE,   -1            | core_param_ui_make's look: id 0's ('none')
| 3.2: a core_audio record (core3.h, struct core_audio_owner)
        .equ    CORE_AUDIO_ON,     0             | nonzero: this mod has the output
        .equ    CORE_AUDIO_RENDER, 4             | render(out, in), each block
        .equ    CORE_AUDIO_FLAGS,  8
        .equ    CORE_AUDIO_MUTE_VOICES, 1        | skip the voices' filters too
        .equ    CORE_AUDIO_INSERT, 2             | 4.0: after the master stage, on its output
| 4.0: core_audio_caps, a long, the flags core_audio takes (weak-import it: 0 before 4.0)
        .equ    CORE_MIDI_CC_AUTO, 1             | ev_midi_cc's flags: on the auto channel
        .equ    CORE_MIDI_PC_STOCK, 2            | 4.0: ev_midi_pc's: the stock would apply it
| 3.3: ev_render_params' words
        .equ    CORE_PARAMS_STRIDE, 158          | words + 158 v + 2 k: slot k of voice v (8.8)
        .equ    CORE_PARAMS_FX,     8            | the global FX's record
| 3.3: the active pattern (fw_pattern)
        .equ    FW_TRACK_STRIDE,  976           | (fw_pattern) + 976 t: track t's data
        .equ    FW_STEP_TRIG,     1             | its step words (+2 s): a note trig
        .equ    FW_STEP_LOCK_TRIG, 2            | a lock trig
        .equ    FW_TRACK_NOTE,    0x3c0         | NOTE
        .equ    FW_TRACK_LENGTH,  0x3c5         | the per-track length, big-endian, unaligned
        .equ    FW_TRACK_ROOT,    0x3cb         | the keyboard's ROOT
        .equ    FW_TRACK_SCALE,   0x3ce         | its SCALE, -1 chromatic
| 4.1: ev_note_queue's event and its p-lock list
        .equ    FW_EVENT_KIND,    0             | what it is, a long: 0 a step's fire, 1 the first step's before PLAY
        .equ    FW_EVENT_TRACK,   8             | its track, a long
        .equ    FW_EVENT_STEP,    0x28          | the step word it came from: bit 0 a note trig, bit 1 a lock trig
        .equ    FW_EVENT_LOCKS,   0x38          | -> its p-lock list, or 0
        .equ    FW_LOCKS_COUNT,   8             | the list's entries, a long
        .equ    FW_LOCKS_PAIRS,   0x14          | from here {u16 slot, s16 value}, FW_LOCKS_STRIDE apart
        .equ    FW_LOCKS_STRIDE,  8

| 4.2, OS1.44: core_sound_set also notifies persistent SoundRef.
| core_step_selection(two_words) and core_lock_selected(track,slot,value).
| Names use core_param_override; optional format/visibility providers
| contribute 16-byte {&core_param_present_v2,name,format,visible} to core_param_presenters.
| fw_view_param_value(view,param_id,locked_byte) reads a native parameter view.
| locked_byte must be non-null. UI task; normal gcc integer ABI. See core3.h.
| fw_voice_pitch_processed[voice] is the IRQ's processed pitch; fw_project_get,
| fw_sound_ref, fw_ui_get, fw_ui_grid_edit, fw_ui_step_edit, fw_key_held,
| fw_track_get/length/has_note/has_lock/set_trig/set_lock, fw_ui_refresh,
| fw_syn_key, fw_syn1_draw/fw_syn2_draw, fw_sound_change_event_vtable,
| fw_param_value_text and fw_lfo_name_format are the measured native exports.
| Firmware pointer results are in D0. fw_string_ctor(result,text,allocator)
| constructs the native string; fw_lfo_choices(owner,id) has a hidden result
| in A0, returns it in D0, and is not a gcc C pointer-return call.
