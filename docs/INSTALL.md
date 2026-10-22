# Installing Spider-Man 2 (Xbox) on the Switch

[![English](https://img.shields.io/badge/lang-English-c8202f?style=for-the-badge)](INSTALL.md)
[![Español](https://img.shields.io/badge/idioma-Espa%C3%B1ol-1b2a4a?style=for-the-badge)](INSTALL.es.md)

[← Back to the README](../README.md)

> [!TIP]
> **New to Switch homebrew?** You first need a console that runs custom firmware (Atmosphère) with the
> Homebrew Menu. The community guide [switch.hacks.guide](https://switch.hacks.guide/) explains how, step by
> step. The words used on this page are explained in the [glossary](#glossary).

The port is built from **your own copy** of the game, once, on your computer: the result is a `.nro` made
for you. Then it goes on the SD card with your game.

- [Getting your game](#getting-your-game)
  - [With a modded Xbox](#with-a-modded-xbox)
  - [The formats the port reads](#the-formats-the-port-reads)
- [Building the port](#building-the-port)
  - [On Linux](#on-linux)
  - [On Windows with WSL](#on-windows-with-wsl)
  - [What the builder says](#what-the-builder-says)
- [Copying it to the SD card](#copying-it-to-the-sd-card)
- [The first start](#the-first-start)
- [Saves](#saves)
- [Updating](#updating)
- [Troubleshooting](#troubleshooting)
- [Reporting a bug](#reporting-a-bug)
- [Glossary](#glossary)

## Getting your game

You need *Spider-Man 2* for the **original Xbox**, the release whose `default.xbe` has the SHA-1
`853bcb253c5baea2d7e2d9b59e5888b4c87bb402` (title ID `4156002B`, version 4). The builder checks it, so
you don't have to work it out yourself. The port never downloads anything: it uses the copy you give it.

### With a modded Xbox

An ordinary PC DVD drive can't read original Xbox game discs. The usual way to copy your own disc is a
modded Xbox:

1. On the Xbox, a tool such as [DVD2Xbox](https://consolemods.org/wiki/Xbox:DVD2Xbox) copies the disc to the
   Xbox's hard disk, either as files ("Copy DVD/CD to Harddisk") or as an ISO ("ISO Ripper", a full disc
   image split into parts).
2. Copy that folder or those files to your computer over the network, with an FTP program such as
   FileZilla (modded Xbox dashboards run an FTP server).

Copying a disc with a PC is only possible with very particular drives; the
[Redump wiki](http://wiki.redump.org/) has the details. Whatever the tool, the result is either the game's
files (`default.xbe` and the `data` folder) or a disc image, and the port takes both.

### The formats the port reads

| What you have | Works? |
|---|---|
| The disc's files: `default.xbe` and the `data` folder | ✅ |
| `.iso` or `.xiso`: a trimmed XISO (as `extract-xiso`, `xdvdfs` or Repackinator make) | ✅ |
| `.iso`: a full disc dump (with the video partition first, as Redump's are) | ✅ |
| `.cci`: compressed, as Repackinator makes for Cerbios | ✅ |
| `.cso`: compressed (version 2, LZ4), as Repackinator, `ciso` or `xdvdfs` make for StellarOS | ✅ |
| Any of the above split into parts: `name.1.iso`, `name.2.iso`, ... (also `.1.cci`, `.1.cso`) | ✅ put all the parts in the folder |
| `.zip`, `.7z`, `.rar` | ❌ extract the image or the folder from them first |
| An Xbox 360 disc image, or a PSP-style CSO (version 1, zlib) | ❌ |

If a tool split your image with another naming, rename the parts to `name.1.iso`, `name.2.iso`, and so on,
in order. On a FAT32 SD card no file can be over 4 GB: a full disc dump must be split, or turned into a
trimmed XISO or a `.cci` / `.cso` (this game fits in about 1.7 GB as an XISO, 1.5 GB compressed).
[ConsoleMods' guide to ISO tools](https://consolemods.org/wiki/Xbox:ISO_Extraction_%26_Repacking) lists the
tools that convert between these.

## Building the port

The builder, `build.sh`, needs a few free tools the first time (about 10 minutes, once), then one command:
it checks your copy, recompiles the game's code and builds `SpiderMan2Xbox.nro`, in 10 to 30 minutes
depending on the computer. It needs about 2 GB of space.

First download `SpiderMan2-2004-NX-<version>.zip` from the [Releases](../../../releases) page and unzip it, or
clone the repository with git.

### On Linux

On Ubuntu or Debian (other distributions have the same packages under similar names):

1. **The build tools:**
   ```sh
   sudo apt install git cmake ninja-build g++ wget
   ```
2. **devkitPro's Switch tools** ([devkitPro's instructions](https://devkitpro.org/wiki/devkitPro_pacman)):
   ```sh
   wget https://apt.devkitpro.org/install-devkitpro-pacman
   chmod +x ./install-devkitpro-pacman
   sudo ./install-devkitpro-pacman
   sudo dkp-pacman -Sy
   sudo dkp-pacman -S switch-dev switch-mesa
   ```
   Press Enter to take all the packages it offers. `switch-mesa` is OpenGL for the Switch, which the GPU renderer
   draws with.
3. **Build**, in the folder you unzipped, pointing at your game (a disc image, the first part of a split
   one, the game's folder, or `default.xbe`):
   ```sh
   bash build.sh "/path/to/Spider-Man 2.iso"
   ```

### On Windows with WSL

WSL runs a real Linux inside Windows 10 and 11, and the builder runs there.

1. **Install WSL with Ubuntu.** Open PowerShell as administrator, type `wsl --install`, and restart the
   computer when it asks. At the next start, Ubuntu opens and asks for a user name and password (they are
   only for this Linux).
2. **In Ubuntu**, make WSL ready for devkitPro, then do steps 1 and 2 of [On Linux](#on-linux):
   ```sh
   sudo ln -s /proc/self/mounts /etc/mtab
   ```
3. **Copy the port's folder into Linux** (building inside Linux is much faster than on `C:`). Your Windows
   drives are under `/mnt/`: `C:\Users\You\Downloads` is `/mnt/c/Users/You/Downloads`.
   ```sh
   cp -r "/mnt/c/Users/You/Downloads/SpiderMan2-2004-NX-0.0.10" ~/
   cd ~/SpiderMan2-2004-NX-0.0.10
   ```
4. **Build**, pointing at your game on the Windows drive (it is only read):
   ```sh
   bash build.sh "/mnt/c/Users/You/Games/Spider-Man 2.iso"
   ```
5. **Find the result in Windows**: in File Explorer, type `\\wsl$` in the address bar, then open
   `Ubuntu\home\<your user>\SpiderMan2-2004-NX-0.0.10\out\sd`.

### What the builder says

| Message | What to do |
|---|---|
| `missing: ...` | Install what it names (step 1). |
| `devkitPro's Switch tools were not found` | Do step 2. If devkitPro is somewhere other than `/opt/devkitpro`, run `export DEVKITPRO=/where/it/is` first. |
| `this default.xbe is not the release the port supports` | Your copy is another release of the game (another region or version). The port only runs the one it was made for: open an issue with the SHA-1 it printed, if you'd like that release supported. |
| `... is a compressed archive` | Extract the disc image or the game's folder from the `.zip`, `.7z` or `.rar`, and point the builder at that. |
| `default.xbe could not be read from ...` | The message says why (not an Xbox image, an Xbox 360 image, a missing part of a split image...). |
| `the PC tools did not build` / `the Switch build failed` | The log it names (`build-tools.log`, `build-switch.log`) says why. Attach it to an issue if it isn't clear. |

## Copying it to the SD card

1. Copy the folder `out/sd/switch` to the **root** of your SD card. It merges with the `switch` folder that
   is already there, and you get `switch/SpiderMan2Xbox/`.
2. Copy your game into `switch/SpiderMan2Xbox/game/`: your disc image (all its parts, if it is split), or
   `default.xbe` and the `data` folder, so you have `game/default.xbe` and `game/data/game.ini`.

> [!CAUTION]
> The `.nro` you built holds code made from your copy of the game. Keep it for yourself: don't share it,
> post it or upload it.

## The first start

Hold **R** while you start any game: the Homebrew Menu opens instead of the game (title takeover). Choose
**Spider-Man 2 (Xbox)**.

> [!WARNING]
> Don't start it from the Album: there, homebrew only gets about 440 MB of memory, and the game needs about
> 1.3 GB. The port tells you if it was started that way.

A blue screen shows what the port is doing: the newest lines of its log, until the game draws its first
picture. From then on the game is drawn by the Switch's graphics chip, every frame.

- **Quit:** hold **+ and −** together for one second.
- **Sound off:** hold **L** while the port starts (until the blue screen appears).
- **The old CPU renderer:** hold **R** while the port starts. It shows a still picture every few seconds, but
  it doesn't use the graphics chip: try it if the picture is wrong or the port stops right after the blue
  screen, and say so in your report.
- **No render thread:** hold **ZR** while the port starts. The graphics chip then gets its work from the
  game's own thread: slower, but a way to tell whether a problem comes from the render thread.

## Saves

The port keeps an Xbox hard disk in `switch/SpiderMan2Xbox/hdd/`, and the game saves there as it would on
the Xbox. Back that folder up from time to time; updates don't touch it.

## Updating

Build the new version the same way, then copy its `out/sd/switch` folder over the old one. `game/` and
`hdd/` stay as they are.

## Troubleshooting

| What you see | What to do |
|---|---|
| **Game not found** | Your game isn't in `switch/SpiderMan2Xbox/game/`, or it is in a form the port doesn't read. The screen lists the files it found and why each can't be used. See [the formats](#the-formats-the-port-reads). |
| **The data folder is missing** | The contents of `data` are directly in `game/`. Make a folder named `data` there and move `game.ini`, `movies`, `packs` and `sound` into it. |
| **A different release of the game** | Your copy isn't the release the port was built from. It would most likely crash: press **+** to go back. The builder normally catches this; this happens when the `.nro` was built from another copy. |
| **Another game or version** | The image in `game/` holds another game, or another version of this one. |
| **Not enough memory in this mode** | Start the port with **R + a game**, not from the Album. |
| **The game did not start** | The newest file in `logs/` says why. |
| **The screen freezes** | Take a photo of the screen, restart the console (hold POWER for 12 seconds), and send the `logs` folder. Then try once holding **L** at start (no sound), once holding **ZR** (no render thread) and once holding **R** (the CPU renderer), and say which ones still freeze. |
| **The picture is wrong** (missing, black, flickering, wrong colours) | Take a photo and send the `logs` folder. Then try holding **R** at start (the CPU renderer): if that picture is right, the problem is in the GPU renderer. |
| **It runs slowly** | Send the `logs` folder: every 5 seconds the log has a `perf:` line that says where the time goes. |
| **It stutters** (short freezes while playing) | Mostly the first time through a place, while the graphics driver builds the shaders it needs; they are kept in `shaders.cache` and built at the start the next time. Send the `logs` folder if it goes on: a `hitch:` line names each stutter and what took the time. |
| **An Atmosphère error screen** | Send the newest file in `atmosphere/crash_reports/` and the `logs` folder. |

## Reporting a bug

Open an [issue](../../../issues/new/choose) and attach the `switch/SpiderMan2Xbox/logs` folder (zip it), with
`crash.txt` if there is one, and a photo of the screen if it helps. Say which port version, which firmware and
Atmosphère version, and what format your game is in. **Never attach game files** or the `.nro` you built.

## Glossary

| Word | Meaning |
|---|---|
| **XBE** | The Xbox's program file format. `default.xbe` is the game's program; the port is built from it. |
| **XISO** | A disc image with only the game partition of an Xbox disc. |
| **Full disc dump** | A disc image of the whole disc (the video partition first, then the game partition), as Redump's are. |
| **CCI**, **CSO** | Compressed Xbox disc images, made by Repackinator, read by Cerbios (CCI) and StellarOS (CSO). |
| **Split image** | An image cut into parts (`name.1.iso`, `name.2.iso`, ...) so each fits FAT32's 4 GB limit. |
| **FAT32**, **exFAT** | The two file systems an SD card can have on the Switch. FAT32 can't hold files over 4 GB. |
| **Title takeover** | Starting the Homebrew Menu by holding R while you start a game: homebrew then gets the game's memory. |
| **emuMMC** | A copy of the console's system on the SD card, so homebrew doesn't touch the real one. |
| **`.nro`** | A homebrew program for the Switch. |
| **devkitPro** | The free toolchain used to build Switch homebrew. |
| **WSL** | Windows Subsystem for Linux: a real Linux inside Windows. |
| **Static recompilation** | Translating a program's code to another processor ahead of time, instead of while it runs. |
