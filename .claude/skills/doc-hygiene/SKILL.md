---
name: doc-hygiene
description: Keeps elekloader's markdown lean. It says where each kind of fact lives, names the bloat this repo grows (changelog prose in reference text, investigation stories and test logs, the guide swallowing the API reference, one explanation copied into many READMEs, AGENTS.md turning into a second wiki, generated snapshots), and has an audit script that sizes the docs and checks links and `FILE.md, "Section"` references before and after a move. Use it whenever you review, trim, split or reorganize docs; when a change adds a section to docs/, AGENTS.md, README.md or a mod's README; when a new core or mod version needs its docs updated; before moving or renaming a heading; or when someone says the docs are bloated, messy, too long or hard to find things in.
---

# Doc hygiene

In October 2026 the markdown grew from about 1,500 lines to 13,000, and
docs/ADAPTING.md from 474 lines to 2,943. Little of it is wrong. Most of it
is in the wrong place, or it is the story of how something came to be,
told where the reader only needs what it is now. Keep each fact in one
home, and say it once, as it is today.

Who reads what decides where it goes:

- **users**: README.md, docs/INSTALL.md, docs/APPS.md, docs/WEB.md;
- **mod authors**: docs/ADAPTING.md (the guide), docs/FORMAT.md,
  docs/CORES.md, the per-device API reference, the cores' headers in
  elekloader/sdk/include;
- **site owners**: docs/INTEGRATING.md;
- **maintainers**: docs/DEVICES.md, BUILDING, TESTS, RELEASING, a mod's
  README;
- **coding agents**: AGENTS.md, which every session reads in full
  (CLAUDE.md imports it). A line there costs every session, whatever it
  works on.

## Run the audit first

From the repository's root:

```bash
python .claude/skills/doc-hygiene/scripts/doc_audit.py
python .claude/skills/doc-hygiene/scripts/doc_audit.py --since origin/main
python .claude/skills/doc-hygiene/scripts/doc_audit.py --detail docs/CORES.md
```

The first lists files over their budget, sections over 8 KB, the files
with the most history phrasing (version stories, PR numbers, dates, check
logs), paragraphs repeated across files, and broken relative links and
section references. `--since` adds what a branch made each file gain.
`--detail` lists one file's sections, with their sizes (with and without
their subsections) and every line of history phrasing. `--strict` exits 1
on a broken link or reference.

The reference check reads `FILE.md, "Title"` and `"Title" in FILE.md`,
wrapped or not, in the docs and in the code's comments and messages. It
cannot check a bare `("Insert audio")`, so name the file every time.

A budget asks you to look, it is not a limit. A file over budget is one
where you check what it carries. Blocks between
`<!-- name: generated ... -->` and `<!-- /name -->` are counted apart.
Change them only through the tool that writes them.

## One home per fact

| the fact | its home | not in |
|---|---|---|
| install, update, recover | README.md (short), docs/INSTALL.md | the guide |
| how to write a mod: the paths, the rules, the build loop, refusals, done | docs/ADAPTING.md | per-core API detail |
| what a core and its companions give mods (events, exports, tables, calls) | the per-device API reference¹ and the core's header | ADAPTING, CORES, AGENTS |
| which core file to use; what each core and each companion is for | docs/CORES.md: a short entry per core as it is now (a line, then a bullet a feature) | a list of what each version added |
| the file format, the linker, its checks | docs/FORMAT.md | the guide (link to it) |
| a device's facts: containers, addresses, protected bytes | elekloader/devices/ and docs/DEVICES.md | mod READMEs |
| a mod's sites, build and checks | mods/<mod>/README.md | AGENTS.md |
| what changed in a version | that tag's release notes (`gh release view <tag>`), or its merged PR | any doc |
| how a bug was found, what a run showed | the PR description | any doc |
| where code lives, and the agents' rules | AGENTS.md: one line a path | anywhere else |

