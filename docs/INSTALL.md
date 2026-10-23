# Installing Spider-Man 2 (Xbox) on the Switch

[![English](https://img.shields.io/badge/lang-English-dc1e2e?style=for-the-badge)](INSTALL.md)
[![Español](https://img.shields.io/badge/idioma-Espa%C3%B1ol-1c2233?style=for-the-badge)](INSTALL.es.md)

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
  - [On Windows](#on-windows)
  - [On Linux](#on-linux)
  - [Installing the tools by hand](#installing-the-tools-by-hand)
  - [On Windows, by hand](#on-windows-by-hand)
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

The builder makes `SpiderMan2Xbox.nro` from your own copy: it checks your copy, recompiles the game's code and
builds the port, in 10 to 30 minutes depending on the computer, plus about 10 minutes the first time to install
its tools. It needs about 2 GB of space, and room for a copy of your game when it puts the game in the SD card
folder too.

First download `SpiderMan2-2004-NX-<version>.zip` from the [Releases](../../../releases) page (under
**Assets**, not "Source code") and unzip it.

### On Windows

1. **Double-click `build-windows.cmd`** in the unzipped folder. If Windows says "Windows protected your PC",
   click **More info**, then **Run anyway**: it is a script from the zip, not a signed program.
2. **The first time only, if WSL isn't installed** (WSL is the Linux that Windows 10 and 11 can install, free,
   from Microsoft): it offers to install it, and Windows asks for permission. Then restart the computer, open
   **Ubuntu** once from the Start menu to choose a user name and a password for it, and double-click
   `build-windows.cmd` again.
3. **Choose your game** in the window that opens: your disc image (the first part, if it is split), or
   `default.xbe` from the disc's files. You can also drop your game onto `build-windows.cmd` instead.
4. **Wait.** The first time it installs the tools inside Linux and asks once for the Linux password you chose;
   then it builds.
5. When it's done, the folder **`SD card`** opens beside `build-windows.cmd`. Its `switch` folder has the
   port and your game in it, ready for [the SD card](#copying-it-to-the-sd-card).

The Windows builder is new in 0.1.0. If it stops, its message says why, and the
[same steps by hand](#on-windows-by-hand) always work.

### On Linux

On Ubuntu or Debian, one command in the unzipped folder, pointing at your game (a disc image, the first part
of a split one, the game's folder, or `default.xbe`):

```sh
bash build.sh --copy-game "/path/to/Spider-Man 2.iso"
```

The first time it offers to install what it needs (git, cmake, ninja, g++ and devkitPro's Switch tools, with
`sudo`; `--yes` installs them without asking). `--copy-game` puts your game into the SD card folder it makes,
`out/sd/switch`, too; without it, you copy the game yourself. On another distribution,
[install the tools by hand](#installing-the-tools-by-hand) first.

### Installing the tools by hand

What `build.sh` installs by itself on Ubuntu and Debian (other distributions have the same packages under
similar names):

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

### On Windows, by hand

What `build-windows.cmd` does, step by step:

1. **Install WSL with Ubuntu.** Open PowerShell as administrator, type `wsl --install`, and restart the
   computer when it asks. At the next start, Ubuntu opens and asks for a user name and password (they are
   only for this Linux).
2. **Copy the port's folder into Linux** (building inside Linux is much faster than on `C:`). Your Windows
   drives are under `/mnt/`: `C:\Users\You\Downloads` is `/mnt/c/Users/You/Downloads`. In Ubuntu:
   ```sh
   cp -r "/mnt/c/Users/You/Downloads/SpiderMan2-2004-NX-0.1.0" ~/
   cd ~/SpiderMan2-2004-NX-0.1.0
   ```
3. **Build**, pointing at your game on the Windows drive. The first time it installs the tools (on WSL it
   also makes the `/etc/mtab` link devkitPro's installer needs):
   ```sh
   bash build.sh --copy-game "/mnt/c/Users/You/Games/Spider-Man 2.iso"
   ```
4. **Find the result in Windows**: in File Explorer, type `\\wsl$` in the address bar, then open
   `Ubuntu\home\<your user>\SpiderMan2-2004-NX-0.1.0\out\sd`.

### What the builder says

| Message | What to do |
|---|---|
| `Missing: ...` and `Install them now?` | Press Enter (or `Y`) and give your password: it installs them. On a distribution without `apt`, [install them by hand](#installing-the-tools-by-hand). |
| `still missing after installing` | The installation above it says what went wrong (often no internet). Try again, or [install them by hand](#installing-the-tools-by-hand). If devkitPro is somewhere other than `/opt/devkitpro`, run `export DEVKITPRO=/where/it/is` first. |
| `this default.xbe is not the release the port supports` | Your copy is another release of the game (another region or version). The port only runs the one it was made for: open an issue with the SHA-1 it printed, if you'd like that release supported. |
| `... is a compressed archive` | Extract the disc image or the game's folder from the `.zip`, `.7z` or `.rar`, and point the builder at that. |
| `default.xbe could not be read from ...` | The message says why (not an Xbox image, an Xbox 360 image, a missing part of a split image...). |
| `the PC tools did not build` / `the Switch build failed` | The log it names (`build-tools.log`, `build-switch.log`) says why. Attach it to an issue if it isn't clear. |
| `WSL cannot see ...` (Windows) | Put your game on a drive of this computer (not a network folder), and try again. |
| `the build stopped` (Windows) | The messages just above it, from Linux, say why: they are the same as the ones in this table. |

## Copying it to the SD card

1. Copy the `switch` folder the builder made to the **root** of your SD card: from `SD card` on Windows, from
   `out/sd` on Linux. It merges with the `switch` folder that is already there, and you get
   `switch/SpiderMan2Xbox/`.
2. If your game isn't in it yet (you built on Linux without `--copy-game`), copy it into
   `switch/SpiderMan2Xbox/game/`: your disc image (all its parts, if it is split), or `default.xbe` and the
   `data` folder, so you have `game/default.xbe` and `game/data/game.ini`.

> [!CAUTION]
> The `.nro` you built holds code made from your copy of the game. Keep it for yourself: don't share it,
> post it or upload it.

## The first start

Hold **R** while you start any game: the Homebrew Menu opens instead of the game (title takeover). Choose
**Spider-Man 2 (Xbox)**.

> [!WARNING]
> Don't start it from the Album: there, homebrew only gets about 440 MB of memory, and the game needs about
> 1.3 GB. The port tells you if it was started that way.

A blue screen says **Loading...** until the game draws its first picture. From then on the game is drawn by
the Switch's graphics chip, every frame. The game is in the console's language (below).

- **Quit:** hold **+ and −** together for one second.
- **Sound off:** hold **L** while the port starts (until the blue screen appears).
- **The old CPU renderer:** hold **R** while the port starts. It shows a still picture every few seconds, but
  it doesn't use the graphics chip: try it if the picture is wrong or the port stops right after the blue
  screen, and say so in your report.
- **No render thread:** hold **ZR** while the port starts. The graphics chip then gets its work from the
  game's own thread: slower, but a way to tell whether a problem comes from the render thread.

The buttons held at start last for that start only; the [debug menu](#the-debug-menu) keeps the same choices
for every start.

## The debug menu

Hold **ZL + ZR** and press **−** (in any order) to open the port's settings over the game; the same
combination, **B** or **+** closes it. **Up/Down** picks a setting, **Left/Right** or **A** changes it, and the
touch screen works too. While it is open the game gets no input (it keeps running: press **+** first to pause
it). Each setting has a line of help, and an orange note when the chosen value costs something (heat, battery,
speed). The bottom line shows the frame rate, the time of a frame and the draw calls right now.

- **Language**: Auto (the console's), English, Español, Français, Deutsch or Italiano, from the next start.
- **Resolution**: 480p (the Xbox's), 720p or 1080p: the whole game and its movies drawn with that many pixels.
  720p fills the handheld screen pixel for pixel; 1080p, for docked play, is drawn bigger and shrunk (in handheld
  mode it draws 720p, the screen's own). More work for the GPU, which the Auto GPU clock raises: if the frame rate
  drops, go back a step.
- **Screen**: 4:3 with black, blurred or spider-web sides, or **16:9 full screen**, the game's own hidden
  widescreen camera, with more of the city at the sides and the HUD, menus and movies kept 4:3 in the middle.
- **Upscaling** (Smooth or Sharp) and **Texture filtering** (Standard or Anisotropic 4x to 16x).
- **Vibration**: Off, Low, Medium or High, on Joy-Con or a Pro Controller (the Switch Lite has no rumble of its
  own). A rumble the game leaves on stops after 2 seconds.
- **CPU, GPU and memory clocks**: Auto (the default), stock, or one of the rates Nintendo's own performance
  profiles use (up to 1785 MHz CPU, 768 MHz GPU, 1600 MHz memory). Auto is 1785 MHz CPU and 1600 MHz memory, and
  for the GPU 460.8 MHz at 480p in handheld mode, 768 MHz at 720p and docked. The console's own clocks come back
  while the port is in the HOME menu and when it closes. Stock uses less battery.
- **Frame-rate counter**: off, FPS, or full (also the time of a frame, the draw calls, the clocks, the memory),
  in the top left corner.
- **Sound volume**, **renderer** (GPU or CPU), **render thread**, and the **start-up log** (the newest log
  lines on the start screen instead of "Loading...", for a photo when the game doesn't start).

Changes are saved at once in `switch/SpiderMan2Xbox/settings.ini`. To go back to the recommended settings,
choose **Restore recommended settings**, or delete the file.

## Language

The game takes the console's language (System Settings → System → Language). With English, Spanish (Spain or
Latin America), French (France or Canada), German or Italian, its menus and subtitles are in that language,
from the texts on your disc; with any other language, in English. The voices are in English in every language:
the disc only has the English ones. To play in another language than the console's, set **Language** in the
debug menu and start the port again.

## Saves

Your saves are in `switch/SpiderMan2Xbox/save/`: the game's saves in `save/progress/` (what the Xbox keeps in
`E:\UDATA` for this game) and its own data in `save/data/` (`E:\TDATA`). Back the `save` folder up from time to
time, and copy it back to restore; updates don't touch it. The rest of the Xbox's hard disk (its cache drives) and
the shader list are in `cache/`, which can be deleted at any time: it is made again at the next start.

## Updating

Build the new version the same way, then copy its `out/sd/switch` folder over the old one. `game/` and
`save/` stay as they are. Versions before 0.1.0 kept everything in `hdd/`: at the first start of 0.1.0 the saves
in it are moved to `save/`, `hdd/` becomes `cache/`, and `shaders.cache` moves into it. If a save is in both
places, the one in `hdd/` stays where it is and is used, and the log says so.

## Troubleshooting

| What you see | What to do |
|---|---|
| **Game not found** | Your game isn't in `switch/SpiderMan2Xbox/game/`, or it is in a form the port doesn't read. The screen lists the files it found and why each can't be used. See [the formats](#the-formats-the-port-reads). |
| **The data folder is missing** | The contents of `data` are directly in `game/`. Make a folder named `data` there and move `game.ini`, `movies`, `packs` and `sound` into it. |
| **A different release of the game** | Your copy isn't the release the port was built from. It would most likely crash: press **+** to go back. The builder normally catches this; this happens when the `.nro` was built from another copy. |
| **Another game or version** | The image in `game/` holds another game, or another version of this one. |
| **Not enough memory in this mode** | Start the port with **R + a game**, not from the Album. |
| **The game did not start** | The newest file in `logs/` says why. |
| **The screen freezes** | Take a photo of the screen, restart the console (hold POWER for 12 seconds), and send the `logs` folder. Then try once holding **L** at start (no sound), once holding **ZR** (no render thread) and once holding **R** (the CPU renderer), and say which ones still freeze. If it freezes on "Loading...", turn on **Start-up log** in the [debug menu](#the-debug-menu) (or in `settings.ini`: `startup_log=on`), so the photo shows how far it got. |
| **The picture is wrong** (missing, black, flickering, wrong colours) | Take a photo and send the `logs` folder. Then try holding **R** at start (the CPU renderer): if that picture is right, the problem is in the GPU renderer. |
| **It runs slowly** | Turn on the frame-rate counter in the [debug menu](#the-debug-menu), and check that the clocks are on Auto there. At 720p or 1080p, try a lower Resolution, or 4:3 instead of 16:9. Send the `logs` folder: every 5 seconds the log has `perf:` lines that say where the time goes. |
| **It doesn't vibrate** | The Switch Lite has no rumble motor: use Joy-Con or a Pro Controller. Check that **Vibration** isn't Off in the [debug menu](#the-debug-menu), and that the controller's vibration is on in the console's System Settings. |
| **The game is in the wrong language** | It follows the console's language, read at start. Set **Language** in the [debug menu](#the-debug-menu) and start the port again. The voices are always in English. |
| **It stutters** (short freezes while playing) | Mostly the first time through a place, while the graphics driver builds the shaders it needs; they are kept in `cache/shaders.cache` and built at the start the next time. Send the `logs` folder if it goes on: a `hitch:` line names each stutter and what took the time. |
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
