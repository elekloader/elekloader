# elekloader in the browser

elekloader's patcher as a static web page, in two builds of the same `web/`
(`build_web.py --page`):

- **The Mod Shop**, <https://elekloader.github.io/shop/>: the whole page below, built by
  [elekloader/shop](https://github.com/elekloader/shop) with its list.
- **The builder**, <https://elekloader.github.io/elekloader/> (`--page builder`,
  this repository's `pages.yml`): the build, Make your own mod, Contribute and
  the +Drive mods view, for your own `.elemod` files. The library, the sections, the Splash screen
  maker and Submit are the shop's: the builder hides them, says where the shop
  is (the side bar, the top bar, a banner over the build, the phone's tab bar),
  and sends their addresses (`#library`, `#s/...`, `#splash`, `#submit`) to the
  same place on the shop's site. A missing requirement links to the shop
  instead of adding it. It still carries the shop's files and
  `shop/index.json`, for older copies of `publish` and other sites that read
  them there.

In the shop you pick mods from its library or add your own `.elemod` files,
drop in your stock OS file, tick mods, and download the patched `.syx` (and,
for the Octatrack, the `.bin`), with the build manifest. The build runs in
the page. Nothing is uploaded, and the site hosts no firmware.

The page is an app with two views:
- **Mods** (`#library`), where the page opens. Until a stock file is in, it
  starts with **Get started**: what the page does, and three steps.
  1. **Pick your device**: a tile per device, with its OS versions and how
     many mods it has. It also filters the cards.
  2. **Add its stock OS file**: a link to the device's page on Elektron's
     site, for its newest OS, and a drop zone. A file that is not a stock OS
     gets a message naming the files the page knows.
  3. **Add mods, then build**, with a link to how to flash and how to go
     back.

  Once a stock file is in, the head shows the device and OS, and a strip of
  what is done and what is next: **✓ Stock OS**, then **+ Add** a mod (or
  **✓ n mods in your build**), then **Build your firmware**. Below are the
  mods, every mod as a card on a shelf per kind (Sampling, Performance, ...;
  your own files last). The sidebar's Categories, the device chips and the search
  filter them, and Sort orders them.
- **Build** (`#build`): your stock OS file, your mods (the list, the profile,
  the details), the check, and Build firmware, then the downloads. Under the
  list are the two ways to add a mod: **Add from the library**, and **Add
  your own `.elemod`**, a drop zone that also opens a file chooser. (A
  `.elemod` dropped anywhere on the page is added too.)

Around them:
- **The sidebar**: the two views, the categories of mod with their counts,
  and your profiles.
- **Profiles**: a profile is one stock OS (its device, OS version and file)
  and the mods you tick for it. Each shows its stock OS under its name and
  how many mods it has ticked. Picking one switches both: its stock file
  comes back from this visit or from this browser (if you keep files here),
  or the page asks for it by name. **+** makes a new profile: a name, then
  the stock OS that is in now (empty, or with the mods ticked now) or
  another file you drop in next. Your first stock file makes the first
  profile, "Default". A stock file for another OS goes to the profile that
  has it, or makes a new one: each profile keeps its own. The Build view's
  Profile menu switches too, and Delete deletes the profile in use (its mods
  stay added).
- **Its foot** shows the build engine, and opens About (licences, how to
  recover).
- **On a phone** the page is one column (the profiles at its end), and a tab
  bar switches between Mods, Build and About.

## How it works

```
index.html + app.js  (the page: the library, your mods, the check, the build, the downloads)
      |  postMessage: your files' bytes, ticks; results back
worker.js            (a module worker, so the page never blocks)
      |
Pyodide              (CPython 3.14 compiled to WebAssembly, served by this site)
      |
bridge.py            (a thin layer over elekloader, on Pyodide's in-memory file system)
      |
elekloader           (the package, unchanged: elekloader.zip)
```

The page runs elekloader's own code, and the logic is the desktop window's:
- the stock file goes to `gui.LoaderModel.set_stock`: it is recognised by its
  hash, or refused with the same words;
- the mod list, the details and each mod's status come from
  `LoaderModel.describe`;
- the live check, as you tick, is `LoaderModel.check`, which runs the
  linker's checks on the stock image it has already loaded;
- ticking a mod ticks what it requires (`gui.with_requirements`);
- the build is `patch.build` then `patch.save`, the same calls as the
  command line and the window. They verify every output before anything is
  offered for download.

Your files are written under `/work` in Pyodide's in-memory file system. It
disappears with the tab. The downloads are made in the page from the bytes
the worker returns (`blob:` URLs).

The page shows the device's recovery text before it lets you download.
Flash the file yourself, as with any OS update ([BUILDING.md](BUILDING.md#flash-it)).
The build never talks to a device. Only [the +Drive mods view](#the-drive-mods-view)
does, over Web MIDI, and it never flashes firmware.

## The mod shop: the library's cards

The library shows the shop's curated mods, and the `.elemod` files you added
yourself (the "Your files" shelf). Once you have dropped in a stock file, it
opens on that file's device. A device you pick before that says which stock
OS file it needs.

**Sections.** A repository with many small mods of one kind can have a page
of its own instead of crowding the shelves: the shop repository's `sections.json` lists each
section (`id`, `title`, `nav`, `blurb`, optional `tools`: pages of the site such
as `#splash`, listed under it) and its
packs, one repository each. Its mods leave the Mod Shop's shelves; the side
bar links the section's page (`#s/<id>`, a shelf per pack, the device chips
and the search work there), and the Mod Shop shows one row that opens it
(with a search, how many match there). A repository's later releases land in
its section by themselves. `build_web.py` checks the file and publishes it as
`shop/sections.json`. The first: **Splash & screens**, DigiSplash and
DigiCandy, with the Splash screen maker as its tool: while either page is
open, the side bar lists Mods and Splash screen maker under the section, and
both pages show the same row of tabs.

**Picture packs: a few examples, not the whole set.** Anyone can make a
splash in the maker, so the shop lists five of DigiSplash's (`rare-splash`,
`aba-bootanim`, `digitrash`, `match`, `vinyl`) and five of DigiCandy's
on-command overlays (`digitrash`, `planet1`, `reach`, `loox`, `tidemoon`),
each for both devices, and links the rest on the authors' repositories. The
daily updates only take newer files of mods the shop already lists, so the
others stay out. A new picture pack goes in the same way: a few files, named
under **Only these files** ([SHOP.md](SHOP.md)). Their `"thumb": null` turns
off each repository's one `thumb.png`, which shows none of them; a
`thumb-<mod id>.png` per picture would bring pictures back.

Each card's cover is the mod's name over line art, in its kind's colour.
- **Keywords pick the art.** `MOTIFS` in `app.js` matches the mod's id and title,
  and the first match wins:
  - a tuner draws a needle; metallic percussion, a struck metal's spectrum;
    a synth, an FM wave; a quantizer, a line snapped to steps; pitch tables,
    their steps around a zero line, with a strum's ADD steps hollow; REPITCH,
    a record;
  - a jump, steps and an arc; a recorder, a loop; scenes, a crossfader; CCs,
    knobs;
  - USB audio, one meter per channel (and the inputs, for USB IO); USB, the
    trident; MIDI, a DIN socket.
- **The motif also nudges the hue**, so one shelf still varies.
- **No keyword:** the art is drawn for the mod's kind (a sliced waveform for
  Sampling, a level under a ceiling for Performance).
- **Variation:** each drawing is varied by the mod's id, so two mods with one
  motif differ. Below it are the mod's title, version, device and OS
versions, the summary from the catalog, and its patch sites. The cover and
**Details** open a sheet with its author, kind and licence, what it changes,
and the files with their sha256.

- **+ Add** puts the mod in your mods and ticks it with what it requires.
  Of a required mod the shop lists in several versions (machine pages), every
  version comes, and the build takes the newest that links with its mods
  ([CORES.md](CORES.md), "How a build picks one").
  Before you have given the stock file, it goes in your mods, and the page
  ticks it when the stock file it is for comes; so with a file of your own.
- **✓ In build** marks a mod in your build; a click takes it out of the
  build, and it stays in your mods.
- **Remove** takes it out of your mods.
- **Clashes** greys out a card whose file can never go in one build with
  what is ticked. The note under the summary, and the same note in the
  details sheet, say which ticked mod it clashes with and why: both patch
  the same place, one names the other as incompatible, both claim the same
  name, symbol, table or memory, or one is a whole build. A mod it would
  bring along counts too: DT-FM needs machine pages, which clashes with
  DigiSlicer. These are the link check's rules that need no stock file.
  Each shop file's `footprint` in `shop/index.json` (`build_web.footprint`)
  carries what they need, and your own files have it from the engine. The
  check as you tick still has the last word.

A mod built for several OS versions of a device (its files for 1.53 and
1.54, say) is one card, which lists the OS versions it has. With a stock
file dropped in, the card adds the file for that OS; before one, it adds
every OS's file, and the list shows the one that fits once you drop in your
stock file.

- **The list** is `catalog.json` in [elekloader/shop](https://github.com/elekloader/shop), changed by pull
  requests there (the forms' and the daily updates', or a maintainer's). Each
  item names a file of a GitHub release (`repo`, `tag`, `file`), or a file
  in a repository at a commit (`repo`, `commit`, `path`), its `sha256`, its
  `device`, and optionally:
  - `needs_core`: the oldest core version it links with;
  - a `summary`;
  - `on_unit`: what has been checked on real hardware. The details sheet shows
    it under "On a unit"; the card does not.
  - a `license`, when the file names none. `"kind": "core"` marks a core the listed mods need: it joins the
  site's cores.
  - `thumb`: the card's picture ([THUMBNAILS.md](THUMBNAILS.md)), pinned on its own:
    `{"tag", "file", "sha256"}` from a release or `{"commit", "path", "sha256"}`, from the item's `repo`.
    The workflow downloads it, `build_web.py` checks it again (`packaging/thumbs.py`) and serves the
    canonical 1-bit PNG; one missing or failing is left out with a warning, and the card keeps its drawn
    cover. Deleting `thumb` takes a picture off the site.
- **The files** are not committed. The pages workflow downloads each from
  its author's release or commit, and `build_web.py` puts it on the site only if it
  is the file the catalog pins (by sha256), made for the device the catalog
  says, and under a licence that allows passing it on (`SHOP_LICENCES`). A
  file that cannot be downloaded (a draft release) is listed as not
  released yet.
- **In the page,** a mod from the shop comes from this site like everything
  else, and the worker checks it against the catalog's sha256 again before
  adding it. The page ticks the core the build takes, the one it lists
  (docs/CORES.md, "How a build picks one"): the newest of the device and OS,
  or an older one of the site's while a ticked mod cannot link with the
  newest (Tone+FX 4.0c on the Digitakt mk1 patches a place core 4.1
  patches, so it builds with core 4.0). If a mod needs a newer
  core than one you ticked yourself (`needs_core`, or the file's own
  `resources.core`), the newest is ticked in its place. So is the newest
  when the ticked core's RAM does not hold the mods
  (core-dn1 4.0's 1.25 MB on the Digitone, core 4.0's 256 KB on the
  Digitakt mk1, in place of 128 KB). The page changes the core without a
  word: it speaks of a core only when one stops a build. A card is greyed
  out against the core when no core of the site's takes the build with it
  (a mod in the build needs a newer core than the card's mod links with, or
  the other way round), and the card says which; the check says the rest.

To add a mod to the shop, publish its `.elemod` in a GitHub release and
submit it with the **Submit a mod** form ([Submitting a mod](#submitting-a-mod)):
a check opens the pull request for you. By hand, add an
item to the shop repository's `catalog.json` with the asset's sha256 (the release page shows
it, or `gh release view --json assets`), and merge: the next deploy takes
it. A mod whose author keeps its `.elemod` files in their repository
instead (as digi1_mods does) is pinned to a commit: the whole commit id,
the file's path, and its sha256. A shop mod that requires another shop mod
(Digi Mono needs digichain) brings it when it is added. A mod you added yourself that needs a shop mod (a Digitone
machine needs machines) shows "Needs machines" with a + beside it, which adds the shop's for your OS and ticks it. A mod with a file for each OS version gets an item per file; the page
shows them as one card (the same repository, mod, version and device).

### Where the list is

The list (`catalog.json`), its sections (`sections.json`), the shop's forms
and its two bots live in [elekloader/shop](https://github.com/elekloader/shop), so submissions and the
daily updates don't commit here. Its workflows check out elekloader's `main`
into `el/` and run the scripts from there, and its `check` workflow runs
elekloader's unit tests on the list for every pull request.

`pages.yml`, `kit-build.yml` and `tests.yml` clone the shop repository's
`main` into `build/shop-list`: elekloader/shop, or the repository the
variable **`SHOP_REPO`** names (`owner/name`, public, with `catalog.json` and
`sections.json` at its root). The site's `build.json` names it
(`shop_repository`, `shop_commit`), and the page's Submit, Remove and Report
a picture links open its forms. A kit's catalog takes the list's commit as
its `revision`. The leaderboard counts the claims on both repositories'
merged pull requests.

A change to the list reaches the site within minutes: the shop repository
starts this repository's pages workflow with its secret **`SITE_DISPATCH`**, a
fine-grained token that may only start workflows here (resource owner
elekloader, repository elekloader/elekloader, Actions: read and write).
- The bots' own merges (a submission, a daily update) start it themselves.
- Any other change to `catalog.json` or `sections.json` on the shop's `main`
  (a maintainer's merge, a push) runs its `site.yml`, which starts it. A push
  made with a workflow's token starts no workflow, so a bot's merge redeploys
  once. Run it by hand from elekloader/shop's Actions > Redeploy the site.
- **The token expires.** Then the shop's runs warn or fail at that step and
  the site takes the change only at its next build (a push to main here, or
  pages run by hand). Make a new one and store it with
  `gh secret set SITE_DISPATCH -R elekloader/shop`.

On your machine the scripts read the list from `build/shop-list/`, or from
**`ELEKLOADER_CATALOG`** (the path of `catalog.json`; `sections.json` is read
beside it): `build_web.py`, `submit_mod.py`, `shop_updates.py` and the unit
tests, whose checks of the real list say they skipped without it.
**`ELEKLOADER_SHOP_REPO`** is the repository `python -m elekloader.sdk.publish`
opens the form in (elekloader/shop by default).

```sh
git clone https://github.com/elekloader/shop build/shop-list
python tests/test_units.py
python packaging/build_web.py --catalog build/shop-list/catalog.json ...
```

### Submitting a mod

Authors don't need to edit the list themselves; [SHOP.md](SHOP.md) is their page. The shop's
**Submit a mod** button opens the [issue form](https://github.com/elekloader/shop/blob/main/.github/ISSUE_TEMPLATE/submit-mod.yml) in elekloader/shop: the
repository, the release tag or a commit, a one-sentence summary, what has been tested on a unit, the
licence if the files name none, and optionally which of the files to submit. A release or a
pre-release; a draft is refused until it is published. With a commit (its whole id), the files are
the `.elemod` files in the commit's tree, and the entries pin the commit and each file's path.
`python -m elekloader.sdk.publish` (in the package) builds a mod for each stock file, makes the
release with `gh`, and opens this form filled in from its address.

When the issue is opened (or edited), elekloader/shop's `.github/workflows/mod-submission.yml` runs
elekloader's `packaging/submit_mod.py` (from main) on it:
- **The check.** It downloads the release's (or the commit's) `.elemod` files and checks each
  against the sha256 GitHub lists. It reads each with elekloader (id, version, device, OS, licence,
  requirements) and refuses a licence that doesn't allow the site to pass the file on
  (`SHOP_LICENCES`). A file the shop lists already, byte for byte, is just that.
- **The comparison.** It compares the files with the shop's, downloaded as the pages workflow does:
  - **an update:** the same mod, device and OS from the same repository replaces the old entry, in
    its place (a repository that moved counts as the same; the entry takes its new name);
  - **a clash:** another repository's file for a mod the shop lists for that device, for any OS
    (the mod is that repository's on the device), is flagged and left out. A release sometimes
    bundles someone else's mod; the submitter can name the file in "Only these files" to add it
    anyway, and the review decides.
- **What each mod needs.** A mod goes in only if the site can build it:
  - **its core:** the site has a core for its device and OS (the latest release's, or the shop's)
    at least as new as the one the file names (`resources.core`), or as the core its release
    carries. That version becomes the entry's `needs_core`. Cores come from elekloader's own
    releases only: the page builds with the newest core for a device and OS that takes the mods,
    so a core from a mod's release would become every build's. A mod's release that carries one
    is fine; the core stays out;
  - **the mods it requires** (machine-pages, fxkit, machines, ...): the shop's for its device and
    OS, or else the release's own, which comes along even when "Only these files" leaves it out
    (with the summary its file gives). A mod whose requirement is in neither is refused.
- **The answer.** It comments on the issue with a table of what it found, and labels the issue
  `needs-changes` if nothing could be added. Editing the issue runs the check again. Under "Good to
  know" it says, for each file that goes in, the core it builds with (the newest of the site's it
  can share a build with: an older one, and why, for a file that patches a place the newest
  patches), its RAM, and the shop's mods for its device and OS it can't share a build with
  (`build_web.clash`, the page's rules); that is never a refusal. A note says when the issue isn't from the repository's owner.
- **The pull request.** When files pass, it writes them into `catalog.json` (in the list's own
  layout, which `tests/test_units.py` keeps exact) and opens a pull request from branch
  `mod-submission/<issue>`. The pull request closes the issue when it merges, and the next deploy
  puts the mods on the site. The workflow starts the shop's check workflow on it (its own token's
  pushes start none).
- **An author's own update merges itself.** When the repository's owner opened the issue and every
  change is a newer file of a mod that repository already has in the shop (no new mod, no clash,
  no core, no held entry), the unit tests run and the pull request merges, and pages redeploys.
  Everything else waits for a maintainer.

The **Remove a mod** form ([remove-mod.yml](https://github.com/elekloader/shop/blob/main/.github/ISSUE_TEMPLATE/remove-mod.yml)) goes through
the same workflow (`submit_mod.py --remove`): the repository's entries, all or the files it names,
leave the list (cores stay: a maintainer removes those), and the report names every other shop mod
for the same device and OS that requires a mod that goes. It merges by itself when the repository's
owner opened it, nothing else needs what goes, and no entry was held.

An update keeps the card's text: the summary, and the hardware note, marked "As of <the old
version>" so it doesn't claim a run of the new file. New text comes from the form, or from lines in
the release's notes:

```
Shop summary: One sentence the card shows.
Tested on a unit: What ran on hardware, and what not yet.
```

A file older than the one the shop lists is left out.

Nothing in a submitted file runs. The issue's text reaches the script as a file, never the shell.
The shop repository needs **Settings > Actions > General > Allow GitHub Actions to create and approve
pull requests** (in an organisation, allowed in the organisation's settings first).

The same check runs locally, on a copy of the list, with the shop's files and the latest release's
cores downloaded as pages.yml does:

```bash
python packaging/submit_mod.py --repo owner/name --tag v1.0 --summary "One sentence." \
    --catalog /tmp/catalog.json --files-dir build/shop --cores-dir build/core
```

### Updates, by themselves

Once a mod is in the shop, its author only publishes releases. Every day
elekloader/shop's `.github/workflows/shop-updates.yml` runs elekloader's `packaging/shop_updates.py`:

- **It finds the newer releases.** For each repository the shop takes mods from by a release tag,
  the releases GitHub published after the earliest one the shop takes (no drafts, and none whose
  `.elemod` files are all cores).
- **It checks them, oldest first,** with the submission's check in its update mode: only newer
  files of mods the repository already has in the shop go in, for any OS of the same mod (a new OS
  version's file joins its card). It leaves out new mods (they come through the form), clashes,
  cores, older files, held entries, and a pre-release over a file that came from a release.
- **It opens a pull request** from branch `shop-update/<owner>-<name>`, or refreshes the one open,
  and closes one that main has caught up with. When every change is such an update, the unit tests
  run and it merges, and pages redeploys; otherwise it waits for a maintainer, with the check
  workflow started on it.

**Authors can opt out.** Updates are on for every entry unless it says `"auto_update": false`.
The form's **Updates** box (ticked = opt out) or a `Shop auto-update: off` line in a release's
notes sets it; the box unticked or `Shop auto-update: on` clears it. Submitting the form for a
release the shop already has changes only the setting. A newer release whose notes say `off` is
not taken: it opts the repository out, and the daily check stops following it. An opted-out
entry is left alone by the daily check, and its releases come in through the form.

Three switches, for maintainers:

- **`"hold"`** on an entry in the shop's `catalog.json` (true, or a reason) keeps that mod's file where it
  is: the daily check leaves it, and an update through the form waits for a review (and stays
  held). For a mod others build on, such as machine-pages, when a new version needs a look first.
- **The repository variable `SHOP_THUMBS`** set to `false` leaves every thumbnail off the site at
  the next deploy (the pages workflow passes `--no-thumbs`); the catalog keeps them. "Report a
  picture" issues ([report-picture.yml](https://github.com/elekloader/shop/blob/main/.github/ISSUE_TEMPLATE/report-picture.yml), in elekloader/shop) come from a card's details.
- **The shop repository's variable `SHOP_AUTO_MERGE`** set to `false` (Settings > Secrets and variables >
  Actions > Variables) stops both workflows merging; their pull requests still open. The daily run
  runs on its schedule only while its variable `SHOP_LIVE` is `true`.

Run it by hand from elekloader/shop's Actions > Shop updates > Run workflow (one repository, if you name it), or
locally on a copy of the list:

```bash
python packaging/shop_updates.py --list --catalog /tmp/catalog.json
python packaging/shop_updates.py --apply owner/name --catalog /tmp/catalog.json \
    --files-dir build/shop --cores-dir build/core
```

## The +Drive mods view

The page's +Drive mods view (`#drive`, `drive.js`) manages the `/mods`
folder of a unit running a build with the Drive Loader (FORMAT.md, "+Drive
mods"), over Web MIDI with SysEx. The browser asks for MIDI the first time
the view opens, not before. It only puts mods on the unit: the build
itself (`/mods/active`) is chosen on the unit, on its MODS page (SETTINGS >
GLOBAL > MODS, LOAD TO BUILD), and the view never writes it. The view writes
nothing but files in `/mods`:
every request goes through `datarpc.Mods`, whose paths are inside that
folder. It runs `js/`'s engine as the site serves it (`engine/`), not
Pyodide, and `app.js` loads it only when the view first opens:
- **Connect** picks a MIDI port (one named like "Rytm" first) and reads
  `/mods/.build`, the build descriptor. The view shows the build (its
  modules, the +Drive area's size with its addresses in brackets, the
  largest file the loader takes) and each table's spare entries, folded
  until clicked. Reads ask for
  8 KB chunks; where a reply never arrives (Firefox's Web MIDI on Windows
  drops SysEx over about 1 KB), the read starts again with 768-byte chunks
  and the view keeps that size, so the first read there takes one timeout
  (5 s) longer.
- **Mods on +Drive** (the card for `/mods`) shows one folder at a time: its path on top (`/mods/fx/a`),
  each part of which opens that folder, then its folders (`/NAME`) and its
  `.elemod` files. A folder opens with a click. The view reads every folder when it connects, so an empty
  folder shows too, and each file is downloaded for its id and version
  (and for what it requires, when an upload needs it), with Download and Delete (one press deletes). A mod
  `/mods/active` names, the saved build, can't be deleted, as the unit's
  MOD IN USE: unload it there first. A folder row says how many mods are
  below it. In a
  folder 3 deep, a note says that nothing deeper shows: the route lists no
  folder there, and the loader can't reach one.
  The table spans the card: the Name column takes what the others leave.
- **Move and rename**: a file or folder row dragged onto a folder row, or
  onto a part of the path, moves there, and **Rename** on a row makes its
  name a text box (Enter renames, Escape keeps it). Either is one upload of
  `FROM\nTO` to `/mods/.mv` (`Mods.move`); the route moves a folder with what
  is in it and keeps `/mods/active`'s lines naming them, so a mod in the
  build can move too. The view refuses first a name already there, a folder
  into itself, a file without a `.elemod` name, and a result past 3 deep or
  127 bytes; while the MODS page hasn't caught up with the last move, the
  route answers `Busy: the MODS page`, which the view shows.
- **New folder** makes a folder in the open one (no `/`, not starting with
  `.`, at most 63 bytes, 3 deep at most) on the unit, empty, and opens it.
  The Data router has no request for a folder, so the view uploads nothing
  to `FOLDER/.dir`, which the loader's route takes as one (`Mods.mkdir`),
  and then reads the parent's listing to see it there. **Delete** on a folder row
  deletes an empty folder (one press, no second ask), as SAMPLES' DELETE DIR;
  a folder with anything in it can't be deleted, and the route refuses one
  with `Folder not empty`.
- **Uploads**, dropped or chosen, go into the open folder; files dropped on
  a folder row, or on a part of the path, go into that folder. They are checked
  first, together with the files in `/mods`, in any folder, that they
  require. If one would not load, nothing is sent.
- **The last boot** shows `/mods/.status`; its `stage:` line is the
  largest upload the room shows, and a file over it is refused before
  anything is sent.
- **The shop's mods** for the build's device and OS are each marked
  **+Drive** or **flash** (`drive.flashOnly`: sites, storage and the other
  things only a flashed build carries), and a +Drive one can be uploaded from
  there, into the open folder. A shop mod already in `/mods`, in any folder
  (found by its file name), reads Upload again and replaces that copy where
  it is.

`tests/test_drive_view.mjs` runs the view in Chromium (Playwright) against a
mocked MIDI port: a fake unit with the made-up build of
`js/test/drive-vectors.json`, whose uploads must be the requests
`elekloader mods` sent, byte for byte. The fake unit keeps folders as the
route does, and the test makes one, uploads into it, keeps a mod of the
saved build from being deleted, checks the view never writes `/mods/active`, goes 3 deep, drops onto folder rows and the path, finds a shop mod's
copy in a folder, deletes an empty folder, and moves and renames files and
folders (its `.mv` moves them, and `/mods/active`'s lines, as the route does).

## What stays private, and how

- **Nothing is sent anywhere.** No request ever carries your files. The
  page fetches only its own files (the page, the worker, Pyodide, the
  elekloader package, the engine, the cores, the shop's list and its mods),
  all from this site. The +Drive mods view sends the files you choose to
  the unit you connect over USB MIDI, and nowhere else. There are no
  analytics, no fonts or scripts from elsewhere, and no server side.
- **The page** carries a Content Security Policy in a `<meta>` tag:
  `default-src 'self'`, scripts from this site only, plus
  `'wasm-unsafe-eval'` (WebAssembly). GitHub Pages cannot send CSP headers,
  so the tag is all there is.
- **The worker** is not covered by a `<meta>` CSP, and that is by the
  standard: a worker takes its policy from its own response's headers, and
  Pages sends none. So `worker.js` keeps itself to the site. Before
  anything loads, it replaces `fetch` and `XMLHttpRequest.open` with
  versions that refuse any URL off this site, and it removes `WebSocket`,
  `EventSource`, `WebTransport` and `RTCPeerConnection`. Pyodide is loaded
  with `indexURL` and `packageBaseUrl` both set to `./pyodide/`, so it has no
  CDN to fall back on. Nothing asks it for a package, and only Pyodide's core
  files are on the site.
- **Kept in this browser:** your profiles (each one's name, stock OS by
  sha256, and mod file names), the last version field per device, and your
  display choices are kept in `localStorage`. Only if you tick "Keep my stock
  file and mods in this browser" are the stock files (one per profile's OS)
  and the mods you added kept, in IndexedDB. "Forget them" deletes them.
  Neither leaves the browser. Profiles from before (sets of mods per device,
  with no stock OS) become profiles that take the next stock file for their
  device.

## What is on the site

`packaging/build_web.py` assembles it, about 14 MB in 20 or so files:

| path | what |
|---|---|
| `index.html`, `style.css`, `app.js`, `worker.js`, `bridge.py`, `drive.js` | `web/`, as committed, but for one change: index.html names `app.js?v=` and `style.css?v=` with the site's version (a hash of every other file), and the page and its worker pass that `?v=` on to every file they load, so a browser's cache never mixes two deploys |
| `engine/<hash>/` | the TypeScript engine the [+Drive mods view](#the-drive-mods-view) loads (`js/tools/build.ts`'s output, in a folder named by its files' hash, which index.html names: a deploy never mixes two engines) |
| `devices.json` | the devices the engine knows (key, name, OS versions), so the page lists them all before the engine has loaded |
| `elekloader.zip` | the elekloader package, exactly as the commit has it (`git archive HEAD elekloader`), stored, sorted, fixed dates: its sha256 follows from the commit |
| `core/*.elemod`, `core/index.json` | the cores of the latest release, and the cores the catalog lists, with their sha256; the worker loads and checks each one, and the page lists the one a build takes |
| `shop/*.elemod`, `shop/index.json` | the shop: the catalog's mods, from their authors' releases, with what each file says about itself |
| `shop/thumbs/<sha256>.png` | the cards' thumbnails: each the canonical 1-bit PNG of the picture the catalog pins (`packaging/thumbs.py`), named by the sha256 of the author's file |
| `shop/claims.json` | the leaderboard: the to-do list's entries claimed by the repository's merged pull requests (`Claims:` lines; [CONTRIBUTE.md](CONTRIBUTE.md#claiming-an-entry)), from `gh pr list --state merged` |
| `shop/bounties.json` | the community's to-do list for the core, from the shop's files and the cores (`elekloader.sdk.bounties`, grouped by distance: the site has no stock file), and from the upstream lists in `web/upstream/` (`--upstream`: the stock code octabam's newer modules hook), shown in the page's Contribute view ([CONTRIBUTE.md](CONTRIBUTE.md)) |
| `pyodide/` | five files from Pyodide's core tarball (`pyodide.mjs`, `pyodide.asm.mjs`, `pyodide.asm.wasm`, `python_stdlib.zip`, `pyodide-lock.json`), and `NOTICE.txt` with their licences |
| `build.json` | elekloader's repository (`$ELEKLOADER_REPOSITORY`, else the workflow's: its commit links) and the shop's (`shop_repository`: its forms), the commit, the site's version, the package's sha256 and git tree, the release the cores come from and whether the package is that release's, Pyodide's version, every file's sha256 |
| `LICENSE.txt`, `NOTICE.txt` | elekloader's |

The Pyodide release is pinned by version and sha256 in `build_web.py`
(`PYODIDE_VERSION`, `PYODIDE_SHA256`). To move to a newer one, change both,
then build and run the checks below.

## The workflow

`.github/workflows/pages.yml` runs on every push to main, and by hand. It:
1. runs `tests/test_units.py`;
2. takes the `core-*.elemod` files of the latest release
   (`gh release download`), and fetches that release's tag;
3. downloads the shop's files, each from its author's release or commit
   (one it cannot get is a warning, and the shop lists it as not available);
4. lists the repository's merged pull requests (`gh pr list --state
   merged`) for the leaderboard; without them the board is empty;
5. downloads the pinned Pyodide core tarball;
6. assembles the site (`build_web.py` checks the tarball's sha256, and
   the shop's files against the catalog);
7. runs `tests/test_web.mjs` on the assembled site: the unit tests under
   Pyodide with the site's own `elekloader.zip`, the bridge loaded as the
   worker loads it, and every shop file through the bridge;
8. deploys it with `actions/upload-pages-artifact` and `actions/deploy-pages`.

No firmware reaches the workflow. Pages has to be on, with **GitHub
Actions** as its source (Settings > Pages). When a new release carries its
cores, run the workflow by hand to put them on the site.

## Build and try it locally

```bash
curl -fLO "$(python packaging/build_web.py --pyodide-url)"
python packaging/build_web.py --pyodide pyodide-core-314.0.7.tar.bz2 --core core-*.elemod --out build/site   # the release's cores
node tests/test_web.mjs build/site
python -m http.server --directory build/site 8000      # then open http://localhost:8000
```

The page needs module workers and WebAssembly, which current Chrome, Edge,
Firefox and Safari all have; it has been tested in Chromium. It also needs
a secure context (https, or localhost).

## How long it takes

Measured on one Windows 11 desktop PC, in Chromium, from a local server. The
builds in the table are the cores of that time (core 2.1, core-dn1 2.0a, core
0.1), not measured again since; a current build may differ.
A build is `patch.build` plus `patch.save`; each figure is the median of
three runs. CPython 3.14 is shown for comparison:

| | the page | CPython | of which packing (page / CPython) |
|---|---|---|---|
| loading the engine | 2.7 to 3.9 s | | |
| Digitakt mk1: core 2.1 + digislicer 2.0 | 11.9 s | 7.3 s | 6.2 s / 3.9 s |
| Digitone mk1: core-dn1 2.0a | 13.3 s | 7.7 s | 6.7 s / 4.1 s |
| Octatrack: core 0.1 + tuner | 5.0 s | 2.9 s | 2.7 s / 1.6 s |

The page downloads 13.2 MB the first time: Pyodide's WebAssembly runtime
(9.6 MB) and standard library (2.5 MB) are most of it, and gzip brings the
whole site to about 6.5 MB where the server compresses. The browser caches
it after that. Packing the main OS (aplib, pure Python) is about half of
every build. A build in the page takes about 1.6 to 1.7 times as long as in
CPython.