¹ The per-device reference does not exist yet. For now it is docs/ADAPTING.md
section 2's subsections. The proposal is one page per device, named like the
include folders (`docs/api/octatrack.md`, `docs/api/digitone-mk1.md`, ...).
Until the split, put a new API section with its device's other sections,
and keep the rules section itself to the rules.

## The bloat this repo grows

### 1. Changelog prose in reference text

**Looks like:** "From 3.0 ... From 3.1 ... From 3.2 ..." (docs/CORES.md,
"What each core adds"; the bullets of mods/core-dn1/README.md); "0.2 took
1,707 pages whatever the mods used"; "(27 arena writes, was 28)".

**Why it hurts:** the reader wants what the newest version does, and has
to work it out from the history. Each release adds a sentence, and nothing
takes one away.

**Fix:** describe what main has now: the version in each `mod.json`,
released or not. Keep a version number only where a reader acts on it,
and write it as a fact about now, not as news:

- a feature a mod has to declare: the `resources.core` it needs
  ("exclusive audio (core-dn1 3.2)");
- a limit people still live with ("on OS 1.73, core-ar2 0.2 keeps 24 bits
  a sound");
- a released build people still run, as an instruction ("cores before
  0.3.1 freeze on PURGE SAMPLES: update").

A feature the build picks for itself, like 4.0's RAM, needs no version.
The rest of the story is in the release notes or the PR (step 3 below).

Before:

> From 3.2 a mod may take the whole output, with the synths silent, and
> keep large buffers in the profile's `bulk` area (ADAPTING.md,
> "Exclusive audio"), and take MIDI CCs before the stock applies them
> (`ev_midi_cc`). From 3.3 a mod sets key LEDs over the stock views'
> (`ev_key_leds`) and ...

After:

> - exclusive audio, one mod taking the whole output (3.2; ADAPTING.md,
>   "Exclusive audio");
> - MIDI CCs before the stock applies them (`ev_midi_cc`, 3.2);
> - key LEDs over the stock views' (`ev_key_leds`, 3.3; ADAPTING.md,
>   "Key LEDs and each block's sound parameters").

The entry says what the core gives and where to read more. The API itself,
like the `bulk` area, stays in the reference.

### 2. Investigation stories and test logs

**Looks like:** the emulator addresses where a walk stopped, whose unit
froze on which build, "4.0 was checked the same way on 1.43 and 1.44:
every stage passes ...", upstream PR numbers, dates.

**Why it hurts:** it is evidence for whoever reviews the change. After the
merge it is noise that reads like instructions.

**Fix:** keep the fact and its reason, in a sentence or two
("0x400254f8 stays stock: it ends PURGE SAMPLES' walk over the banks, not
the arena"). The evidence goes in the PR and the release notes. To say
how a mod is checked, give the command and what it should print, once, in
its README's "Checks" or docs/TESTS.md. Do not keep a log entry per
version.

### 3. The guide swallowing the reference

**Looks like:** "2. The rules" in docs/ADAPTING.md: twelve rules, then
forty sections of per-device, per-core API, about 140 KB of the file's
180 KB.

**Why it hurts:** a mod author targets one device, and reads past five
others to find theirs. The guide's own path (pick a path, build, lint,
patch, refusals, done) gets lost in the middle.

**Fix:** ADAPTING stays the guide, and links to the reference for each
device. A new event, export or table goes in its device's reference,
with a line in CORES.md if it changes what that core is for.

### 4. One explanation in many places

