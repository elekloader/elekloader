# std-midi-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2) 0.7's standard MIDI events
and `ev_seq_tick`: a std1 mod (`"events": "std1"`, docs/ADAPTING.md,
"Standard events (std1)") that subscribes to `ev_midi_in`, `ev_sysex_in`,
`ev_midi_cc`, `ev_midi_pc`, `ev_midi_out`, `ev_seq_midi` and `ev_seq_tick`.
What it records, in its exported `std_midi`, is in the comment at the top of
`midi.c`. It takes one message: the marker, CC 119 with value 119, in
`ev_midi_in`, so `ev_midi_cc` (and stock) never see it.

Not run yet.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/std-midi-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/std-midi-check-a4mk2/out/std-midi-check-0.1.elemod \
    --out a4-std-midi.syx --version SM56
```
