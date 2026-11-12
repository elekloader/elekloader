# voice-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2) 0.7's std1 voice events: a
std1 mod (`"events": "std1"`, docs/ADAPTING.md, "Standard events (std1)")
that subscribes to `ev_voice_start` and `ev_voice_end`, which run in the
audio interrupt, and only counts them. What it records in its exported
`voice_check` is in the comment at the top of `voice.c`: per voice the
starts, the ends and the last track, the voices playing, and the calls that
broke the pairing (an end with no start, a second start, a voice or track
out of range). It changes nothing the unit does.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/voice-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/voice-check-a4mk2/out/voice-check-0.1.elemod \
    --out a4-voice.syx --version VC56
```
