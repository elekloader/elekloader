# Contributing to elekloader

Contributions are welcome and encouraged. elekloader grows from what the
people who write mods need, so a pull request from someone building a mod
is exactly what the project is for. You do not need to be a maintainer, or
to ask first. A draft pull request that says what it could not finish is
fine.

Good things to send:

- **A core addition**: an event, a table or a firmware export your mod
  needed and the core lacked. This is the main way the core grows:
  [docs/CORE-CONTRIBUTING.md](docs/CORE-CONTRIBUTING.md) says how to build
  it so it moves into the core cleanly.
- **A new device or OS version**: [docs/DEVICES.md](docs/DEVICES.md).
- **A mod for the shop**: an item in `web/catalog.json` pointing at your
  release ([docs/WEB.md](docs/WEB.md)).
- **Fixes and docs**: a refusal message that misled you, a step that did
  not work, a page that was unclear.

Using a coding agent? Point it at [AGENTS.md](AGENTS.md). It has the
rules, the commands and what "done" means, and it tells the agent that a
pull request here is welcome.

Before you open one:

- run the tests you can (`python tests/test_units.py` always; the others
  need stock files and say so: [docs/TESTS.md](docs/TESTS.md)), and say
  in the pull request which ran;
- commit no firmware: no `.syx`, `.bin` or built `.elemod` files;
- keep the licence: GPL-2.0-or-later for the Python and the mods,
  GPL-3.0-or-later for `js/`.

`main` takes changes only through pull requests, so open one from a
branch or a fork.
