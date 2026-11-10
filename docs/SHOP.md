# How to upload your mod to the Elekloader Shop

The shop is the library of mods on the [elekloader Mod Shop](https://elekloader.github.io/shop/), its own site.
Anyone can open it, tick the mods they want, and build a patched firmware in
their browser. This page is everything you need to get your mod listed, keep
it up to date, and take it down.

**The short version:** put your `.elemod` files in a release on your own
GitHub repository, fill in one form, and a bot checks your files and does the
rest. There is nothing to add to elekloader's repository, no evidence files,
and no account but GitHub's. A picture for your card is optional.

**Where:** the shop's list and all its forms are in
[elekloader/shop](https://github.com/elekloader/shop/issues/new/choose), not
in elekloader/elekloader. You don't open a pull request to list a mod: the
form does it.

The shop lists *your files*. Your code, your releases and your repository stay
yours, and so do questions and bug reports about your mod.

- [What you need](#what-you-need)
- [Upload, step by step](#upload-step-by-step)
- [Using an AI agent](#using-an-ai-agent)
- [What the check tells you](#what-the-check-tells-you)
- [If it says no](#if-it-says-no)
- [Updates: publish a release, that's all](#updates-publish-a-release-thats-all)
- [A thumbnail](#a-thumbnail)
- [Take a mod out](#take-a-mod-out)
- [Questions](#questions)

## What you need

1. **Your mod's `.elemod` files**, one for each firmware (OS) version it
   supports. [ADAPTING.md](ADAPTING.md) shows how to build them.
2. **A GitHub repository** to hold them. It can be a repository you made just
   for this.
3. **A licence that lets the shop pass your files on.** Any of: MIT, GPL,
   LGPL, BSD, Apache-2.0, MPL-2.0, ISC, 0BSD, CC0, the Unlicense. Put it in
   your mod's `mod.json` (`"license": "MIT"`), or type it into the form.
4. **No Elektron firmware in the files.** Your `.elemod` holds your mod's own
   code only. (The short stock fragments elekloader allows are fine.)

That's it.

## Upload, step by step

### 1. Put your files in a GitHub release

On your repository's page, open **Releases → Draft a new release**:

1. Pick or create a tag, such as `v1.0`.
2. Drag your `.elemod` files into **Attach binaries**.
3. If it's a beta, tick **Set as a pre-release**. Pre-releases are welcome.
4. Click **Publish release**. A release left as a *draft* is refused: it has
   to be published.

(Prefer a command line? One command builds your mod for each OS, makes the
release and opens the form for you. It needs GitHub's command line tool, `gh`:

```bash
python -m elekloader.sdk.publish my-mod --stock Digitakt_OS1.53.syx --stock Digitakt_OS1.54.syx \
    --summary "One sentence for the shop's card." --tested "What ran on a unit, and what not yet."
```

Your stock OS files stay on your machine; only the built `.elemod` files are
published. `--dry-run` shows what it would do without doing it. Then skip to
step 3.)

### 2. Open the form

Click **Submit a mod** on the shop page, or go straight to the
[Submit a mod form](https://github.com/elekloader/shop/issues/new?template=submit-mod.yml).
You'll be asked to sign in to GitHub if you aren't.

| Field | What to put |
|---|---|
| **Repository** | `owner/name`, as in the address bar: `yourname/my-mod`. |
| **Release tag** | The tag from step 1, such as `v1.0`. |
| **Summary** | One sentence for your mod's card (up to 300 characters). |
| **Tested on a unit** | What you've run on real hardware, and what you haven't yet. Be honest: it's shown on the card, and "not yet" is a fine answer. |
| **Licence** | Only if your files don't name one themselves. |
| **Only these files** | Only if the release holds files you *don't* want listed. Leave it empty to submit them all. A pack of boot splashes or screen pictures: name a few examples here, not the whole set; anyone can make their own in the [Splash screen maker](https://elekloader.github.io/shop/#splash). |
| **Updates** | Leave it unticked. See [Updates](#updates-publish-a-release-thats-all). |
| **Checks** | Tick both: you wrote the mod (or its licence lets it be passed on), and the files carry no Elektron firmware. |

Press **Submit new issue**.

*Keeping the files in the repository instead of a release?* Leave **Release
tag** empty and give the whole 40-character commit id in **Or a commit**. The
shop pins that exact commit.

### 3. Read the bot's comment

Within a minute or two a bot comments on your issue with a table of what it
found. If everything passes, it opens a pull request that adds your files to
the shop, and says so.

### 4. Wait for the merge

A maintainer looks at the pull request and merges it. The next deploy puts
your mod on the page. The pull request closes your issue by itself.

If the bot says no, you haven't lost anything: see the next two sections,
fix it, and the check runs again.

## Using an AI agent

Most uploads are done with an agent's help, and the whole path works without a
browser. The **Submit your mod** tab on the shop page has this prompt with a
Copy button:

```text
Submit my elekloader mod to the Elekloader Shop. First read the section "Using an AI agent" of
https://github.com/elekloader/elekloader/blob/main/docs/SHOP.md and follow it. In short: build the
.elemod files for each OS version I support, publish them in a GitHub release on my repository, then
open a "Submit a mod" issue on elekloader/shop with gh issue create. A bot checks the issue and
opens the pull request, so don't open one yourself. Before you publish or submit anything, show me the
release notes, the summary and the "Tested on a unit" text, and wait for my OK: I am the one declaring
that I wrote the mod (or its licence lets it be passed on) and that the files hold no Elektron
firmware. Then read the bot's comment on the issue and fix whatever it refuses.
```

**For the agent.** You don't open a pull request to submit a mod. You publish a
release on the author's repository and open an issue; a bot checks it and opens
the pull request itself.

1. **Build and release.** Build one `.elemod` per OS version the mod supports
   ([ADAPTING.md](ADAPTING.md); the repository's [AGENTS.md](../AGENTS.md) has
   the commands). The stock OS files are the author's and stay on their machine:
   publish only the built `.elemod` files. One command does both, and prints the
   issue form's address instead of opening it with `--no-browser`:

   ```bash
   python -m elekloader.sdk.publish MODDIR --stock Digitakt_OS1.53.syx --stock Digitakt_OS1.54.syx \
       --summary "One sentence." --tested "What ran on a unit, and what not yet." --no-browser
   ```

   Or `gh release create v1.0 *.elemod --repo owner/name` (a published release or
   pre-release; a draft is refused). A mod needs its licence in `mod.json`
   (`"license"`): MIT, GPL, LGPL, BSD, Apache-2.0, MPL-2.0, ISC, 0BSD, CC0 or
   the Unlicense.
2. **Ask the author before you publish or submit.** Show them the summary
   (one sentence, up to 300 characters), the "Tested on a unit" text (what ran
   on hardware, and what didn't yet: never claim a test that wasn't made) and the
   release notes. The two "Checks" below are the author's own declarations; don't
   tick them for them without their yes.
3. **Open the issue** with a body in the form's own layout (save it as
   `body.md`). The title must start with `Submit a mod`; an empty answer is
   `_No response_`:

   ```markdown
   ### Repository

   owner/name

   ### Release tag

   v1.0

   ### Or a commit

   _No response_

   ### Summary

   One sentence the shop shows on the card.

   ### Tested on a unit

   What ran on hardware, and what not yet.

   ### Licence, for files that name none

   _No response_

   ### Only these files

   _No response_

   ### Updates

   - [ ] Don't update these mods automatically; I'll send each new release through this form.

   ### Checks

   - [x] I wrote these mods, or their licence allows them to be passed on.
   - [x] The files carry no Elektron firmware, only the mod's own code (and the short stock fragments elekloader allows).
   ```

   ```bash
   gh issue create --repo elekloader/shop --title "Submit a mod: My Mod" --body-file body.md
   ```

   Leave "Updates" unticked unless the author wants to opt out of automatic
   updates (see [Opting out](#opting-out)). For files kept in the repository
   instead of a release, leave the tag empty and give the whole 40-character
   commit id under "Or a commit".
4. **Read the answer.** Within a minute or two the bot comments on the issue:
   `gh issue view NUMBER --repo elekloader/shop --comments`. If it
   refused something, [If it says no](#if-it-says-no) says what to change; fix
   the release (or the issue's text with `gh issue edit NUMBER --body-file
   body.md`) and the check runs again. If it passed, it has opened the pull
   request, and a maintainer merges it. Report the issue's address to the author.

## What the check tells you

The bot downloads your files and reads each one with elekloader. Its comment
has four parts:

- **What goes in:** each file's mod, device, OS and licence, checked against
  the sha256 GitHub lists, so nobody can swap a file after the fact.
- **What it needs:** a core for the file's device and OS that's as new as the
  one it names, and any other mod it requires (machine-pages, fxkit,
  machines...). The shop's copy will do; failing that, one from your own
  release comes along.
- **Good to know:** the core it builds with, how much RAM it takes, and
  which shop mods it can't share a build with. This is *not* a refusal: the
  page just greys out those cards beside yours. If it says your mod builds
  with an older core, *not the newest*, your mod patches a place the newest
  core patches too: it still builds, but not beside a mod that needs the
  newer core, until you build it on that core's event
  ([CORES.md](CORES.md), "Which file").
- **What it refuses, and why:** in plain words, so you know what to fix.

## If it says no

Edit the issue (the three dots on the top right, **Edit**) and the check runs
again. The messages you're most likely to see:

| What it says | What to do |
|---|---|
| *GitHub has no release v1.0 ... (a draft is not one until it is published)* | Publish the release, or fix the tag in the form. |
| *The release has no .elemod files attached* | Attach them in step 1 and save the release. |
| *refused: it names no licence* | Add `"license"` to `mod.json` and rebuild, or fill in **Licence** on the form. |
| *refused: licence 'X' does not allow the site to pass it on* | The shop only passes on the licences in [What you need](#what-you-need). |
| *refused: it needs core X or newer ...* | Your file names a core the site doesn't have yet. Rebuild against a core in the latest elekloader release (cores come from elekloader's own releases only). |
| *refused: it needs X ..., which neither the shop nor this release has* | Add the required mod's `.elemod` to your release. |
| *refused: not the file GitHub lists* | The download didn't match GitHub's checksum. Re-upload the file and try again. |
| *flagged, left out: the shop has X from someone else* | Your release bundles another author's mod that's already listed. That's usually fine to leave out. To add yours as a second build anyway, name the file under **Only these files**; a maintainer decides. |

Anything else, or a message that doesn't make sense: leave a comment on the
issue and a maintainer will look.

## Updates: publish a release, that's all

Once your mod is in the shop, you don't need the form again. **Updates are on
by default.** Every day the shop looks at your repository, and when you've
published a newer release it takes the newer files of your mods, with the same
checks as above. When every change is a newer file of a mod you already have
in the shop, it goes in by itself, and the page redeploys. Anything unusual
(a new mod, a clash) waits for a maintainer.

Your card keeps its text. The hardware note is marked with the version it was
about ("As of 1.2: ...") until you give a new one. To change either, put these
lines in the release's notes:

```
Shop summary: One sentence the card shows.
Tested on a unit: What ran on hardware, and what not yet.
```

A pre-release replaces a file that came from a pre-release. To replace a full
release with a pre-release, or to move a commit pin, use the form.

### Opting out

If you'd rather decide yourself which of your releases go in, opt out. Any one
of these:

- **On the form,** tick the **Updates** box when you submit.
- **In a release's notes,** add the line:

  ```
  Shop auto-update: off
  ```

  The shop doesn't take that release; it just stops following your
  repository. (`publish --no-auto-update` writes the line for you.)
- **For a mod that's already listed,** submit the form for the release the
  shop already has, with **Updates** ticked. Only the setting changes.

Opted out, you send each new release through the form, as the first time. To
opt back in, submit the form again with **Updates** unticked, or put
`Shop auto-update: on` in the release's notes.

## A thumbnail

Your card can show a picture of the device's screen: a `thumb.png` in the
release, or a `thumb-<mod id>.png` for each mod of a pack, 128×64 pixels in
two colours. [THUMBNAILS.md](THUMBNAILS.md) has
the rules, how to make one, and how it's reviewed.

## Take a mod out

The [Remove a mod form](https://github.com/elekloader/shop/issues/new?template=remove-mod.yml)
takes your files out of the shop: all of them, or the ones you name. When you
own the repository and no other shop mod needs your files, it goes through by
itself. Otherwise a maintainer looks first, and the check's comment says which
mods need what you're taking out.

## Questions

**Do I need to give elekloader anything, or add files to its repository?**
No. The shop only points at your release.

**Who can use my mod?** Anyone who opens the page. They build in their own
browser; their firmware never leaves their machine.

**Can I list a beta?** Yes: publish a pre-release. Say what you've tested.

**Someone reported a bug in my mod. Where does it go?** To your repository.
The shop lists files; you know your code.

**What waits for a maintainer rather than going straight in?**

- a mod that's new to the shop;
- a file of a mod the shop already takes from *another* repository;
- an issue opened by someone other than the repository's owner;
- a mod the shop *holds* (`"hold"` in its entry), because other mods build on
  it;
- a removal that other shop mods depend on.

How the shop's list and checks work inside is in
[WEB.md](WEB.md#the-mod-shop-the-librarys-cards).
