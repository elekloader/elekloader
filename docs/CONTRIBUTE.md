[elekloader docs](README.md) › Contribute

# Contribute to elekloader!

elekloader is a passion project, and it grows from what people bring to it:
the core's events came from mods that needed them, and support for
Digitakt II came from an outside contributor. Pull requests are welcome
and encouraged, and you don't need to ask first.

Help the community grow and earn a spot on the leaderboard: claim an
entry of the to-do list ([below](#claiming-an-entry)), and your name goes
on the web page's leaderboard, on your pull request and in the release
notes of the version that takes it. The list on this page is the
community's to-do list.

New to it? The web page's Contribute view has short guides:
[Get started developing](https://irpina.github.io/elekloader/#contribute/start),
[What is a core mod?](https://irpina.github.io/elekloader/#contribute/core),
[Your first pull request](https://irpina.github.io/elekloader/#contribute/pr) and
[Test before you flash](https://irpina.github.io/elekloader/#contribute/test).

## Ways to help

- **Take an entry from the to-do list below.** Each one is stock code that
  a mod still hooks for itself. When it becomes a core event, any number of
  mods can use that code, and none of them needs its own port for the next OS.
- **Write a mod** on the core ([Adapting your mod](ADAPTING.md)), and offer it
  to the shop ([The web page](WEB.md)).
- **Add a device or an OS version** ([Adding a device](DEVICES.md)).
- **Port a mod onto what the core has.** Some sites below are hooked by a
  core or a companion too, so those mods can use what it offers. Those
  pull requests go to the mod's own repository, and its author decides.

## The to-do list

Each entry is one stock routine that one or more mods hook themselves. Its
"to do" says what is left:

- **a core event**: the core has nothing there yet. Build the event as
  [Growing the core](CORE-CONTRIBUTING.md) describes, sections 3 and 4.
- **move onto the core**: the core or a companion (machine-pages, machines,
  seq-events, dspbus, intercepts, usb, settings) hooks the same bytes as every site there, so the
  mods can use it in place of their own sites.
- **some sites move onto the core, the rest need an event**: both of the
  above, in one routine.

The entry's id names the device, the oldest OS and the routine's lowest
site. An entry is done when it leaves the list, which happens when the
mods there are rebuilt on the core and the shop has the new files.

### Upstream: octabam's newer modules

The shop's Octatrack mods are octabam's modules as of its commit 363861e
(30 September), converted ([octabam2elemod](https://github.com/irpina/octabam2elemod)).
[sambanks/octabam](https://github.com/sambanks/octabam) has moved on a long
way since: new modules (BRAIN and its settings store, KITS, PLOCKS P2,
STEMS, REMIX SWITCH, the source machines, new effects), USB in one
module, MIDI SCENES rewritten. The stock code those newer modules hook
is on the list too, read from octabam's manifests
([web/upstream/octabam.json](../web/upstream/octabam.json)), so the core
can get there before the conversions do:

- a mod marked **(upstream)** hooks those sites in octabam's newer
  modules, not in a shop file;
- an entry marked **upstream only** has no shop file in it: only those
  modules hook it. Its id is its lowest site, like any other;
- an entry the shop's files have keeps its id when upstream sites join
  it, so a claim on it still counts;
- the score counts the shop's files only.

When the shop has a module converted from that commit, the file stands
for it, and its upstream sites leave the list. Some of the modules do
not convert yet (each one's reason is in the file, `refused`), but the
core can serve their hooks all the same, and that is what makes them
convertible.

To read a newer octabam (a checkout with its submodules; the stock
Octatrack file decides where a `jmp` ends):

```bash
python -m elekloader.sdk.octabam --octabam <octabam checkout> --stock OCTATRACK_OS1.40C.syx --sites web/upstream/octabam.json
```

## Claiming an entry

Put a line in your pull request's description that names the entries it
does, by their ids:

```
Claims: digitakt-mk1@1.53:0x400d553e, digitakt-mk1@1.53:0x400721e6
```

When the pull request is merged, the next build of the web page counts it:
each entry it names is a point beside your GitHub name on the
**leaderboard** in the page's Contribute view, and the entry shows who
claimed it until it leaves the list.

- **One point an entry.** The first merged pull request that claims an
  entry takes it; a later claim on the same entry scores nothing.
- **A plain line claims.** One in a code block, an HTML comment or a quote
  (`>`) is an example and claims nothing; ids in backticks on a plain line
  count.
- **Open it as a draft early,** with its `Claims:` line, so others see the
  entry is taken.
- **A "move onto the core" entry** is done in the mod's own repository; its
  claim goes on the pull request here that brings the rebuilt mod into the
  shop (`web/catalog.json`).
- **Your merge is the review:** a claim counts only on a merged pull
  request here, and the maintainers merge what does the entry.

The board is made from the merged pull requests themselves (the pages
workflow lists them, `packaging/build_web.py` reads their `Claims:`
lines), so nobody keeps it by hand. A fork's board counts the fork's own
pull requests.

The web page's **Contribute** view always shows the list for the shop's
current files ([irpina.github.io/elekloader/#contribute](https://irpina.github.io/elekloader/#contribute)).
The site holds no firmware, so it groups sites by distance rather than
by routine, and so it shows a few more entries than the copy below. To
make this copy again, with the shop's files, the upstream list and the
stock OS files:

```bash
python -m elekloader.sdk.bounties <folder of the shop's .elemod files> --upstream web/upstream/octabam.json --stock Digitakt_OS1.53.syx --stock Digitakt_OS1.54_dist.zip --stock Digitone_OS1.43.syx --stock Digitone_and_Digitone_Keys_OS1.44_dist.zip --stock OCTATRACK_OS1.40C.syx --update docs/CONTRIBUTE.md
```

<!-- bounties: generated by elekloader.sdk.bounties -->

Made on 2026-10-22; grouped by routine, with the stock OS files.

### Digitakt mk1 (OS 1.53, 1.54)

3 of 16 mods hook no stock code of their own. 53 routines to do, 15 of them hooked by more than one mod.

| entry | to do | mods | what they hook there |
|---|---|---|---|
| `digitakt-mk1@1.53:0x40077d72` | some sites move onto the core, the rest need an event | 8 | digihealth: r_40074b9e, r_4007472a, r_40075184 +11; digimatrix: digimatrix_prehook, digimatrix_posthook; digineighbor: nb_inject_s, nb_tap_s; digipoly: digipoly_lvlhook; digisophie: ds_inject_s; digistring: digistring_inject_s; digistring-fast: digistring_inject_s; digiutils: digiutils_trig, digiutils_tap |
| `digitakt-mk1@1.53:0x4000f2bc` | some sites move onto the core, the rest need an event | 4 | digineighbor: nb_knob_gfx; digisophie: ds_knob_gfx; digistring: digistring_knob_s; digistring-fast: digistring_knob_s |
| `digitakt-mk1@1.53:0x4000f324` | some sites move onto the core, the rest need an event | 4 | digineighbor: nb_val_text; digisophie: ds_val_text; digistring: digistring_val_text_s; digistring-fast: digistring_val_text_s |
| `digitakt-mk1@1.53:0x4000fe8a` | some sites move onto the core, the rest need an event | 4 | digineighbor: nb_lab_short; digisophie: ds_lab_short; digistring: digistring_lab_short_s; digistring-fast: digistring_lab_short_s |
| `digitakt-mk1@1.53:0x4000feac` | some sites move onto the core, the rest need an event | 4 | digineighbor: nb_lab_long; digisophie: ds_lab_long; digistring: digistring_lab_long_s; digistring-fast: digistring_lab_long_s |
| `digitakt-mk1@1.53:0x40065794` | some sites move onto the core, the rest need an event | 4 | digineighbor: nb_ui_rec; digisophie: ds_ui_rec; digistring: digistring_ui_rec_s; digistring-fast: digistring_ui_rec_s |
| `digitakt-mk1@1.53:0x400657ee` | some sites move onto the core, the rest need an event | 4 | digineighbor: nb_pop_text; digisophie: ds_pop_text; digistring: digistring_pop_text_s; digistring-fast: digistring_pop_text_s |
| `digitakt-mk1@1.53:0x40060b8e` | some sites move onto the core, the rest need an event | 3 | digisophie: ds_lfo_label; digistring: digistring_lfo_label_s; digistring-fast: digistring_lfo_label_s |
| `digitakt-mk1@1.53:0x40065de6` | some sites move onto the core, the rest need an event | 3 | digisophie: ds_lfo_overview_group, ds_lfo_overview_name; digistring: digistring_lfo_ov_group_s, digistring_lfo_ov_name_s; digistring-fast: digistring_lfo_ov_group_s, digistring_lfo_ov_name_s |
| `digitakt-mk1@1.53:0x400a4374` | some sites move onto the core, the rest need an event | 3 | digisophie: ds_lfo_popup_name, ds_lfo_popup_fallback; digistring: digistring_lfo_pop_s, digistring_lfo_pop_fb_s; digistring-fast: digistring_lfo_pop_s, digistring_lfo_pop_fb_s |
| `digitakt-mk1@1.53:0x400d553e` | a core event | 3 | digipoly: digipoly_rechook; digislicer: dsl_kbnote; ob1: ob1_live_note |
| `digitakt-mk1@1.53:0x400325e8` | a core event | 2 | digistring: digistring_layout_s; digistring-fast: digistring_layout_s |
| `digitakt-mk1@1.53:0x4003ab02` | a core event | 2 | digistring: digistring_layout_s; digistring-fast: digistring_layout_s |
| `digitakt-mk1@1.53:0x400721e6` | a core event | 2 | digieq: digieq_master; tonefx: tonefx_master_glue |
| `digitakt-mk1@1.53:0x40000438` | a core event | 1 | digihealth: task_switch |
| `digitakt-mk1@1.53:0x40008904` | a core event | 1 | digislicer: dsl_kbstep |
| `digitakt-mk1@1.53:0x40008d40` | a core event | 1 | digislicer: dsl_kbrec |
| `digitakt-mk1@1.53:0x40010d50` | a core event | 1 | ob1: mp_prange_s |
| `digitakt-mk1@1.53:0x40025338` | a core event | 1 | digislicer: dsl_kbpget |
| `digitakt-mk1@1.53:0x40025344` | a core event | 1 | digislicer: dsl_kbpage |
| `digitakt-mk1@1.53:0x40028bae` | a core event | 1 | digipoly: digipoly_prevon_hook |
| `digitakt-mk1@1.53:0x40028c60` | a core event | 1 | digipoly: digipoly_prevoff_hook |
| `digitakt-mk1@1.53:0x40028f3c` | a core event | 1 | digislicer: dsl_kbpred |
| `digitakt-mk1@1.53:0x4002965a` | a core event | 1 | digislicer: dsl_kbled |
| `digitakt-mk1@1.53:0x400299b2` | a core event | 1 | digislicer: dsl_kbpfmt, dsl_kblim |
| `digitakt-mk1@1.53:0x40044acc` | a core event | 1 | digieq: digieq_gfx |
| `digitakt-mk1@1.53:0x4005f91c` | a core event | 1 | digislicer: play_fmt |
| `digitakt-mk1@1.53:0x4005fa2e` | a core event | 1 | digislicer: grid_fmt |
| `digitakt-mk1@1.53:0x4006060a` | a core event | 1 | digislicer: play_icon |
| `digitakt-mk1@1.53:0x4006f7ea` | a core event | 1 | digipoly: digipoly_tag |
| `digitakt-mk1@1.53:0x40074df2` | a core event | 1 | digislicer: slice_win |
| `digitakt-mk1@1.53:0x40075282` | a core event | 1 | digislicer: pp_read0 |
| `digitakt-mk1@1.53:0x400759dc` | a core event | 1 | digislicer: pp_readv |
| `digitakt-mk1@1.53:0x400760ba` | a core event | 1 | digislicer: pp_read2 |
| `digitakt-mk1@1.53:0x400771e8` | a core event | 1 | digipoly: digipoly_setparam |
| `digitakt-mk1@1.53:0x400777a2` | a core event | 1 | digipoly: digipoly_route |
| `digitakt-mk1@1.53:0x4007919e` | a core event | 1 | ob1: core_midi_cc_parameter |
| `digitakt-mk1@1.53:0x4007a4ce` | a core event | 1 | dt-fm: ds_kit_load |
| `digitakt-mk1@1.53:0x4007a6a6` | a core event | 1 | dt-fm: ds_kit_save |
| `digitakt-mk1@1.53:0x4007efe8` | a core event | 1 | dt-fm: ds_proj_load |
| `digitakt-mk1@1.53:0x4007f30a` | a core event | 1 | dt-fm: ds_proj_save |
| `digitakt-mk1@1.53:0x40095aec` | a core event | 1 | digiutils: digiutils_patchk |
| `digitakt-mk1@1.53:0x400a16c4` | a core event | 1 | digislicer: dsl_kbpred2 |
| `digitakt-mk1@1.53:0x400a2042` | a core event | 1 | digislicer: dsl_kbmfmt |
| `digitakt-mk1@1.53:0x400aee4a` | a core event | 1 | digiutils: digiutils_popkey, digiutils_made2 |
| `digitakt-mk1@1.53:0x400d4a90` | a core event | 1 | tonefx: digictl_cc_disp |
| `digitakt-mk1@1.53:0x400d52f0` | a core event | 1 | digihealth: hook_sysex |
| `digitakt-mk1@1.53:0x400d5816` | a core event | 1 | ob1: ob1_live_note |
| `digitakt-mk1@1.53:0x400657cc` | move onto the core | 2 | digineighbor: nb_layout; digisophie: ds_layout |
| `digitakt-mk1@1.53:0x4000f534` | move onto the core (no address on 1.54) | 1 | digisophie: ds_prange |
| `digitakt-mk1@1.53:0x4000f5fc` | move onto the core (no address on 1.54) | 1 | digisophie: ds_prange_f |
| `digitakt-mk1@1.53:0x4000ff20` | move onto the core (no address on 1.54) | 1 | digisophie: ds_prange |
| `digitakt-mk1@1.53:0x400100c4` | move onto the core (no address on 1.54) | 1 | digisophie: ds_prange |

### Digitone mk1 (OS 1.43, 1.44)

9 of 10 mods hook no stock code of their own. 2 routines to do, 0 of them hooked by more than one mod.

| entry | to do | mods | what they hook there |
|---|---|---|---|
| `digitone-mk1@1.43:0x40000438` | a core event | 1 | digihealth: task_switch |
| `digitone-mk1@1.43:0x400ea880` | a core event | 1 | digihealth: hook_sysex |

### Octatrack (OS 1.40C)

2 of 34 mods hook no stock code of their own. 243 routines to do, 80 of them hooked by more than one mod. Upstream (sambanks/octabam@b085c4d), 43 mods hook stock code these files do not: 40 of them are not here at all. Their sites are in 219 entries, 153 of which only they hook ("upstream only").

| entry | to do | mods | what they hook there |
|---|---|---|---|
| `octatrack@1.40C:0x4000d99a` | a core event | 22 | octabam-boot-trace (upstream): tr_frame_end; octabam-cfmeter (upstream): m_tail; octabam-load-meter (upstream): lm_out; octabam-tuner: tu_frame; octabam-usb (upstream): audio_frame_shim; octabam-usb-audio-out-main: audio_frame_shim; octabam-usb-audio-out-main-cue: audio_frame_shim; octabam-usb-audio-out-master: audio_frame_shim; octabam-usb-audio-out-tracks: audio_frame_shim; octabam-usb-audio-out-tracks-main-cue: audio_frame_shim; octabam-usb-io-main-ab: audio_frame_shim; octabam-usb-io-main-abcd: audio_frame_shim; octabam-usb-io-main-cd: audio_frame_shim; octabam-usb-io-main-cue-ab: audio_frame_shim; octabam-usb-io-main-cue-abcd: audio_frame_shim; octabam-usb-io-main-cue-cd: audio_frame_shim; octabam-usb-io-tracks-ab: audio_frame_shim; octabam-usb-io-tracks-abcd: audio_frame_shim; octabam-usb-io-tracks-cd: audio_frame_shim; octabam-usb-io-tracks-main-cue-ab: audio_frame_shim; octabam-usb-io-tracks-main-cue-abcd: audio_frame_shim; octabam-usb-io-tracks-main-cue-cd: audio_frame_shim |
| `octatrack@1.40C:0x400108b0` | a core event | 19 | octabam-usb (upstream): usbmidi_prio_shim; octabam-usb-audio-out-main: usbmidi_prio_shim; octabam-usb-audio-out-main-cue: usbmidi_prio_shim; octabam-usb-audio-out-master: usbmidi_prio_shim; octabam-usb-audio-out-tracks: usbmidi_prio_shim; octabam-usb-audio-out-tracks-main-cue: usbmidi_prio_shim; octabam-usb-io-main-ab: usbmidi_prio_shim; octabam-usb-io-main-abcd: usbmidi_prio_shim; octabam-usb-io-main-cd: usbmidi_prio_shim; octabam-usb-io-main-cue-ab: usbmidi_prio_shim; octabam-usb-io-main-cue-abcd: usbmidi_prio_shim; octabam-usb-io-main-cue-cd: usbmidi_prio_shim; octabam-usb-io-tracks-ab: usbmidi_prio_shim; octabam-usb-io-tracks-abcd: usbmidi_prio_shim; octabam-usb-io-tracks-cd: usbmidi_prio_shim; octabam-usb-io-tracks-main-cue-ab: usbmidi_prio_shim; octabam-usb-io-tracks-main-cue-abcd: usbmidi_prio_shim; octabam-usb-io-tracks-main-cue-cd: usbmidi_prio_shim; octabam-usb-midi: usbmidi_prio_shim |
| `octatrack@1.40C:0x40010bc8` | a core event | 19 | octabam-usb (upstream): usbmidi_send_shim; octabam-usb-audio-out-main: usbmidi_send_shim; octabam-usb-audio-out-main-cue: usbmidi_send_shim; octabam-usb-audio-out-master: usbmidi_send_shim; octabam-usb-audio-out-tracks: usbmidi_send_shim; octabam-usb-audio-out-tracks-main-cue: usbmidi_send_shim; octabam-usb-io-main-ab: usbmidi_send_shim; octabam-usb-io-main-abcd: usbmidi_send_shim; octabam-usb-io-main-cd: usbmidi_send_shim; octabam-usb-io-main-cue-ab: usbmidi_send_shim; octabam-usb-io-main-cue-abcd: usbmidi_send_shim; octabam-usb-io-main-cue-cd: usbmidi_send_shim; octabam-usb-io-tracks-ab: usbmidi_send_shim; octabam-usb-io-tracks-abcd: usbmidi_send_shim; octabam-usb-io-tracks-cd: usbmidi_send_shim; octabam-usb-io-tracks-main-cue-ab: usbmidi_send_shim; octabam-usb-io-tracks-main-cue-abcd: usbmidi_send_shim; octabam-usb-io-tracks-main-cue-cd: usbmidi_send_shim; octabam-usb-midi: usbmidi_send_shim |
| `octatrack@1.40C:0x4001d824` | a core event | 19 | octabam-usb (upstream): audio_getiface_shim, usbmidi_clamp1_shim, stub_fs_config +10; octabam-usb-audio-out-main: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +4; octabam-usb-audio-out-main-cue: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +4; octabam-usb-audio-out-master: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +4; octabam-usb-audio-out-tracks: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +4; octabam-usb-audio-out-tracks-main-cue: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +4; octabam-usb-io-main-ab: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-main-abcd: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-main-cd: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-main-cue-ab: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-main-cue-abcd: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-main-cue-cd: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-tracks-ab: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-tracks-abcd: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-tracks-cd: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-tracks-main-cue-ab: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-tracks-main-cue-abcd: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-io-tracks-main-cue-cd: audio_getiface_shim, usbmidi_clamp1_shim, usbmidi_clamp2_shim +6; octabam-usb-midi: usbmidi_clamp1_shim, usbmidi_clamp2_shim, usbmidi_setcfg_shim +1 |
| `octatrack@1.40C:0x4001e606` | a core event | 19 | octabam-usb (upstream): audio_isr_shim, audio_reset_shim, audio_sessend_shim; octabam-usb-audio-out-main: audio_isr_shim; octabam-usb-audio-out-main-cue: audio_isr_shim; octabam-usb-audio-out-master: audio_isr_shim; octabam-usb-audio-out-tracks: audio_isr_shim; octabam-usb-audio-out-tracks-main-cue: audio_isr_shim; octabam-usb-io-main-ab: audio_isr_shim; octabam-usb-io-main-abcd: audio_isr_shim; octabam-usb-io-main-cd: audio_isr_shim; octabam-usb-io-main-cue-ab: audio_isr_shim; octabam-usb-io-main-cue-abcd: audio_isr_shim; octabam-usb-io-main-cue-cd: audio_isr_shim; octabam-usb-io-tracks-ab: audio_isr_shim; octabam-usb-io-tracks-abcd: audio_isr_shim; octabam-usb-io-tracks-cd: audio_isr_shim; octabam-usb-io-tracks-main-cue-ab: audio_isr_shim; octabam-usb-io-tracks-main-cue-abcd: audio_isr_shim; octabam-usb-io-tracks-main-cue-cd: audio_isr_shim; octabam-usb-midi: usbmidi_isr_shim |
| `octatrack@1.40C:0x4001d4b2` | a core event | 18 | octabam-usb (upstream): audio_ep0page_shim; octabam-usb-audio-out-main: audio_ep0page_shim; octabam-usb-audio-out-main-cue: audio_ep0page_shim; octabam-usb-audio-out-master: audio_ep0page_shim; octabam-usb-audio-out-tracks: audio_ep0page_shim; octabam-usb-audio-out-tracks-main-cue: audio_ep0page_shim; octabam-usb-io-main-ab: audio_ep0page_shim; octabam-usb-io-main-abcd: audio_ep0page_shim; octabam-usb-io-main-cd: audio_ep0page_shim; octabam-usb-io-main-cue-ab: audio_ep0page_shim; octabam-usb-io-main-cue-abcd: audio_ep0page_shim; octabam-usb-io-main-cue-cd: audio_ep0page_shim; octabam-usb-io-tracks-ab: audio_ep0page_shim; octabam-usb-io-tracks-abcd: audio_ep0page_shim; octabam-usb-io-tracks-cd: audio_ep0page_shim; octabam-usb-io-tracks-main-cue-ab: audio_ep0page_shim; octabam-usb-io-tracks-main-cue-abcd: audio_ep0page_shim; octabam-usb-io-tracks-main-cue-cd: audio_ep0page_shim |
| `octatrack@1.40C:0x40004bc0` | a core event | 15 | octabam-cfmeter (upstream): m_e6, m_e6x; octabam-stems (upstream): stems_frame_hook; octabam-usb (upstream): in_state7_shim; octabam-usb-io-main-ab: in_state7_shim; octabam-usb-io-main-abcd: in_state7_shim; octabam-usb-io-main-cd: in_state7_shim; octabam-usb-io-main-cue-ab: in_state7_shim; octabam-usb-io-main-cue-abcd: in_state7_shim; octabam-usb-io-main-cue-cd: in_state7_shim; octabam-usb-io-tracks-ab: in_state7_shim; octabam-usb-io-tracks-abcd: in_state7_shim; octabam-usb-io-tracks-cd: in_state7_shim; octabam-usb-io-tracks-main-cue-ab: in_state7_shim; octabam-usb-io-tracks-main-cue-abcd: in_state7_shim; octabam-usb-io-tracks-main-cue-cd: in_state7_shim |
| `octatrack@1.40C:0x4001e030` | a core event | 14 | octabam-usb (upstream): crossbar_init_shim; octabam-usb-crossbar: crossbar_init_shim; octabam-usb-io-main-ab: crossbar_init_shim; octabam-usb-io-main-abcd: crossbar_init_shim; octabam-usb-io-main-cd: crossbar_init_shim; octabam-usb-io-main-cue-ab: crossbar_init_shim; octabam-usb-io-main-cue-abcd: crossbar_init_shim; octabam-usb-io-main-cue-cd: crossbar_init_shim; octabam-usb-io-tracks-ab: crossbar_init_shim; octabam-usb-io-tracks-abcd: crossbar_init_shim; octabam-usb-io-tracks-cd: crossbar_init_shim; octabam-usb-io-tracks-main-cue-ab: crossbar_init_shim; octabam-usb-io-tracks-main-cue-abcd: crossbar_init_shim; octabam-usb-io-tracks-main-cue-cd: crossbar_init_shim |
| `octatrack@1.40C:0x4001fbd0` | a core event | 5 | octabam-boot-trace (upstream): tr_panel; octabam-cfmeter (upstream): m_isr; octabam-cfmeter-idle (upstream): m_idle; octabam-kits (upstream): kits_bootblank; octabam-midi-scenes: bank_inv |
| `octatrack@1.40C:0x4005a848` | a core event | 5 | octabam-analog-bassdrum (upstream): ab_src_commit, ab_src_commit2; octabam-blackpyramids-303 (upstream): ot_src_commit, ot_src_commit2; octabam-cxosc (upstream): fm_src_commit, po_machlist, fm_src_commit2; octabam-octa303 (upstream): o3_src_commit, o3_src_commit2; octabam-synth: po_machlist |
| `octatrack@1.40C:0x40061e78` | some sites move onto the core, the rest need an event | 5 | octabam-batch-bugfixes (upstream): ob_batch_bugfixes_part_change_carryover_fix; octabam-blackpyramids-303 (upstream): ot_rec_midi_hook; octabam-cxosc (upstream): po_mrec, po_mtrig; octabam-midi-scenes: xf1; octabam-midi-scenes (upstream): scn_xf1, scn_evt; octabam-store (upstream): store_ctl |
| `octatrack@1.40C:0x40079816` | a core event | 5 | octabam-analog-bassdrum (upstream): ab_main_commit; octabam-blackpyramids-303 (upstream): ot_main_commit; octabam-cxosc (upstream): fm_main_commit, ml_cur_bound; octabam-octa303 (upstream): o3_main_commit; octabam-synth: po_machwin |
| `octatrack@1.40C:0x40002318` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_validate; octabam-blackpyramids-303 (upstream): ot_validate; octabam-cxosc (upstream): fm_validate; octabam-octa303 (upstream): o3_validate |
| `octatrack@1.40C:0x40006da6` | a core event | 4 | octabam-buffer-saver (upstream): arm_mark; octabam-recorder-loop-fix (upstream): rlf_spacing; octabam-recorder-spacing: ob_recorder_spacing_spacing_cave; octabam-rlen-plen: ob_rlen_plen_rlen_plen_cave; octabam-rlen-plen (upstream): plen_cave |
| `octatrack@1.40C:0x4000aad0` | a core event (upstream only) | 4 | octabam-boot-trace (upstream): tr_frame; octabam-cfmeter (upstream): m_hc1; octabam-load-meter (upstream): lm_in; octabam-rec-threshold (upstream): frame_check |
| `octatrack@1.40C:0x4000b2c8` | a core event | 4 | octabam-blackpyramids-303 (upstream): ot_live_rel_hook; octabam-cxosc (upstream): po_stop, po_rel; octabam-plocks-p2 (upstream): plk_note, plk_p2r; octabam-synth: po_stop, po_rel |
| `octatrack@1.40C:0x4000c634` | a core event | 4 | octabam-cxosc (upstream): po_retrig; octabam-octa303 (upstream): o3_retrig; octabam-plocks-p2 (upstream): plk_apply; octabam-synth: po_retrig |
| `octatrack@1.40C:0x4000d310` | a core event (upstream only) | 4 | octabam-cxosc (upstream): po_fmsource; octabam-euclid (upstream): eu_frame_hook; octabam-load-meter (upstream): lm_scrub; octabam-mute-modes (upstream): dt_trig |
| `octatrack@1.40C:0x4000f834` | a core event | 4 | octabam-flex-seekbind: ob_flex_seekbind_seek_bind_cave; octabam-flex-seekbind-ctr: ob_flex_seekbind_ctr_seek_bind_counter_cave; octabam-mute-modes (upstream): mt_rebind; octabam-recorder-loop-fix (upstream): rlf_seekbind_ctr, rlf_seekbind |
| `octatrack@1.40C:0x4002b8f8` | a core event | 4 | octabam-cxosc (upstream): fm_track_undo; octabam-midi-scenes: apply_bridge; octabam-plocks-p2 (upstream): plk_memcpy; octabam-varispeed (upstream): vh_trkpaste |
| `octatrack@1.40C:0x40031e62` | a core event | 4 | octabam-analog-bassdrum (upstream): ab_resolve_pb; octabam-blackpyramids-303 (upstream): ot_resolve_pb; octabam-octa303 (upstream): o3_page; octabam-synth: po_lfopage |
| `octatrack@1.40C:0x400334d8` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_machine_name; octabam-blackpyramids-303 (upstream): ot_machine_name; octabam-cxosc (upstream): fm_machine_name; octabam-octa303 (upstream): o3_machine_name |
| `octatrack@1.40C:0x4003a52e` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_setup_edit6; octabam-blackpyramids-303 (upstream): ot_setup_edit6; octabam-cxosc (upstream): fm_setup_edit6; octabam-octa303 (upstream): o3_setup_edit6 |
| `octatrack@1.40C:0x4003a9dc` | a core event | 4 | octabam-cxosc (upstream): po_ampedit, po_amplane; octabam-mode-defaults (upstream): fx2_hook, fx1_hook; octabam-scenes-p2: fx2_edit_hook, fx1_edit_hook; octabam-synth: po_ampedit, po_amplane |
| `octatrack@1.40C:0x4003c928` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_src_names, ab_setup_row; octabam-blackpyramids-303 (upstream): ot_src_names, ot_setup_row; octabam-cxosc (upstream): fm_src_names, ml_name_bound, fm_setup_row; octabam-octa303 (upstream): o3_src_names, o3_setup_row |
| `octatrack@1.40C:0x4003cd98` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_setup_draw6; octabam-blackpyramids-303 (upstream): ot_setup_draw6, ot_setup_value_hook; octabam-cxosc (upstream): fm_setup_draw6; octabam-octa303 (upstream): o3_setup_draw6 |
| `octatrack@1.40C:0x4003d718` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_name_a; octabam-blackpyramids-303 (upstream): ot_name_a; octabam-cxosc (upstream): fm_name_a; octabam-octa303 (upstream): o3_name_a |
| `octatrack@1.40C:0x4004c146` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_name_b; octabam-blackpyramids-303 (upstream): ot_slot_rec_ui, ot_name_b; octabam-kits (upstream): kits_status; octabam-octa303 (upstream): o3_name_b |
| `octatrack@1.40C:0x4005221e` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_tick_hook; octabam-blackpyramids-303 (upstream): ot_tick_hook; octabam-cxosc (upstream): fm_tick; octabam-octa303 (upstream): o3_tick |
| `octatrack@1.40C:0x400585dc` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_setup_open; octabam-blackpyramids-303 (upstream): ot_setup_open; octabam-cxosc (upstream): fm_setup_open, ml_src_count; octabam-octa303 (upstream): o3_setup_open |
| `octatrack@1.40C:0x40078678` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_chooser_row; octabam-blackpyramids-303 (upstream): ot_chooser_row; octabam-cxosc (upstream): ml_draw_bound, fm_chooser_row, ml_hi_bound; octabam-octa303 (upstream): o3_chooser_row |
| `octatrack@1.40C:0x40078886` | a core event (upstream only) | 4 | octabam-analog-bassdrum (upstream): ab_chooser_open; octabam-blackpyramids-303 (upstream): ot_chooser_open, ot_chooser_level; octabam-cxosc (upstream): fm_chooser_open; octabam-octa303 (upstream): o3_chooser_open |
| `octatrack@1.40C:0x4009f794` | a core event (upstream only) | 4 | octabam-midi-loopback (upstream): lb_tick; octabam-midi-scales (upstream): ms_output; octabam-midi-scenes (upstream): scn_ccnote, ?; octabam-sequencer-events (upstream): seq_sent_a, seq_sent_b |
| `octatrack@1.40C:0x40000412` | a core event (upstream only) | 3 | octabam-boot-trace (upstream): tr_clock, tr_reprog_branch, tr_dsp_done; octabam-midi-part-state (upstream): mp_resident_activate; octabam-remix-switch (upstream): osw_chain |
| `octatrack@1.40C:0x40004c72` | a core event (upstream only) | 3 | octabam-bbd (upstream): bbd_kill; octabam-mute-modes (upstream): trigflag; octabam-tempo-sync (upstream): cave |
| `octatrack@1.40C:0x40005638` | a core event (upstream only) | 3 | octabam-brain (upstream): brain_partinit; octabam-midi-part-state (upstream): mp_init_entry; octabam-rig-hosts (upstream): fx_ids, fx2_page1, fx2_page2 |
| `octatrack@1.40C:0x4000685c` | a core event | 3 | octabam-cxosc (upstream): po_kill; octabam-mute-modes (upstream): fresh_bind, mt_trig; octabam-synth: po_kill |
| `octatrack@1.40C:0x4000e452` | a core event | 3 | octabam-cxosc (upstream): po_mgate; octabam-midi-loopback (upstream): lb_rx_map; octabam-synth: po_mgate |
| `octatrack@1.40C:0x4000e746` | a core event | 3 | octabam-blackpyramids-303 (upstream): ot_live_midi_hook; octabam-cxosc (upstream): po_mon; octabam-synth: po_mon |
| `octatrack@1.40C:0x4003685a` | a core event | 3 | octabam-cxosc (upstream): po_ampdraw1, po_ampdraw2, po_ampdraw3; octabam-synth: po_ampdraw1, po_ampdraw2, po_ampdraw3; octabam-varispeed (upstream): qdial2 |
| `octatrack@1.40C:0x40037840` | a core event | 3 | octabam-plocks-p2 (upstream): plk_widget2; octabam-scenes-p2: dial2_hook; octabam-varispeed (upstream): qdial3 |
| `octatrack@1.40C:0x40037bdc` | a core event | 3 | octabam-plocks-p2 (upstream): plk_widget1; octabam-scenes-p2: dial1_hook; octabam-varispeed (upstream): qdial4 |
| `octatrack@1.40C:0x4004fbde` | a core event | 3 | octabam-blackpyramids-303 (upstream): ot_chrom_limit_hook, ot_live_chrom_hook, ot_rec_chrom_hook; octabam-octa303 (upstream): o3_chrom; octabam-quantizer: qz_leg0, qz_leg1, qz_chrom +4 |
| `octatrack@1.40C:0x40085176` | a core event (upstream only) | 3 | octabam-brain (upstream): brain_on_newproj, brain_on_load, brain_on_store; octabam-buffer-saver (upstream): load_clear; octabam-reload-from-project (upstream): rl_job |
| `octatrack@1.40C:0x4009d1e8` | a core event (upstream only) | 3 | octabam-blackpyramids-303 (upstream): ot_step_hook, ot_note_hook, ot_oneshot_hook; octabam-plocks-p2 (upstream): plk_fill_slide, plk_fill_plain; octabam-rec-trig-mute (upstream): rtm_gate |
| `octatrack@1.40C:0x4000406a` | a core event | 2 | octabam-repitch: rate_gate, pitch_gate, rate_hook; octabam-varispeed (upstream): vem_frame |
| `octatrack@1.40C:0x40005558` | some sites move onto the core, the rest need an event (upstream only) | 2 | octabam-brain (upstream): brain_on_midi; octabam-midi-loopback (upstream): lb_receive |
| `octatrack@1.40C:0x4000853e` | a core event | 2 | octabam-recorder-hold: ob_recorder_hold_hold_cave_crossfade_0x48, ob_recorder_hold_hold_cave_crossfade_0x4c, ob_recorder_hold_hold_cave_copy; octabam-recorder-loop-fix (upstream): rlf_hold_xfade_a, rlf_hold_xfade_b, rlf_hold_xguard +2 |
| `octatrack@1.40C:0x4000de10` | a core event | 2 | octabam-cxosc (upstream): po_mogate; octabam-synth: po_mogate |
| `octatrack@1.40C:0x4000dfd4` | a core event | 2 | octabam-cxosc (upstream): po_moff; octabam-synth: po_moff |
| `octatrack@1.40C:0x40014cfe` | a core event (upstream only) | 2 | octabam-ata-write-guard (upstream): ata_write_return; octabam-stems (upstream): stems_ata_first |
| `octatrack@1.40C:0x40022686` | a core event | 2 | octabam-cxosc (upstream): po_loadsel; octabam-synth: po_loadsel |
| `octatrack@1.40C:0x40025aa2` | a core event | 2 | octabam-midi-scenes: bank_inv; octabam-quantizer: qz_defaults |
| `octatrack@1.40C:0x40026eb0` | a core event (upstream only) | 2 | octabam-kits (upstream): kits_pcopy; octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40026ef0` | a core event (upstream only) | 2 | octabam-kits (upstream): kits_psnap; octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40029af8` | a core event | 2 | octabam-midi-scenes: apply_bridge; octabam-varispeed (upstream): vh_partpaste |
| `octatrack@1.40C:0x4002b59a` | a core event | 2 | octabam-midi-scenes: apply_bridge; octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x4002b9b0` | a core event (upstream only) | 2 | octabam-kits (upstream): kits_pstore_ptn; octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x4002fb10` | a core event | 2 | octabam-rec-threshold (upstream): trig_screen, draw_gate; octabam-rlen-plen: ob_rlen_plen_rlen_plen_cave; octabam-rlen-plen (upstream): plen_screen |
| `octatrack@1.40C:0x40044968` | a core event | 2 | octabam-blackpyramids-303 (upstream): ot_chrom_img_hook; octabam-quantizer: qz_octdraw, qz_octnum; octabam-quantizer (upstream): qz_octnum |
| `octatrack@1.40C:0x40045918` | a core event | 2 | octabam-blackpyramids-303 (upstream): ot_oct_hook; octabam-quantizer: qz_octkey |
| `octatrack@1.40C:0x4004bee2` | a core event (upstream only) | 2 | octabam-rec-threshold (upstream): blink_gate; octabam-rec-trig-mute (upstream): rtm_glyph, rtm_draw |
| `octatrack@1.40C:0x4004e348` | a core event | 2 | octabam-brain (upstream): brain_klabel_hook; octabam-midi-scenes: dial; octabam-midi-scenes (upstream): scn_dial |
| `octatrack@1.40C:0x40050254` | a core event | 2 | octabam-blackpyramids-303 (upstream): ot_chrom_key_hook; octabam-quantizer: qz_keyidx |
| `octatrack@1.40C:0x40050e60` | a core event | 2 | octabam-blackpyramids-303 (upstream): ot_plock_hook; octabam-quantizer: qz_plock |
| `octatrack@1.40C:0x40051fce` | a core event | 2 | octabam-cxosc (upstream): po_octave; octabam-synth: po_octave |
| `octatrack@1.40C:0x40055170` | a core event | 2 | octabam-midi-scenes: write_mix; octabam-midi-scenes (upstream): scn_pwrite; octabam-quantizer: qz_knob |
| `octatrack@1.40C:0x40059d56` | a core event | 2 | octabam-cxosc (upstream): po_ampstage; octabam-synth: po_ampstage |
| `octatrack@1.40C:0x40059ef0` | a core event | 2 | octabam-tempo-bus (upstream): tb_open; octabam-tuner: tu_tempo |
| `octatrack@1.40C:0x4005e05a` | a core event | 2 | octabam-kits (upstream): kits_freload; octabam-midi-scenes: reload |
| `octatrack@1.40C:0x400622aa` | a core event | 2 | octabam-midi-scenes: bank_sw, after_proj; octabam-synth: po_mrec, po_mtrig |
| `octatrack@1.40C:0x4006e71c` | a core event | 2 | octabam-repitch: attr_label; octabam-varispeed (upstream): attr_label |
| `octatrack@1.40C:0x4006ee56` | a core event | 2 | octabam-repitch: attr_up; octabam-varispeed (upstream): attr_up, attr_tail |
| `octatrack@1.40C:0x4006ef7c` | a core event | 2 | octabam-repitch: attr_down; octabam-varispeed (upstream): attr_down, attr_tail |
| `octatrack@1.40C:0x400791e4` | a core event (upstream only) | 2 | octabam-analog-bassdrum (upstream): ab_pool_open; octabam-cxosc (upstream): ml_win_count |
| `octatrack@1.40C:0x400795ba` | a core event | 2 | octabam-cxosc (upstream): po_assign; octabam-synth: po_assign |
| `octatrack@1.40C:0x4009b6f2` | a core event (upstream only) | 2 | octabam-batch-bugfixes (upstream): ob_batch_bugfixes_midi_plays_free_fix; octabam-plocks-p2 (upstream): plk_s2p_a |
| `octatrack@1.40C:0x4009c038` | a core event (upstream only) | 2 | octabam-plocks-p2 (upstream): plk_s2p_b; octabam-sequencer-events (upstream): seq_start, seq_resume |
| `octatrack@1.40C:0x400a06d6` | a core event | 2 | octabam-direct-jump: ob_direct_jump_direct_jump_cave; octabam-kits (upstream): kits_sched |
| `octatrack@1.40C:0x400a44e2` | a core event | 2 | octabam-direct-jump: ob_direct_jump_direct_jump_cave; octabam-midi-scenes (upstream): scn_switch |
| `octatrack@1.40C:0x40000554` | a core event (upstream only) | 1 | octabam-brain (upstream): brain_sched |
| `octatrack@1.40C:0x40001b18` | a core event (upstream only) | 1 | octabam-boot-trace (upstream): tr_records, tr_tx1, tr_echo +3 |
| `octatrack@1.40C:0x40001d4c` | a core event (upstream only) | 1 | octabam-boot-trace (upstream): tr_bootup |
| `octatrack@1.40C:0x40001e50` | a core event (upstream only) | 1 | octabam-boot-trace (upstream): tr_dsp |
| `octatrack@1.40C:0x40002f44` | a core event (upstream only) | 1 | octabam-tapeecho (upstream): te_cpu_reset |
| `octatrack@1.40C:0x4000361a` | a core event (upstream only) | 1 | octabam-tapeecho (upstream): te_cpu_hook |
| `octatrack@1.40C:0x40003ca4` | a core event | 1 | octabam-synth: po_lfo3 |
| `octatrack@1.40C:0x40003d1c` | a core event (upstream only) | 1 | octabam-sidechain-compressor (upstream): sc_norm |
| `octatrack@1.40C:0x400047f0` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_idle_hook |
| `octatrack@1.40C:0x40004dc6` | a core event (upstream only) | 1 | octabam-mute-modes (upstream): pre |
| `octatrack@1.40C:0x40005030` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_live_req_hook |
| `octatrack@1.40C:0x40007d96` | a core event | 1 | octabam-repitch: tstr_resolve |
| `octatrack@1.40C:0x40009094` | a core event (upstream only) | 1 | octabam-varispeed (upstream): rp_apply1 |
| `octatrack@1.40C:0x40009e00` | a core event (upstream only) | 1 | octabam-varispeed (upstream): rp_apply2 |
| `octatrack@1.40C:0x4000a3fe` | a core event (upstream only) | 1 | octabam-midi-loopback (upstream): lb_rx_map |
| `octatrack@1.40C:0x4000a53a` | a core event (upstream only) | 1 | octabam-midi-loopback (upstream): lb_rx_map |
| `octatrack@1.40C:0x4000a654` | a core event (upstream only) | 1 | octabam-midi-loopback (upstream): lb_rx_map |
| `octatrack@1.40C:0x4000a90c` | a core event (upstream only) | 1 | octabam-cfmeter (upstream): m_hc2 |
| `octatrack@1.40C:0x4000cae8` | a core event (upstream only) | 1 | octabam-sidechain-compressor (upstream): sc_norm |
| `octatrack@1.40C:0x4000cf40` | a core event | 1 | octabam-scenes-p2: frame_hook |
| `octatrack@1.40C:0x4000d03e` | a core event | 1 | octabam-synth: po_lfo3b |
| `octatrack@1.40C:0x4000da58` | a core event (upstream only) | 1 | octabam-midi-loopback (upstream): lb_rx_map |
| `octatrack@1.40C:0x4000e7bc` | a core event (upstream only) | 1 | octabam-midi-loopback (upstream): lb_rx_map |
| `octatrack@1.40C:0x4000f210` | a core event (upstream only) | 1 | octabam-rec-trig-mute (upstream): rtm_cc |
| `octatrack@1.40C:0x4000f9b4` | a core event (upstream only) | 1 | octabam-boot-trace (upstream): tr_reprog |
| `octatrack@1.40C:0x4000faf0` | a core event (upstream only) | 1 | octabam-store (upstream): store_tocs1 |
| `octatrack@1.40C:0x40010212` | a core event | 1 | octabam-quantizer: qz_boot |
| `octatrack@1.40C:0x4001513c` | a core event (upstream only) | 1 | octabam-brain (upstream): brain_ata_in, brain_ata_out |
| `octatrack@1.40C:0x4001f4dc` | a core event (upstream only) | 1 | octabam-boot-trace (upstream): tr_handshake |
| `octatrack@1.40C:0x40020898` | a core event (upstream only) | 1 | octabam-midi-part-state (upstream): mp_copy_entry |
| `octatrack@1.40C:0x40023c62` | a core event (upstream only) | 1 | octabam-reload-from-project (upstream): rl_done |
| `octatrack@1.40C:0x4002573e` | a core event (upstream only) | 1 | octabam-remix-switch (upstream): osw_bootfiles |
| `octatrack@1.40C:0x4002574c` | a core event (upstream only) | 1 | octabam-remix-switch (upstream): osw_bootproject |
| `octatrack@1.40C:0x40025808` | a core event (upstream only) | 1 | octabam-store (upstream): store_fromcs1 |
| `octatrack@1.40C:0x40025b40` | a core event | 1 | octabam-scenes-p2: scene_write_hook |
| `octatrack@1.40C:0x400267e8` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40026884` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x400274cc` | a core event | 1 | octabam-scenes-p2: scene_copy_hook |
| `octatrack@1.40C:0x400275a0` | a core event | 1 | octabam-scenes-p2: scene_undo_hook |
| `octatrack@1.40C:0x40027764` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40027834` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40027e4c` | a core event (upstream only) | 1 | octabam-varispeed (upstream): vh_srcpaste |
| `octatrack@1.40C:0x40027e98` | a core event (upstream only) | 1 | octabam-cxosc (upstream): fm_page_store |
| `octatrack@1.40C:0x4002924c` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40029280` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40029316` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40029352` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40029652` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x4002978e` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40029980` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x40029b54` | a core event (upstream only) | 1 | octabam-cxosc (upstream): fm_part_undo |
| `octatrack@1.40C:0x40029b8a` | a core event (upstream only) | 1 | octabam-cxosc (upstream): fm_part_paste |
| `octatrack@1.40C:0x4002b370` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x4002b42e` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x4002b99e` | a core event (upstream only) | 1 | octabam-cxosc (upstream): fm_track_paste |
| `octatrack@1.40C:0x4002bf38` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_tcopy |
| `octatrack@1.40C:0x4002cb52` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_tpaste |
| `octatrack@1.40C:0x4002dc9c` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_savekey |
| `octatrack@1.40C:0x4002dcd4` | a core event | 1 | octabam-midi-scenes: save |
| `octatrack@1.40C:0x4002dd12` | a core event | 1 | octabam-midi-scenes: save |
| `octatrack@1.40C:0x4002dd56` | a core event | 1 | octabam-midi-scenes: reload |
| `octatrack@1.40C:0x4002e7b8` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_partkey |
| `octatrack@1.40C:0x4002e828` | a core event | 1 | octabam-midi-scenes: clr_pt |
| `octatrack@1.40C:0x4002ef60` | a core event (upstream only) | 1 | octabam-rec-threshold (upstream): edit_gate, trig_init |
| `octatrack@1.40C:0x40031f44` | a core event | 1 | octabam-midi-scenes: pad; octabam-midi-scenes (upstream): scn_pad |
| `octatrack@1.40C:0x400343bc` | a core event | 1 | octabam-midi-scenes: ?, taddi, paddi; octabam-midi-scenes (upstream): scn_taddi, scn_paddi |
| `octatrack@1.40C:0x40034764` | a core event | 1 | octabam-midi-scenes: taddi; octabam-midi-scenes (upstream): scn_taddi |
| `octatrack@1.40C:0x40034950` | a core event | 1 | octabam-midi-scenes: taddi; octabam-midi-scenes (upstream): scn_taddi |
| `octatrack@1.40C:0x40036698` | a core event (upstream only) | 1 | octabam-varispeed (upstream): qdial1 |
| `octatrack@1.40C:0x40038a5c` | a core event (upstream only) | 1 | octabam-erase-empty-trigless-locks (upstream): ob_erase_empty_trigless_locks_erase_empty_trigless_locks_cave |
| `octatrack@1.40C:0x40038c30` | a core event | 1 | octabam-scenes-p2: scene_clear_hook |
| `octatrack@1.40C:0x40039df4` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_clrtrack |
| `octatrack@1.40C:0x4003b33c` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_slic_a |
| `octatrack@1.40C:0x4003bff0` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_pmtr_pb |
| `octatrack@1.40C:0x4003d552` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_slot_rec |
| `octatrack@1.40C:0x4003de14` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_memcpy |
| `octatrack@1.40C:0x4003f3a2` | a core event | 1 | octabam-midi-scenes: morph; octabam-midi-scenes (upstream): scn_morph |
| `octatrack@1.40C:0x4004012c` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_glyph |
| `octatrack@1.40C:0x40040250` | a core event (upstream only) | 1 | octabam-reload-from-project (upstream): rl3_bank_trk |
| `octatrack@1.40C:0x40040e14` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_clrlocks |
| `octatrack@1.40C:0x40043418` | a core event (upstream only) | 1 | octabam-direct-jump-kyoti (upstream): dj_ptnrel |
| `octatrack@1.40C:0x400434ca` | a core event | 1 | octabam-midi-scenes: press; octabam-midi-scenes (upstream): scn_press |
| `octatrack@1.40C:0x40044006` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_led_hook |
| `octatrack@1.40C:0x40044dbe` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_slic_b |
| `octatrack@1.40C:0x40048774` | a core event (upstream only) | 1 | octabam-stems (upstream): stems_rec_key |
| `octatrack@1.40C:0x4004883a` | a core event (upstream only) | 1 | octabam-quantize-live-rec-toggle (upstream): ob_quantize_live_rec_toggle_quantize_live_rec_toggle_cave |
| `octatrack@1.40C:0x4004a8fc` | a core event | 1 | octabam-midi-scenes: apply_bridge |
| `octatrack@1.40C:0x4004a9c4` | a core event (upstream only) | 1 | octabam-store (upstream): store_partsaved |
| `octatrack@1.40C:0x4004a9d0` | a core event (upstream only) | 1 | octabam-store (upstream): store_partclear |
| `octatrack@1.40C:0x4004aab4` | a core event (upstream only) | 1 | octabam-varispeed (upstream): rp_reload |
| `octatrack@1.40C:0x4004b528` | a core event (upstream only) | 1 | octabam-tempo-bus (upstream): tb_draw |
| `octatrack@1.40C:0x4004f8dc` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_slices_hook |
| `octatrack@1.40C:0x4004fa54` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_slots_hook |
| `octatrack@1.40C:0x4004fde4` | a core event (upstream only) | 1 | octabam-blackpyramids-303 (upstream): ot_page_key_hook, ot_slic_c |
| `octatrack@1.40C:0x4004ffc4` | a core event (upstream only) | 1 | octabam-euclid-fill (upstream): ef_page |
| `octatrack@1.40C:0x400503c4` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_fright |
| `octatrack@1.40C:0x400508e4` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_edit |
| `octatrack@1.40C:0x40052a10` | a core event | 1 | octabam-midi-scenes: scene_done; octabam-midi-scenes (upstream): scn_done |
| `octatrack@1.40C:0x40052ae0` | a core event | 1 | octabam-midi-scenes: scene_done; octabam-midi-scenes (upstream): scn_done |
| `octatrack@1.40C:0x40052ece` | a core event | 1 | octabam-midi-scenes: hold_b; octabam-midi-scenes (upstream): scn_hold_b |
| `octatrack@1.40C:0x400534ce` | a core event | 1 | octabam-midi-scenes: hold_a; octabam-midi-scenes (upstream): scn_hold_a |
| `octatrack@1.40C:0x40053a9e` | a core event | 1 | octabam-midi-scenes: hook_a; octabam-midi-scenes (upstream): scn_enc_a |
| `octatrack@1.40C:0x40054392` | a core event | 1 | octabam-midi-scenes: hook_b; octabam-midi-scenes (upstream): scn_enc_b |
| `octatrack@1.40C:0x40054cb6` | a core event | 1 | octabam-midi-scenes: release |
| `octatrack@1.40C:0x4005595c` | a core event | 1 | octabam-cc-feedback: cf_tick |
| `octatrack@1.40C:0x40056930` | a core event (upstream only) | 1 | octabam-tempo-bus (upstream): tb_close |
| `octatrack@1.40C:0x40056b2c` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_ptrig |
| `octatrack@1.40C:0x40056c72` | a core event | 1 | octabam-tuner: tu_tick |
| `octatrack@1.40C:0x40058958` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_page_draw |
| `octatrack@1.40C:0x40058a64` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_mkisave |
| `octatrack@1.40C:0x4005e3d8` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_funcyes |
| `octatrack@1.40C:0x4006036e` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_place |
| `octatrack@1.40C:0x40060b38` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_page_draw |
| `octatrack@1.40C:0x40060db0` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_lpaste |
| `octatrack@1.40C:0x40060eb4` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_lclear |
| `octatrack@1.40C:0x40060f34` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_lcopy |
| `octatrack@1.40C:0x40061778` | a core event (upstream only) | 1 | octabam-quantize-live-rec-toggle (upstream): ob_quantize_live_rec_toggle_quantize_live_rec_toggle_cave |
| `octatrack@1.40C:0x40061c94` | a core event (upstream only) | 1 | octabam-boot-trace (upstream): tr_ui |
| `octatrack@1.40C:0x40062c32` | a core event | 1 | octabam-midi-scenes: xf2; octabam-midi-scenes (upstream): scn_xf2 |
| `octatrack@1.40C:0x40062e3c` | a core event | 1 | octabam-midi-scenes: pst_sc; octabam-midi-scenes (upstream): scn_paste |
| `octatrack@1.40C:0x40062f24` | a core event | 1 | octabam-midi-scenes: clr_sc; octabam-midi-scenes (upstream): scn_clear |
| `octatrack@1.40C:0x40062fbe` | a core event | 1 | octabam-midi-scenes: cpy_sc; octabam-midi-scenes (upstream): scn_copy |
| `octatrack@1.40C:0x40063618` | a core event (upstream only) | 1 | octabam-buffer-saver (upstream): new_gate |
| `octatrack@1.40C:0x40063890` | a core event (upstream only) | 1 | octabam-buffer-saver (upstream): save_gate |
| `octatrack@1.40C:0x40064c32` | a core event (upstream only) | 1 | octabam-remix-switch (upstream): osw_menu |
| `octatrack@1.40C:0x40065bca` | a core event | 1 | octabam-quantizer: qz_draw |
| `octatrack@1.40C:0x4006d784` | a core event (upstream only) | 1 | octabam-analog-bassdrum (upstream): ab_list_draw |
| `octatrack@1.40C:0x40077b5c` | a core event (upstream only) | 1 | octabam-analog-bassdrum (upstream): ab_pool_title |
| `octatrack@1.40C:0x40078c22` | a core event (upstream only) | 1 | octabam-cxosc (upstream): ml_clamp1 |
| `octatrack@1.40C:0x40078cb0` | a core event (upstream only) | 1 | octabam-cxosc (upstream): ml_clamp2 |
| `octatrack@1.40C:0x40078d2e` | a core event (upstream only) | 1 | octabam-cxosc (upstream): ml_clamp3 |
| `octatrack@1.40C:0x40078dee` | a core event (upstream only) | 1 | octabam-cxosc (upstream): ml_clamp4 |
| `octatrack@1.40C:0x40078e7c` | a core event (upstream only) | 1 | octabam-cxosc (upstream): ml_clamp5 |
| `octatrack@1.40C:0x4007a3e2` | a core event (upstream only) | 1 | octabam-midi-scales (upstream): ms_encoder |
| `octatrack@1.40C:0x4007af42` | a core event (upstream only) | 1 | octabam-reload-from-project (upstream): rl3_bank_show |
| `octatrack@1.40C:0x4007b3e0` | a core event (upstream only) | 1 | octabam-reload-from-project (upstream): rl3_bank_rel |
| `octatrack@1.40C:0x400835e2` | a core event (upstream only) | 1 | octabam-rec-threshold (upstream): trig_gate, idle_gate |
| `octatrack@1.40C:0x400837b6` | a core event (upstream only) | 1 | octabam-rec-threshold (upstream): trig_gate_trk, idle_gate_trk |
| `octatrack@1.40C:0x40083dc4` | a core event (upstream only) | 1 | octabam-reload-from-project (upstream): rl3_ptn_trk |
| `octatrack@1.40C:0x4008485e` | a core event (upstream only) | 1 | octabam-brain (upstream): brain_on_job |
| `octatrack@1.40C:0x40084d4a` | a core event (upstream only) | 1 | octabam-brain (upstream): brain_on_bankload |
| `octatrack@1.40C:0x400866cc` | a core event | 1 | octabam-quantizer: qz_ld_entry, qz_ld_line |
| `octatrack@1.40C:0x40087d44` | a core event | 1 | octabam-midi-scenes: bank_pub |
| `octatrack@1.40C:0x400888aa` | a core event | 1 | octabam-quantizer: qz_wr |
| `octatrack@1.40C:0x4008ee42` | a core event (upstream only) | 1 | octabam-store (upstream): store_fcopy |
| `octatrack@1.40C:0x4008ee74` | a core event (upstream only) | 1 | octabam-store (upstream): store_pstore, store_d5_ef9a, store_d5_f02e |
| `octatrack@1.40C:0x4008f14e` | a core event (upstream only) | 1 | octabam-store (upstream): store_fcopy |
| `octatrack@1.40C:0x4008f180` | a core event (upstream only) | 1 | octabam-store (upstream): store_preload, store_d5_f2a6, store_d5_f33a |
| `octatrack@1.40C:0x40090504` | a core event (upstream only) | 1 | octabam-store (upstream): store_loadall |
| `octatrack@1.40C:0x400905d4` | a core event (upstream only) | 1 | octabam-store (upstream): store_loadmask |
| `octatrack@1.40C:0x400909d8` | a core event (upstream only) | 1 | octabam-store (upstream): store_newproj |
| `octatrack@1.40C:0x400917c8` | a core event (upstream only) | 1 | octabam-store (upstream): store_bankw, store_bankw_mid |
| `octatrack@1.40C:0x40096d22` | a core event (upstream only) | 1 | octabam-cxosc (upstream): fm_preview |
| `octatrack@1.40C:0x4009a464` | a core event (upstream only) | 1 | octabam-batch-bugfixes (upstream): ob_batch_bugfixes_empty_pattern_led_fix |
| `octatrack@1.40C:0x4009b220` | a core event (upstream only) | 1 | octabam-plocks-p2 (upstream): plk_reset |
| `octatrack@1.40C:0x4009c634` | a core event (upstream only) | 1 | octabam-kits (upstream): kits_chain |
| `octatrack@1.40C:0x4009d1de` | a core event | 1 | octabam-midi-scenes: plock |
| `octatrack@1.40C:0x4009f26e` | a core event (upstream only) | 1 | octabam-midi-loopback (upstream): lb_control_tail |
| `octatrack@1.40C:0x4009f288` | a core event (upstream only) | 1 | octabam-midi-loopback (upstream): lb_panic |
| `octatrack@1.40C:0x400a169a` | a core event (upstream only) | 1 | octabam-midi-scenes (upstream): scn_qcommit, scn_snap1, scn_snap2 |
| `octatrack@1.40C:0x400a1f72` | a core event (upstream only) | 1 | octabam-direct-jump-kyoti (upstream): dj_land, dj_nofa |
| `octatrack@1.40C:0x400a3c2a` | a core event (upstream only) | 1 | octabam-midi-scenes (upstream): scn_qrec |
| `octatrack@1.40C:0x400a4ba0` | a core event (upstream only) | 1 | octabam-midi-scenes (upstream): scn_step |

<!-- /bounties -->
