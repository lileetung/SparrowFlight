# Contributing to SparrowFlight

Thank you for your interest in SparrowFlight.

## Problem reports and questions

- **Problem reports:** open an [issue](https://github.com/lileetung/SparrowFlight/issues/new/choose). The bug report form asks for the app and firmware versions and the flight log, which [SUPPORT.md](SUPPORT.md) explains how to find.
- **Questions and suggestions:** open an issue as well.
- **Corrections to the documentation:** typos, unclear steps or wrong values, as an issue or a pull request.

## Firmware

- Open an issue before a larger change, and before any change to the link: packets, commands, parameters or telemetry fields. The SparrowFlight app depends on them.
- Build with the versions pinned in `firmware/platformio.ini`.
- Say how the change was tested: on the bench (a dry run on USB, and `get` after flashing) and, for anything that affects flight, in flight, with the blackbox log of that flight.
- Update `firmware/README.md` when commands, parameters or behaviour change, and add the change to `CHANGELOG.md`.
- Start new source files with the `SPDX-License-Identifier: GPL-3.0-or-later` line.

## Hardware

- **Flight controller:** change the EasyEDA Pro project, then export the schematic PDF, Gerber, BOM and pick-and-place files from it again, so that all the files match, and update the layout image and the 3D renders. A board change raises the version (see `CHANGELOG.md`) in the file names and on the silkscreen.
- **Frame:** change the DXF, its drawing and the 3D render together.
- Say whether the changed part was built and flown.

## Licences

A contribution is accepted under the licence of the part it changes; see [LICENSE.md](LICENSE.md). The frame's licence, CC BY-NC-SA 4.0, does not allow commercial use.

## Pull requests

Keep each pull request to one change. Write commit messages and documentation in English; `README.zh-TW.md` mirrors `README.md`, so a change to one needs the same change in the other.

## Code of Conduct

Everyone taking part in SparrowFlight is expected to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
