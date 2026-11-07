# A thumbnail for your shop card

Your mod's card in the [shop](SHOP.md) can show a picture of the device's
screen instead of its drawn cover. It's optional, and it lives in your own
release: elekloader's repository only records its sha256.

- [The rules](#the-rules)
- [Making one](#making-one)
- [Check it](#check-it)
- [Add it to the shop](#add-it-to-the-shop)
- [What a maintainer sees](#what-a-maintainer-sees)
- [Changing or removing it](#changing-or-removing-it)

## The rules

A thumbnail is a file named **`thumb.png`**, and it has to be:

- **exactly 128×64 pixels**, the size of the screen;
- **exactly two solid colours**: no greys, no smoothing, no transparency;
- **drawn, not dithered**: lines, shapes and pixel text. Shading or a photo
  turned into a pattern of dots is refused.

Nothing is converted for you: a file that breaks a rule is refused, with
the reason. The rules keep every picture in the shop to what the screen
itself can show, which keeps the shop safe to browse and quick to review.

The shop draws the lighter of your two colours lit, in the card's colour, on
black, so amber on navy and white on black come out the same.

## Making one

**From the screen.** A screenshot of your mod's page is the best thumbnail.
Take one from an emulator window or a capture of the device, and scale it to
128×64 with **nearest neighbour** (no smoothing). An emulator usually draws
the screen at 2× or 4×, so it divides exactly. With Python and Pillow:

```python
from PIL import Image
im = Image.open('screen-4x.png').convert('L').resize((128, 64), Image.NEAREST)
im.point(lambda v: 255 if v > 127 else 0).convert('1').save('thumb.png')
```

This keeps a screenshot's two colours. On a photo it makes blobs, which pass
the check, but a maintainer will turn them down.

**By hand.** In any pixel editor (Aseprite, GIMP, Photoshop, Paint):

1. Make a new 128×64 image, and limit it to two colours (an indexed palette
   of two, or a 1-bit image).
2. Draw with a pencil tool. Switch anti-aliasing off for lines, shapes and
   text, and use a pixel font, or the stock screen's.
3. Export as PNG, without interlacing.

## Check it

From an elekloader checkout:

```bash
python packaging/thumbs.py thumb.png         # "taken", or why not
python packaging/thumbs.py thumb.png --md    # what a maintainer sees
```

| It says | Do this |
|---|---|
| it is WxH; a thumbnail is exactly 128x64 | resize it with nearest neighbour, or crop it to the screen |
| it has N colours; a thumbnail has exactly two | switch anti-aliasing off and redraw the soft edges, or save with a two-colour palette |
| it has transparent pixels | fill the background with your second colour |
| it is one flat colour | draw something on it |
| it looks dithered | draw it with lines, shapes and text instead of shading |
| it is an interlaced PNG, or a 16-bit PNG | export it again: not interlaced, 1-bit or 8-bit |
| it is N bytes; a thumbnail is at most 16384 | export as 1-bit or indexed (a thumbnail is usually under 1 KB) |

## Add it to the shop

Attach `thumb.png` to the release, beside your `.elemod` files: drag it in
on the release page, or

```bash
gh release upload v1.0 thumb.png
```

For a mod pinned to a commit, keep one `thumb.png` anywhere in the
repository at that commit.

Then [submit the release](SHOP.md#2-open-the-form), as for a new mod. When
the shop already has your files, submit the release it has: only the
picture changes. The bot's comment says whether it was taken.

A picture that fails never stops your mod: the mod goes in, and the card
keeps the picture it had, or its drawn cover.

## What a maintainer sees

A maintainer looks at every new picture before it goes in, so a submission
that brings one never merges by itself. In the pull request the bot shows
the picture as text, each pixel a braille dot, and never as an image:

```text
⡏⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⢹
⡇⠀⢰⣏⠑⠀⢸⡇⠀⠀⠈⢹⡏⠁⢠⡞⠉⠂⢸⡏⠉⠀⢸⡏⢹⡆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⢀⠉⢻⡆⢸⡇⠀⠀⠀⢸⡇⠀⠸⣇⠀⡀⢸⡏⠉⠀⢸⡏⢷⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⠀⠉⠉⠀⠈⠉⠉⠀⠈⠉⠉⠁⠀⠈⠉⠀⠈⠉⠉⠀⠈⠁⠈⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⢀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⢀⣀⠀⠀⠀⠀⢸
⡇⠀⡇⠀⠀⠀⠀⡠⠔⠊⠉⠉⠉⠒⠢⠤⡀⠀⡇⠀⠀⠀⠀⡠⠔⠉⠉⠉⠉⠒⠢⢤⠀⠀⡇⠀⠀⠀⢀⠤⠒⠉⠁⠉⠉⠒⠤⢤⠀⠀⡇⠀⠀⠀⢀⠤⠊⠁⠀⠉⠑⠒⠀⢸
⡇⠀⡇⠀⠀⢀⠜⠀⠀⠀⠀⠀⠀⠀⠀⠀⠱⡀⡇⠀⠀⡠⠊⠀⠀⠀⠀⠀⠀⠀⠀⠀⢣⠀⡇⠀⠀⡠⠃⠀⠀⠀⠀⠀⠀⠀⠀⠀⢇⠀⡇⠀⠀⡔⠁⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⡧⠤⠒⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⡧⠤⠊⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠣⣇⡠⠊⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⢆⣇⡠⠊⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⡇⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸
⣇⣀⣁⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣁⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣁⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣁⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣸
```

It's turned down if it's offensive, misleading (it doesn't show your mod),
or not yours to use. Then the maintainer takes it out of the pull request,
and your mod goes in without it.

## Changing or removing it

- **Updates keep it.** When your newer release carries the same
  `thumb.png`, the daily update takes it along. When it carries a new one,
  the update keeps the old picture and its report says so: send that
  release through the form to change it.
- **A thumbnail outlives its release.** It's pinned on its own, so the card
  keeps it after your mod moves on to newer releases. Delete the old release
  and the card goes back to its drawn cover until you send a new one.
- **To remove it,** ask in an issue, or take the mod out with the
  [Remove a mod form](SHOP.md#take-a-mod-out). Leaving `thumb.png` out of a
  new release doesn't remove it.
- **Anyone can report a picture** from the card's details ("Report it").
  A maintainer can take it off the site at once. How maintainers do that is
  in [WEB.md](WEB.md).
