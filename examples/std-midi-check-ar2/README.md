# std-midi-check (Analog Rytm MKII)

A test for [core-ar2](../../mods/core-ar2) 0.17's standard MIDI events and
`ev_seq_tick`: [std-midi-check-a4mk2](../std-midi-check-a4mk2)'s mod, built
against the Rytm's `ports.h`. That README says what it subscribes to and the
one message it takes; `midi.c` says what it records in `std_midi`.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/std-midi-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.17.elemod --mod examples/std-midi-check-ar2/out/std-midi-check-0.1.elemod \
    --out ar-std-midi.syx --version SM74
```

## Not run yet

Built in the cloud with core-ar2 0.17 and linked; not yet run in the Rytm
emulator or on a unit. What the runs must confirm: CCs on a track's channel,
on the FX track's, on the auto channel (flag 1) and on the PERF channel
(track -1, flag 4), each applied by stock; the marker taken before
`ev_midi_cc`; note, NRPN, bend and aftertouch through `ev_midi_in` over DIN
and USB; SysEx; program changes, with `CORE_MIDI_PC_STOCK` only while
program changes are received on their channel; a knob's CC out and its
port mask; MIDI clock out and `ticks_per_tick`; and the sequencer's notes
in `ev_seq_midi` on a track with a MIDI channel set.
