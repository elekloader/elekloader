# The Splash screen maker

Your own picture or animation on the boot screen of a **Digitakt mk1** (OS
1.53, 1.54) or a **Digitone mk1 / Digitone Keys** (OS 1.43, 1.44), made into a
mod in the web page, with no toolchain. It is the page's **Splash screen
maker** view (`#splash`), linked from the side bar, from the Mod Shop's
**Make a splash screen** button, and from **Make your own mod!**.

## Using it

1. **Drop in a picture:** a PNG or an animated GIF (JPEG and WebP work too;
   only a GIF's frames animate). It stays in your browser tab: only the
   converted 128 × 64 black-and-white frames go to the page's build engine,
   which also runs in the tab.
2. **Adjust it** while the preview shows exactly the pixels your unit will
   show, played at the unit's rate:
   - **Fit**, **Fill** or **Stretch**, then **Zoom** and **Move**;
   - **Black and white:** **Sharp** (a threshold), **Dither**
     (Floyd–Steinberg), **Atkinson**, or **Pattern** (4 × 4 Bayer), with
     **Threshold**, **Brightness** and **Contrast**;
   - **Invert**, and whether a picture's transparent parts are lit;
   - for a GIF, **Speed** (¼× to 4×) and **Loop** (else it stays on its last
     frame).
3. **Make the mod:** give it a name, pick the device and OS (the stock file in
   use is the default), and **Add to my build**. The mod goes in Your files
   and, for the stock file in use, is ticked with the core it needs. Or
   **Download .elemod** for the desktop app or the command line.

Then build as usual. To go back, build without it, or send Elektron's own OS
file.

**Limits:** the intro runs about **3.3 seconds** (50 ticks of 15 a second),
so that much of an animation shows; at most **96 different frames** (identical
frames are stored once; past 96, frames are left out evenly and the page says
so); a frame is 1 KB of the unit's RAM. One splash mod per build: any two
patch the same intro calls, so the check refuses a second (DigiSplash's
included).

## How it works

`elekloader/splash.py` writes the `.elemod`; the page calls it through
`web/bridge.py` (`make_splash`). The mod is
[`examples/splash/splash.s`](../examples/splash/splash.s) assembled: its code
has no relocations and only five 32-bit fields differ between the four OS
versions (the two presents, the intro's PIT3 handler, the front buffer
pointer, and the semaphore post), so `splash.py` keeps its bytes and fills
them in. `tests/test_units.py` assembles the source for each OS and compares,
when `m68k-linux-gnu-as` is there.

- **Time.** The intro's PIT3 handler runs 15 times a second (66.67 ms) and
  posts the intro's semaphore. The mod sends its call to the post through
  `splash_tick`, which moves the sequence on one entry and then posts.
- **Pictures.** The intro presents several frames each tick (about 180 in
  all), through two stock calls, a delta present and a full present. Each of
  the intro's calls goes through `splash_diff` / `splash_flush`: while the
  PIT3 vector is still the intro's, the current entry's frame is copied over
  the composed frame, then the stock present runs. Once the display module
  takes the vector, nothing is drawn.
- **Data,** after the code: the sequence's length, the position, where the
  frames start, the loop flag, a 16-bit frame number per tick, then the
  frames in the panel's format (byte = page + 8 × column, page = 7 − y / 8,
  bit y % 8).

| | DT 1.53 | DT 1.54 | DN 1.43 | DN 1.44 |
|---|---|---|---|---|
| delta present | 0x400e60e2 | 0x400e630a | 0x400f8efa | 0x400f916e |
| full present | 0x400e6064 | 0x400e628c | 0x400f8e7c | 0x400f90f0 |
| intro PIT3 handler | 0x4006c154 | 0x4006c154 | 0x40090cdc | 0x40090cfc |
| front buffer pointer | 0x4020d8f8 | 0x4020dca8 | 0x40241a44 | 0x40241e44 |
| intro calls to the presents | 7 | 7 | 9 | 9 |

The present sites and the PIT3 gate come from
[DigiSplash](https://github.com/DigiAlchemydsp/DigiSplash) (GPL-2.0-or-later),
which found them for 1.53 and 1.43 and tested its mods on both units. The 1.54
and 1.44 addresses were found by their bytes; every intro call to a present
was checked to be the same in each. The tick site is the handler's
`jsr` to the post, at handler + 0x1c, the same instruction in all four.
DigiSplash's own generator advances a frame per present, about 3.5 a tick:
this one keeps to the timer.

## What was checked

- **In digiemu**, for each of the four OS versions: core 4.1 (core-dn1 4.1.1)
  and a 4-frame test splash, resumed from a snapshot taken before the intro,
  with PIT3 delivered. All 180 of the intro's presents showed the frame its
  tick names, the picture changed on every second tick as its sequence said,
  and nothing was drawn after the intro handed over (49 ticks).
- **In the page:** the GIF decoder (`web/gif.js`) against Pillow's frames
  for 18 GIFs, pixel for pixel; a splash made, added, checked and built with
  a stock Digitakt 1.54; the mod's frame read back as the preview shows it.
- **Not yet on a unit.**
