# test-looks-check (Analog Rytm MKII, core-ar2 0.15)

A test for the knob order and knob looks of machines in the TEST slots
(`machines.h`, `CORE_MACHINE_LOOKS`), on screen. `check.c`'s header says what
it does.

| machine | slot | what you see |
|---|---|---|
| TL PLAITS | 44 (BD TEST's records) | LEV on H; LPG and TUN draw as dials |
| TL SAMPLE | 45 (SD TEST's records) | LEV on H; END draws as a 0..127 dial, LOP as a switch |

The knobs without a look keep the test machine's own. Both machines are
silent.

It is a test, not for a project you keep.

```bash
python -m elekloader.sdk.build examples/test-looks-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
```
