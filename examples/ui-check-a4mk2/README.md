# ui-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2) 0.7's UI and system events:
a std1 mod (`"events": "std1"`) that subscribes to `ev_boot_done`,
`ev_idle`, `ev_crash`, `ev_usb`, `ev_key_leds`, `ev_mode`,
`ev_param_set`, `ev_clip`, `ev_pattern`, `ev_mute` and `ev_view`, and
contributes to core's own `ev_knob_turn` and `ev_analog_frame` tables
(`analog-four-mk2/ui.h`). What it records, in its exported `ui_check`, is
in the comment at the top of `ui.c`; LED 0x20 is read back through the LED
slot table and its palette, since the 2 bits a LED holds are a colour index
and a lit LED can read 0. It takes nothing, except while armed (UI TEST
selected in SETTINGS; select it again to disarm): then it takes encoder B's
and the LEVEL encoder's turns, every mute and every queued pattern change,
and lights LED 0x20 with `core_led_set`.

A second row, UI CRASH, raises an illegal instruction in the UI task while
armed (nothing otherwise), so `ev_crash` runs on a real fault: `ui_check`
then holds the exception frame's first long and PC (`crash_head`,
`crash_pc`) and `crash_magic` "CRSH". From 0.4 it also shows them, since
after the fault nothing answers USB and a power cycle loses RAM: rows 0-3 of
the screen read `UC CRASH Nnn Fff` (crash, faults), `M` crash_magic, `H`
crash_head and `P` crash_pc, in hex, written to the front panel's UART the
way stock's crash printer writes (no calls into stock). It holds them about
10 s, timed by DMA timer 0's free-running count with interrupts still masked,
then returns: stock's crash screen follows and the unit needs a power cycle.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/ui-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/ui-check-a4mk2/out/ui-check-0.4.elemod \
    --out a4-ui.syx --version UI56
```

In analog-emu a build whose mods handle `ev_idle` answers no USB MIDI: the
emulator feeds USB MIDI only while main sits on one of the image's own
self-branches, and with a handler main loops in core instead. Check
`ev_idle` on the unit, and in the emulator build ui-check without its
`ev_idle` subscription.
On the unit a build with an `ev_idle` handler answers USB as usual, and
analoghealth's CPU figure reads 100 %: core's idle loop takes the place of
stock's own spin, the time that figure counts as idle.
