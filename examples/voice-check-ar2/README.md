# voice-check (Analog Rytm MKII)

A test for [core-ar2](../../mods/core-ar2) 0.17's std1 voice events:
[voice-check-a4mk2](../voice-check-a4mk2) for the Rytm's eight voices and
twelve tracks. A std1 mod that subscribes to `ev_voice_start` and
`ev_voice_end`, which run in the audio interrupt, and only counts them; what
it records in `voice_check` is in the comment at the top of `voice.c`. It
changes nothing the unit does.

Not yet run in an emulator or on a unit.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/voice-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.18.elemod --mod examples/voice-check-ar2/out/voice-check-0.1.elemod \
    --out ar-voice.syx --version VC74
```
