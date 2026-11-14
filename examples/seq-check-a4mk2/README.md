# seq-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2)'s sequencer events
("The sequencer"): it subscribes to `ev_seq_step` and `ev_transport`
and records their calls, masks and timing, with `core_transport` at each
step and, from 0.2, the getters `core_pattern_now`, `core_pattern_next`,
`core_tempo` and `core_track_speed/step/length`, in its exported `seq_check` (the layout is in the comment at the top
of `seq.c`), for an emulator's or a unit's memory read. Nothing else
changes.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/seq-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.8.elemod --mod examples/seq-check-a4mk2/out/seq-check-0.2.elemod \
    --out a4-seq.syx --version SQ56
```