**Looks like:** the pedal shell explained in each of the seven pedal
READMEs; "From 0.2 DigiHalo is an effect on fxkit ..." copied into the
others with the name changed (the audit's repeated paragraphs).

**Fix:** say it once, at its home (mods/fxkit-dn1/README.md, or one
pedal-shell section), and have the others link to it with one sentence on
what they do differently.

### 5. AGENTS.md as a second wiki

**Looks like:** path-table rows that run to a paragraph, with every version
("from 0.5 ... from 0.6 ... from 0.7.1"), and a row for every mod.

**Why it hurts:** every session pays for every row. An agent working on a
mod opens its folder and reads its README anyway.

**Fix:** a row says what a path is for, in about 25 words at most. Mods are
grouped by kind (the cores, the companions, whole mods), and each folder's
README has the rest. The rules stay, short.

### 6. Generated snapshots

**Looks like:** docs/CONTRIBUTE.md's to-do list: `sdk.bounties` output,
dated, about 47 KB of the page's 53 KB, while the web page's Contribute
view shows the same list live.

**Fix:** a committed copy has to be worth its size. If it stays, keep it at
summary level (counts per device, the top entries) and link to the live
view. Never edit inside the markers by hand: change the generator or its
options.

## Trimming, step by step

1. Run the audit, with `--since` on a branch. Take one file, or one
   pattern, per pass: a docs PR that touches everything cannot be
   reviewed.
2. Run `--detail FILE` and go through its sections: who reads it, what do
   they do with it, is this its home, does it say how things are now?
   "Now" means complete as well as short, so check the text against the
   code: each mod's `mod.json` version, `requires` and `resources.core`,
   and the ADAPTING headings that name a core. Add what is missing, and
   correct what is wrong.
3. Before you cut a fact, find where else it survives. Most versions were
   never released, so the release notes are not enough on their own. Look
   in the merged PR (`gh pr list -R irpina/elekloader --state merged
   --search "<words>"`), in `git log -S"<phrase>"`, in a comment by the
   code, or in another doc. A published release's notes are the
   maintainer's, so ask before you edit one; otherwise put the text in your
   PR. If the fact survives nowhere and someone could still need it, move
   it. If its home is outside this pass, leave it where it is and list it
   in the PR as "to move to X".
4. Before you move or rename a heading, look for what points at it:
   anchors (`#exclusive-audio`), and references by title
   (`docs/ADAPTING.md, "Exclusive audio"`) in the docs, in the headers in
   elekloader/sdk/include, and in messages the code prints. The audit's
   last two lists check both. Run it again after the move and fix what it
   lists.
5. These stay where they are:
   - README.md's recovery paragraph (web/app.js's fallback says "see the
     README");
   - ADAPTING's refusal table and its definition of done (AGENTS.md
     promises both);
   - AGENTS.md's Rules and "Done means";
   - any heading a code message names.
6. Docs-only changes go in their own PR, apart from code, so the review is
   about the words. Say in it what moved where, and that the audit lists
   no broken links or references.

## Writing docs without growing the pile

- Find the home first (the table above). If it is over budget, trim it in
  the same PR, or say in the PR that it is over.
- A new version of a core or mod: change the text in place so it says what
  the mod does now. The version's story goes in its release notes. Write a
  "(core-dn1 3.2)" only where a mod author has to declare it.
  docs/CORE-CONTRIBUTING.md asks for "a line in 'What each core adds'"
  with each change to a core. Fold it into that core's entry, as a bullet
  for the feature. Do not add a "From X.Y" sentence.
- Point at a section as `FILE.md, "Title"` (naming the file every time), or
  with a link and its anchor. The audit checks those, and nothing looser.
- Write how things are, in the repo's plain style: short sentences, what
  it does, then the one thing the reader must know. Leave out the
  backstory, and words like "now", "new", "since" and "no longer".
- Link instead of retelling: one sentence of context and the link.
- What you ran and what it showed goes in the PR.

## Budgets

They live in `BUDGETS` in scripts/doc_audit.py: AGENTS.md 8 KB, README.md
and docs/README.md 8 KB each, a docs page 40 KB, a mod's README 16 KB, an
example's 6 KB, a section 8 KB. When a file really needs more, raise its
budget in the script and give the reason in the commit. Don't let them
drift.
