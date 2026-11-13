# elekloader

A mod loader for Elektron firmware. You pick mods and supply the stock OS
file Elektron publishes for your device. elekloader builds a custom OS
file on your own machine, and you flash it the way you flash any OS update.

- **Nothing from Elektron is distributed.** A mod (`.elemod`) carries only
  its author's bytes, the short instruction fragments needed to hook the OS,
  and hashes of the stock bytes it expects. Every build starts from your own
  stock file, recognised by its hash.
- **The bootloader is never touched,** so the stock OS file always recovers
  the device. Mods change the main OS (and, on the Digitone II, a SHARC mod
  the DSP's section, inside fixed limits). Each output file is re-read and
  verified before it is written.
- **Mods are checked against each other before anything is built.** Two
  mods may not touch the same bytes, claim the same memory or the same
  named resource, or leave a requirement unmet.

## Devices

| Device | OS | Status |
|---|---|---|
| Digitakt (mk1) | 1.53, 1.54 | supported |
| Digitone (mk1) and Digitone Keys | 1.43, 1.44 | supported |
| Octatrack (MKI and MKII) | 1.40C | supported (its core runs on an MKII, not yet tried on an MKI) |
| Model:Cycles, Model:Samples | 1.13 | supported |
| Analog Rytm MKII | 1.74 | supported |
| Digitakt II | 1.17 | experimental |
| Digitone II | 1.12 | experimental, with SHARC (DSP) mods and [machines](docs/MACHINES-DN2.md) |
| Analog Four MKII | 1.56 | experimental: a proof-of-concept core |

Every device has a **core** mod, attached to each
[release](https://github.com/elekloader/elekloader/releases/latest), that
other mods build on ([docs/CORES.md](docs/CORES.md)). What was checked on
each device, on a unit and in the emulator, and what a new device needs:
[docs/DEVICES.md](docs/DEVICES.md).

## Get started

- **In your browser, nothing to install:** the
  **[Mod Shop](https://elekloader.github.io/shop/)**. Pick mods for your
  device, drop in your stock OS file, build, and download the result.
  Already have `.elemod` files of your own? The
  [builder](https://elekloader.github.io/elekloader/) takes them. Your
  files are never uploaded.
- **On Windows:** `elekloader-<version>-windows.exe` from
  [Releases](https://github.com/elekloader/elekloader/releases/latest), with
  the cores built in.
- **From source, on any system:** Python 3.9 or newer and nothing else:
  `python -m elekloader`.

Each way needs the **stock OS file** for your device, exactly as Elektron
publishes it; the `.zip` from Elektron's site works as it is.
[docs/INSTALL.md](docs/INSTALL.md) says which file each device needs.

Then tick your mods and build. Send the `.syx` to the unit with Elektron
Transfer, as for any OS update; the Octatrack takes its `.bin` from the
card ([docs/BUILDING.md](docs/BUILDING.md)).

**If a custom OS will not start,** send the stock OS file from the startup
menu: hold **FUNC** while powering on, press **TRIG 4** for OS UPGRADE
(**TRIG 3**, MIDI UPGRADE, on the Octatrack) and send the stock `.syx`.
Most devices need 5-pin DIN MIDI for this, not USB:
[Recovery](docs/BUILDING.md#recovery) has each device's steps.

## Mods

Mods are `.elemod` files: a whole build in one file, or linkable mods that
combine on top of your device's core. Cores add things of their own. On the
Digitone, for example, holding a track key opens the Mod Menu, where you
pick what your mods add:

![The Digitone's Mod Menu: four tiles, each an icon over a label with its trig key's number, TABLES selected, and a scroll bar](docs/img/dn-modmenu-grid.png)

- **Find mods** in the [Mod Shop](https://elekloader.github.io/shop/), or
  make a boot screen from your own picture with the
  [Splash screen maker](docs/SPLASH.md).
- **Write your own:** [docs/ADAPTING.md](docs/ADAPTING.md) takes you from
  the first build to done. For one C machine on the Digitakt, the Digitone
  and the Digitone II, there is the [shared machine SDK](docs/SDK.md)
  (alpha).

## Submitting a mod to the shop

**Mods are submitted in [elekloader/shop](https://github.com/elekloader/shop),
not in this repository.** Put your `.elemod` files in a release on your own
GitHub repository and fill in the
**[Submit a mod](https://github.com/elekloader/shop/issues/new?template=submit-mod.yml)**
form there. A bot checks your files and opens the shop's pull request for
you, and your newer releases reach the shop by themselves. You don't open a
pull request or an issue here to list a mod.
[docs/SHOP.md](docs/SHOP.md) is the whole guide, and the shop's other forms
(Remove a mod, Report a picture) are
[in the same place](https://github.com/elekloader/shop/issues/new/choose).

## Contributing

**Contributions are welcome and encouraged.** The core grows from what mods
need: when yours needs an event, a table or a firmware address the core
lacks, a pull request that adds it is the intended path
([Growing the core](docs/CORE-CONTRIBUTING.md)). So are new devices, OS
versions, fixes and docs ([CONTRIBUTING.md](CONTRIBUTING.md)).
[Contribute to elekloader!](docs/CONTRIBUTE.md) has the community's to-do
list for the core.

## Documentation

The [docs](docs/README.md) are elekloader's wiki:

| for | pages |
|---|---|
| **Using it** | [Install](docs/INSTALL.md) · [Build, flash and recover](docs/BUILDING.md) · [Cores](docs/CORES.md) · [The web page](docs/WEB.md) · [Splash screens](docs/SPLASH.md) |
| **Writing mods** | [Adapting your mod](docs/ADAPTING.md) · [Machine SDK](docs/SDK.md) · [Your mod in the shop](docs/SHOP.md) · [Growing the core](docs/CORE-CONTRIBUTING.md) · [Contribute](docs/CONTRIBUTE.md) · [The file format](docs/FORMAT.md) · [Adding a device](docs/DEVICES.md) · coding agents: [AGENTS.md](AGENTS.md) |
| **On your own site** | [The kit](docs/INTEGRATING.md) · [The engine in TypeScript](js/README.md) |
| **Maintaining it** | [Releasing](docs/RELEASING.md) · [The apps](docs/APPS.md) · [Tests](docs/TESTS.md) |

## Licence

GPL-2.0-or-later. See [LICENSE](LICENSE), and [NOTICE](NOTICE) for the code that
comes from digikit and for the credits. elekloader is not affiliated with
Elektron. Flashing custom firmware is at your own risk.
