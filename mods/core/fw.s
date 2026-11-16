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
|   fw_amp_env         0x4199df54  the FILTER envelope (not the AMP one, despite the
|                                  name, kept so mods built with it link): phase +0,
|                                  level +4, 12 B a track; filled by 0x40073304 from
|                                  the filter page's attack, decay, sustain, release
|                                  and delay (voice bytes 76, 78, 80, 82, 88)
|   fw_filter_env      0x4199df54  the same, by its right name (4.4)
|   fw_amp_level       0x4199dd64  the AMP envelope (4.4): phase +0, value +4, 8 B a
|                                  track, kept by the amp stage 0x400716c0 from the
|                                  AMP page's attack, hold and decay. Phase 2 attack
|                                  (the value rises, negated Q31, to 0x80000000),
|                                  1 hold (the value counts time; full level), 0
|                                  decay (the value is the level, negated Q31, falling
|                                  towards 0). All 0 when idle.
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
        FW      fw_filter_env, FW_AMP_ENV
        FW      fw_amp_level, FW_AMP_LEVEL
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

| From 4.5 (system.s): the +Drive's own routines, below its file system, for
| a mod that keeps data in sectors nothing else uses (TELOS). A task, never
| an interrupt; the card is up from ev_card_ready.
|   fw_card_read       0x400e29ec  read(sector, bytes, buf) -> 0 or < 0 (512-byte sectors; 1.54: 0x400e2c14)
|   fw_card_write      0x400e2bd2  write(sector, bytes, buf) -> 0 or < 0 (1.54: 0x400e2dfa)
|   fw_card_erase      0x400e2904  erase(first, count) -> 0 or < 0 (1.54: 0x400e2b2c)
|   fw_card_sectors    0x400e260c  sectors() -> the card's size, -1 before it is up (1.54: 0x400e2834)
|   fw_sysex_send      0x400d6e1e  send(buf, len, volatile uint8_t *abort, port) -> nonzero: sent;
|                                  in pieces of at most 144 bytes; port 1 the settings' output,
|                                  2 USB, 3 DIN, 4 both (1.54: 0x400d7046)
        FW      fw_card_read, FW_CARD_READ
        FW      fw_card_write, FW_CARD_WRITE
        FW      fw_card_erase, FW_CARD_ERASE
        FW      fw_card_sectors, FW_CARD_SECTORS
        FW      fw_sysex_send, SYSEX_SEND
| From 4.5 (system.s): the +Drive's file system (ekFS), from ev_fs_ready on.
| A task, never an interrupt, and never from inside another fs call: its
| mutex is not recursive. Paths from "/", at most 255 bytes. A handle is four
| words {node, position, writing, open}; set the position word to seek.
| ekFS writes through to the card, 16 KB blocks.
|   fw_fs_open    0x400cf178  open(path, mode, handle[4]) -> 0 or -1: mode "r", or "w"
|                             (the file made if it is not there; never truncated)
|   fw_fs_read    0x400ceebe  read(buf, len, handle) -> bytes read, or -1
|   fw_fs_write   0x400cef1e  write(buf, len, handle) -> len, or -1 (grows the file)
|   fw_fs_close   0x400cf136  close(handle) -> 0
|   fw_fs_unlink  0x400cdb28  unlink(path) -> 0 or -1 (files only)
|   fw_fs_mkdir   0x400cde0c  mkdir(path) -> 0 or -1 (there already, too)
|   fw_fs_rename  0x400cd902  rename(from, to) -> 0 or -1
|   fw_fs_stat    0x400cdc36  stat(path, out[4]) -> 0 (out[0] its node) or -1
| 1.54: each + 0x228.
        FW      fw_fs_open, FS_OPEN
        FW      fw_fs_read, FS_READ
        FW      fw_fs_write, FS_WRITE
        FW      fw_fs_close, FS_CLOSE
        FW      fw_fs_unlink, FS_UNLINK
        FW      fw_fs_mkdir, FS_MKDIR
        FW      fw_fs_rename, FS_RENAME
        FW      fw_fs_stat, FS_STAT
