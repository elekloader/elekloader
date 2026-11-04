| SPDX-License-Identifier: GPL-2.0-or-later
| core 3.0 (Digitakt mk1): firmware locations as exports. A mod that names
| these instead of the addresses rebuilds for another OS version unchanged:
| each OS's port in mod.json (defsym FW_*) gives their values, and the
| linker resolves the names against the core in the build. In C, declare
| them as arrays or functions (elekloader/sdk/include/digitakt-mk1/core3.h):
|   extern int32_t fw_track_blocks[];  ...  fw_track_blocks[32 * t + i]
| Values are OS 1.53's in the comments. Each was checked against 1.54 by the
| code that uses it (docs/ADAPTING.md, "Firmware locations").
|
| The render (interrupt level, 1500 times a second; SRAM, 32-sample blocks):
|   fw_track_blocks    0x80001a18  each track's block, 32 Q31 samples, 128 B a track
|   fw_render_machine  0x800018bc  the machine each track renders as, a byte a track
|   fw_voice_params    0x80002794  the machine's 8 parameters A-H, 8.8, 106 B a track
|   fw_voice_note      0x80001f28  the trig's note, 16.16 (60 << 16 when none), a long a track
|   fw_voice_vel       0x80001f18  its velocity, 8.8, a word a track
|   fw_voice_start     0x80001228  bit t: track t starts a voice this block
|   fw_amp_env         0x4199df54  the AMP envelope: phase +0, level +4, 12 B a track
|   fw_pitch_tab       0x4019b1c0  2^((i - 10752) / 2048) in Q29, i = 0-14848
| The UI (its task):
|   fw_active_track    0x4197b6b4  the active track, 0-7 (a long)
|   fw_kit             0x4199dc44  -> the UI kit: track t's sound at +0x20 + 0xa2 t,
|                                  its machine at the sound's +0x7e
|   fw_slice_layout    0x4197cf5c  SLICE's SRC page layout (44 B: the knobs' ids at +8)
|   fw_set_param       0x400771e8  set_param(value 8.8, track, slot): the knob's path
|                                  into the engine's copy
|   fw_bitmap_vt       0x401b73b4  the Bitmap vtable (an icon's first long)
|   fw_fillrect        0x400c19a6  fillrect(bmp, x0, y0, x1, y1, colour)
|   fw_framerect       0x400c178a  framerect(bmp, x0, y0, x1, y1, colour): an outline
|   fw_textf           0x400c257c  textf(bmp, font, x, y, maxlen, fmt, ...)
|   fw_font5           0x40200b0c  the stock 5-pixel font
|   fw_blit            0x400c2960  blit(dst, src, x, y, centre)
|   fw_op_new          0x400d4180  operator new(size) -> d0, 0 when the heap is full
| From 4.1 (master.s):
|   fw_invalidate      0x400c9812  invalidate(view): the view is drawn again (after a
|                                  change a mod page shows)

        .macro  FW name, value
        .globl  \name
        .set    \name, \value
        .endm

        FW      fw_track_blocks, FW_TRACK_BLOCKS
        FW      fw_render_machine, RENDER_MACHINE
        FW      fw_voice_params, FW_VOICE_PARAMS
        FW      fw_voice_note, FW_VOICE_NOTE
        FW      fw_voice_vel, FW_VOICE_VEL
        FW      fw_voice_start, FW_VOICE_START
        FW      fw_amp_env, FW_AMP_ENV
        FW      fw_pitch_tab, FW_PITCH_TAB
        FW      fw_active_track, FW_ACTIVE_TRACK
        FW      fw_kit, FW_KIT
        FW      fw_slice_layout, FW_SLICE_LAYOUT
        FW      fw_set_param, FW_SET_PARAM
        FW      fw_bitmap_vt, FW_BITMAP_VT
        FW      fw_fillrect, FW_FILLRECT
        FW      fw_framerect, FW_FRAMERECT
        FW      fw_textf, FW_TEXTF
        FW      fw_font5, FW_FONT5
        FW      fw_blit, BLIT
        FW      fw_op_new, OP_NEW
        FW      fw_invalidate, FW_INVALIDATE

| From 4.2: the sequencer's time.
|   fw_tempo           0x4020db60  the tempo x 120 (87.0 BPM: 10440), as the
|                                  sequencer runs it (1.54: 0x4020df10)
|   fw_timeline        0x80001f4c  the timeline: 5,400,000 units a 16th; the render
|                                  moves it twice the tempo every block
|   fw_transport       0x4199dc2c  0 stopped, 1 playing (1.54: 0x4199ec2c)
        FW      fw_tempo, FW_TEMPO
        FW      fw_timeline, FW_TIMELINE
        FW      fw_transport, FW_TRANSPORT

| From 5.0:
|   fw_sysex_send      0x400d6e1e  send(buf, len, volatile uint8_t *abort, port) -> nonzero: sent; in pieces of at most 144 bytes (1.54: 0x400d7046)
        FW      fw_sysex_send, SYSEX_SEND

| From 5.0:
|   fw_led_set         0x400083a0  set(brain, led, state, colour): an LED for this frame (in ev_key_leds; brain = its container - 0x40); the first setter wins (1.54: 0x400083a0)
        FW      fw_led_set, FW_LED_SET

| From 5.0:
|   fw_sampler_state   0x4199e114  the recorder's state, a long: 0 idle, 1 armed, 2 recording, 3 stopped, 4 a take kept (1.54: 0x4199f114)
        FW      fw_sampler_state, SAMPLER_STATE
