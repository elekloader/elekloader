# Your mod in the shop

The shop is the web page's library of mods. It lists your files; your code,
your releases and your repository stay yours. This page is everything you
need to get a mod in, keep it up to date, and take it out. How the shop's
list and its checks work inside is in [WEB.md](WEB.md#the-mod-shop-the-librarys-cards).

## What you need

- **Your mod's `.elemod` files**, one for each OS version it supports
  ([ADAPTING.md](ADAPTING.md) builds them).
- **A GitHub repository** for them, in a release (or a pre-release for a
  beta), or committed to the repository.
- **A licence that lets the shop pass the files on**: MIT, GPL, LGPL, BSD,
  Apache-2.0, MPL-2.0, ISC, 0BSD, CC0 or the Unlicense. Name it in your
  `mod.json` (`"license"`), or in the form.

Nothing else: no files in elekloader's repository, no evidence files, no
screenshots, no account but GitHub's.

## Put it in

One command builds your mod for each OS, makes the release and opens the
form filled in (it needs GitHub's command line tool, `gh`):

```bash
python -m elekloader.sdk.publish my-mod --stock Digitakt_OS1.53.syx --stock Digitakt_OS1.54.syx \
    --summary "One sentence for the shop's card." --tested "What ran on a unit, and what not yet."
```

Your stock OS files stay on your machine; only the built `.elemod` files
are published. `--dry-run` shows what it would do.

Or by hand: publish the release yourself, then fill in the
[Submit a mod](https://github.com/elekloader/elekloader/issues/new?template=submit-mod.yml)
form (the page's **Submit a mod** button opens it too): the repository, the
release tag (or a commit), one sentence for the card, and what you have
tested on a unit.

A check then reads your files and comments on the issue:

- **What goes in:** each file's mod, device, OS and licence, checked
  against the sha256 GitHub lists.
- **What it needs:** a core for its device and OS, as new as the one your
  file names, and any mod it requires (machine-pages, fxkit, machines…),
  either the shop's or one from your release, which comes along.
- **Good to know:** the core it builds with, the RAM it takes, and the shop
  mods it can't share a build with. That's not a refusal: the page greys
  those cards out beside yours.
- **What it refuses, and why**, so you know what to fix. Edit the issue,
  and the check runs again.

When files pass, the check opens the pull request that adds them, and a
maintainer merges it. The next deploy puts your mod on the page.

## Keep it up to date

Publish a new release. That's all: once a day the shop finds newer
releases of the mods it lists and takes their newer files, the same
checks as above. When every change is a newer file of a mod your
repository already has in the shop, it goes in by itself. Anything new (a
new mod, a mod that another repository's release also carries) still
comes through the form.

Your card keeps its text. The note on hardware is marked with the version
it was about ("As of 1.2: …") until you give a new one. To change either,
put these lines in the release's notes (`publish --summary/--tested`
writes them):

```
Shop summary: One sentence the card shows.
Tested on a unit: What ran on hardware, and what not yet.
```

A pre-release replaces a file that came from a pre-release. Over a full
release, send it through the form. A commit you pinned stays pinned:
send a newer commit through the form.

## Take it out

The [Remove a mod](https://github.com/elekloader/elekloader/issues/new?template=remove-mod.yml)
form takes your files out of the shop (all of them, or the ones you name).
When you own the repository and no other shop mod needs your files, it
goes through by itself. Otherwise a maintainer looks first, and the
check's comment says which mods need what you're taking out.

## When it waits for a maintainer

Most things go through on their own. These wait for someone to look:

- a mod new to the shop;
- a file of a mod that the shop already takes from another repository
  (a release that bundles someone else's mod);
- an issue opened by someone other than the repository's owner;
- a mod the shop holds (`"hold"` in its entry), because other mods
  build on it;
- a removal that other shop mods depend on.

Bugs in a mod go to its own repository: the shop lists files, and the
mod's author knows its code.
