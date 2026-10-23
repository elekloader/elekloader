# hooks-check (Digitone II)

A test for [core-dn2](../../mods/core-dn2): it subscribes to the core's six UI
events and shows what it counts. The unit behaves exactly as stock
apart from the drawing until its PERSONALIZE row is selected; from then on
it takes [TEMPO]. **The five bars erase parts of the stock interface on every
screen.** This is intentional diagnostic drawing and can look like tearing;
omit this mod for normal use.

| what | where on screen | moves when |
|---|---|---|
| `ev_tick` | 3x3 square, top right | blinks about once a second, all the time |
| `ev_key` (order 60) | bar at y 60-61 (the top) | any key that reaches it: press, repeat and release each add one |
| `ev_enc` | bar at y 56-57 | any encoder turn |
| `ev_settings` | the HOOK TEST row in SETTINGS, and the bar at y 52-53 | the row is listed after SYSTEM; selecting it adds one |
| `ev_personalize` | the HOOK TEST row in PERSONALIZE, and the bar at y 48-49 | the row is listed after NOTE EDIT MENU; selecting it adds one and arms the take |
| `ev_key` (order 40) | bar at y 44-45 | once armed, each [TEMPO] event it takes |
| the order | a second square, below the first | appears only if the order-60 handler ever saw an event the order-40 one had not |

Each bar is its count mod 128. The tick handler also asks for a redraw every
8 ticks, so the bars move with nothing else on screen changing.

It uses the Analog Rytm MKII's [label.s](../hooks-check-ar2/label.s) (the row's name, with
the Digitone II's `std::string` constructor) and its own `hooks.c`. Its
counters are globals, so an emulator can read them from the link map.

```bash
python -m elekloader.sdk.build mods/core-dn2 --stock Digitone_II_OS1.12.syx
python -m elekloader.sdk.build examples/hooks-check-dn2 --stock Digitone_II_OS1.12.syx
python -m elekloader.patch --stock Digitone_II_OS1.12.syx \
    --mod mods/core-dn2/out/core-0.1.elemod --mod examples/hooks-check-dn2/out/hooks-check-0.1.elemod \
    --out dn2-hooks.syx --version HC01
```

Checked in digikit's emulator (`panel_drive`, 23 Oct 2026), reading the
counters back after each step:
- a TRIG 1 tap counted two key events (press and release, code 25), and two
  encoder turns two encoder events; the tick count climbed throughout;
- SETTINGS listed HOOK TEST once after SYSTEM; selecting it counted 1;
  after leaving and reopening SETTINGS it was listed once again, and
  selecting it counted 2. PERSONALIZE did the same after NOTE EDIT MENU;
- before arming, [TEMPO] opened the TEMPO page; after arming, the main
  screen stayed up, the order-40 handler counted 2 taken events (press and
  release), and the order-60 handler's count did not change;
- the order flag stayed clear over 170 key events.

On 23 Oct 2026 the same six-event checks passed on a physical Digitone II
with the DS03 showcase (the core + DigiHealth 1.3 + hooks-check 0.1). Fresh
screen captures and independent counters verified both reopened menu
callbacks, subscriber order and consumed/pass-through TEMPO. Read-only
runtime comparison matched the exact installed build. This is functional
control evidence; it does not qualify audio or longer project use.
