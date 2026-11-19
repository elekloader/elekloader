# usb-gain-check (Analog Rytm MKII, core-ar2 0.15)

A test for core-ar2's held USB input gain (`core_usb_in_gain`, `usbin.s`,
docs/ADAPTING.md "USB input gain (core-ar2 0.15, Analog Rytm MKII)"), on
screen. `check.c`'s header says what it does.

At the first tick it runs ten checks of the hold's rules and shows how many
passed (`C10` is all). PLAYMODE then steps the hold: none (the setting), no
cut, the most cut, none again. A bottom bar on the main screen (hidden while a menu or window is up, so AUDIO ROUTING's rows stay readable) shows the step in force, whether
the mod holds it, the checks passed and the setting's step at boot. With
audio on USB, each PLAYMODE press changes its level in MAIN by the steps'
6 dB apart; AUDIO ROUTING's USB TO MAIN [dB] is never written.

It is a test, not for a project you keep.

```bash
python -m elekloader.sdk.build examples/usb-gain-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
```
