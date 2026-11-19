# sidecar-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2)'s moments, checked in
RAM: [sidecar-check-ar2](../sidecar-check-ar2)'s part 3 (its README,
"Part 3: moments (core 0.11)") with the A4's calls. The record `sc3` after
the magic `SCK3` (moments.c) has the Rytm's fields, and after them:

- `kit_data`: the project model's kit object's data pointer (model +0x16c
  +0x10), which should be `core_kit_item(0)`, and `kit_mirror`, the kit
  index sidecar-a4 reads (0x405f5ca4);
- a third handle field, `sc_h_kit` (kit, 16 bits), which command 3 sets to
  `arg` + 2 on the current kit; command 4's walk names its kit
  (`walk_kit`, `walk_kit_n`) and sets bit 3 of `walk_fields`.

There is no `ev_project_slot`. The SysEx handler is std1's `std_sysex_in`
(port 1 DIN, 2 USB). Commands and the readback over USB MIDI are the
Rytm's (its README, "Over MIDI"), for part 3 only.

In the emulator (OS 1.56, build SC81, 14 Nov 2026) the name, the current
kit, track and pattern and their items, `ev_kit` at a project load, the
handle walk and `core_midi_try_send`'s refusal while the port is not
configured read as expected.

```bash
python -m elekloader.sdk.build examples/sidecar-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
```
