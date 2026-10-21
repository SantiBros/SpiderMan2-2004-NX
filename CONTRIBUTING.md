# Contributing

Thanks for helping! Reports from real consoles are the most useful thing right now: the port is tested on PC
and ARM test builds, and consoles are what's missing.

## Reporting a bug

Use the [bug report form](../../issues/new/choose) and attach the **`logs` folder** from
`switch/SpiderMan2Xbox/` (zipped), with `crash.txt` if there is one. If the console froze or showed an Atmosphère
error, the newest file in `atmosphere/crash_reports/` helps too. Please include the port version, your firmware
and Atmosphère versions, and the format your game is in. English or Spanish are both fine.

## The one rule: no game files

Never attach, commit or link game files: `default.xbe`, the `data` folder, disc images, or anything taken from
them, **including the `.nro` you built and the `generated/` code**, which are made from the game. Issues and pull
requests that contain or ask for game files are closed. The `.gitignore` keeps them out of commits; please don't
work around it.

## Pull requests

- Build both versions before sending: the Switch `.nro` (`build.sh`) and the PC test build
  ([docs/BUILDING.md](docs/BUILDING.md)). The test build runs the game headless with the Switch build's pacing and
  can save its frames, which makes it easy to check a change.
- A change to the disc image reader should pass `tools/test/disc_check` on your copy in the formats it touches.
- Keep changes focused, and describe what you tested (PC test build, ARM test build, console).
- Code style: the existing code's (C++20, 4-space indents, comments that say *why*).
- Changes to the SDK in `sdk/` are listed in [docs/TECHNICAL.md](docs/TECHNICAL.md#changes-to-the-sdk): add yours.
- A config for another release of the game is welcome: addresses and names only, never the `.xbe`.

By contributing you agree that your contribution is released under the project's [MIT License](LICENSE).
