<div align="center">

[![English](https://img.shields.io/badge/lang-English-dc1e2e?style=for-the-badge)](README.md)
[![Español](https://img.shields.io/badge/idioma-Espa%C3%B1ol-1c2233?style=for-the-badge)](README.es.md)

<img src="docs/images/banner.jpg" alt="Spider-Man 2 (original Xbox, 2004) for Nintendo Switch, an unofficial homebrew port" width="100%">

# Spider-Man 2 (Xbox) for Nintendo Switch

**An unofficial homebrew port of the original Xbox version of *Spider-Man 2* (Treyarch, 2004) to the Nintendo Switch.**<br>
The game's own code, recompiled ahead of time for the Switch's processor, with the Xbox's system rebuilt
around it. Bring your own copy of the game, build the port from it once, and swing through New York.

[![Latest release](https://img.shields.io/github/v/release/SantiBros/SpiderMan2-2004-NX?include_prereleases&style=for-the-badge&label=download&color=dc1e2e&logo=github&logoColor=white)](../../releases)
[![Downloads](https://img.shields.io/github/downloads/SantiBros/SpiderMan2-2004-NX/total?style=for-the-badge&label=downloads&color=1c2233)](../../releases)
![Status: test build](https://img.shields.io/badge/status-test%20build-e0a020?style=for-the-badge)
![Nintendo Switch homebrew](https://img.shields.io/badge/Nintendo%20Switch-homebrew-1c2233?style=for-the-badge)
![Original Xbox, 2004](https://img.shields.io/badge/original%20Xbox-2004-1c2233?style=for-the-badge)
![Atmosphère](https://img.shields.io/badge/CFW-Atmosph%C3%A8re-1c2233?style=for-the-badge)
[![License: MIT](https://img.shields.io/badge/license-MIT-1c2233?style=for-the-badge)](LICENSE)

[Install](#how-to-install) · [Controls](#controls) · [Debug menu](#debug-menu) · [Status](#status) · [FAQ](#faq) · [Troubleshooting](docs/INSTALL.md#troubleshooting) · [Changelog](CHANGELOG.md) · [How it works](docs/TECHNICAL.md)

</div>

> [!IMPORTANT]
> **No game files are included, not in this repository and not in the releases.** You need your own
> copy of *Spider-Man 2* for the **original Xbox**, and you build the port from it on your computer
> (one command). Please don't ask for game files in issues, and never upload them, or the `.nro` you
> build, anywhere: it contains code made from the game.
>
> This port is for the **original Xbox version (2004)** only. The PlayStation 2, GameCube, PC, Game Boy
> Advance, Nintendo DS and PSP versions are different programs and don't work, and neither do the
> newer *Spider-Man* games.

> [!WARNING]
> **Test build: not yet played on a Switch.** On the port's PC test build the game runs through its menus,
> cinematics and first missions in the city, in five languages. On a Switch it has not yet got past its start
> in testing, so how fast it runs there is not known yet: 0.1.0 brings a frame-rate counter and the Switch's
> clocks in a debug menu to find out. [Testers](#help-wanted-testers) are welcome.

> [!TIP]
> **New in 0.1.0**
> - **A debug menu**: hold **ZL + ZR** and press **−** for the port's settings: a frame-rate counter, the
>   Switch's CPU, GPU and memory clocks, the language, the sound volume and the renderer, saved on the SD card.
> - **In your language**: the game follows the console's language, with its menus and subtitles in Spanish,
>   French, German or Italian (the voices stay in English: the disc only has those). Or pick one in the menu.
> - **A clean start screen**: "Loading..." instead of the log scrolling by (the log can still be turned on).
> - **Faster**: the game's thread needs about 14% less processor time a frame (its code is smaller and records
>   its drawing with less work).
>
> All the details are in the [changelog](CHANGELOG.md).

## Screenshots

<table>
  <tr>
    <td colspan="2" align="center"><img src="docs/images/screenshots/city.jpg" alt="Spider-Man on a rooftop at dusk, with the HUD" width="100%"></td>
  </tr>
  <tr>
    <td colspan="2" align="center"><sub>The first rooftop, at dusk</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/screenshots/title.jpg" alt="The title screen, PRESS START TO BEGIN"></td>
    <td width="50%"><img src="docs/images/screenshots/title-es.jpg" alt="The title screen in Spanish, PRESIONAR START PARA EMPEZAR"></td>
  </tr>
  <tr>
    <td align="center"><sub>The title screen</sub></td>
    <td align="center"><sub>The same on a console set to Spanish</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/screenshots/menu.jpg" alt="The swing control menu"></td>
    <td width="50%"><img src="docs/images/screenshots/debug-menu.jpg" alt="The port's debug menu over the game, with the frame-rate counter"></td>
  </tr>
  <tr>
    <td align="center"><sub>Choosing the swing controls</sub></td>
    <td align="center"><sub>The debug menu (ZL + ZR + −) and the frame-rate counter; the numbers are an example</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/images/screenshots/start.jpg" alt="The port's start screen, Loading"></td>
    <td width="50%"><img src="docs/images/screenshots/not-found.jpg" alt="The port explaining what to put in the game folder"></td>
  </tr>
  <tr>
    <td align="center"><sub>The port's start screen, until the game draws</sub></td>
    <td align="center"><sub>When no usable copy is found, it says what to put where</sub></td>
  </tr>
  <tr>
    <td colspan="2" align="center"><img src="switch/icon.jpg" alt="The port's icon: Spider-Man's mask with the Spider-Man 2 logo" width="160"></td>
  </tr>
  <tr>
    <td colspan="2" align="center"><sub>The port's icon in the Homebrew Menu</sub></td>
  </tr>
</table>

<p align="center"><sub>The game screenshots come from the port's PC test build, which runs the same recompiled code
and the same GPU renderer as the Switch build (on the PC's Mesa instead of the Switch's), with the Switch build's
screen layout: the game's 4:3 picture on the Switch's 1280×720 screen. The port's own screens and menu are drawn
by the Switch build's screen code, run on the PC.</sub></p>

## Features

- **The game's real code, recompiled, not emulated.** The 26,876 functions of the game's x86 code are
  translated to C++ ahead of time and compiled for the Switch's ARM processor. The Xbox's kernel,
  Direct3D 8, DirectSound and controller interfaces are rebuilt as native Switch code
  ([how it works](#how-it-works)).
- **Drawn by the Switch's GPU.** The game's Direct3D 8 drawing becomes OpenGL for the Switch's graphics
  chip: its vertex programs and pixel combiners are translated to shaders, its textures decoded once and
  kept, on a render thread beside the game's.
- **Your copy in the format you have.** A disc image (`.iso` or `.xiso`, a trimmed XISO or a full disc
  dump), the compressed `.cci` (Cerbios) and `.cso` (StellarOS) images that Repackinator makes, any of
  them split into parts for a FAT32 card, or the files from the disc.
- **Built from your copy in one command.** `build.sh` checks that your copy is the supported release,
  recompiles it and builds the `.nro`, with the SD card folder laid out.
- **In your language.** The game takes the console's language: English, Spanish, French, German or
  Italian menus and subtitles, from the texts on your disc.
- **A debug menu** (**ZL + ZR + −**) with a frame-rate counter, the Switch's CPU, GPU and memory clocks
  (only rates Nintendo's own profiles use, given back in the HOME menu and when you quit), the language, the
  sound volume and the renderer. Saved in `settings.ini`.
- **Says what's wrong.** Missing data folder, an archive that needs extracting, an Xbox 360 image, another
  release of the game: the screen explains it in plain words.
- **Made for testing.** The log reaches the SD card several times a second, so it survives a frozen console,
  and a status line every 2 seconds says what each of the game's threads is doing. Crashes leave a report.
  The start screen can show the newest lines of the log too (the debug menu's start-up log).
- **Controls mapped by position**, so the buttons the game shows are where your fingers expect them.
- **Fully offline.** The port doesn't use the network.

## What you need

| | What |
|---|---|
| **Console** | A Nintendo Switch, Switch OLED or Switch Lite running [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) with the Homebrew Menu. Use emuMMC and keep a backup of your SD card. |
| **Game** | Your own *Spider-Man 2* for the **original Xbox**, the release whose `default.xbe` has the SHA-1 `853bcb253c5baea2d7e2d9b59e5888b4c87bb402` (title ID `4156002B`, version 4). As a disc image or as the disc's files, about 1.7 GB. `build.sh` checks it for you. |
| **Computer** | Linux, or Windows 10/11 with WSL (the Linux that Windows can install), to build the port once. About 2 GB of space and 10 to 30 minutes. macOS is untested. |
| **SD card** | About 2 GB free. FAT32 or exFAT (on FAT32, a disc image over 4 GB must be split). |

## How to install

> [!TIP]
> **New to Switch homebrew?** First set up your console with custom firmware (Atmosphère) and the
> Homebrew Menu: the community guide [switch.hacks.guide](https://switch.hacks.guide/) explains it step by
> step. Unsure what XBE, XISO, CCI or WSL mean? See the [glossary](docs/INSTALL.md#glossary).

<img src="docs/images/install-steps.png" alt="Install in 4 steps: download the zip from Releases, build it with your game, copy the SD card folder and your game, start with R + any game" width="100%">

1. **Get your game as files.** A disc image of your own disc, or the files from it. PC DVD drives can't
   read original Xbox discs: [how to get them](docs/INSTALL.md#getting-your-game).
2. **Build the port.** Download `SpiderMan2-2004-NX-0.1.0.zip` from the [Releases](../../releases) page and
   unzip it on your computer, then run one command in its folder, pointing at your game:
   ```sh
   bash build.sh "/path/to/Spider-Man 2.iso"
   ```
   The first time, install the tools it needs (about 10 minutes, once): [step by step](docs/INSTALL.md#building-the-port).
3. **Copy to the SD card.** Copy the `out/sd/switch` folder it made to the root of your SD card. Then copy
   your game into `switch/SpiderMan2Xbox/game/`: your disc image (all its parts, if it is split), or
   `default.xbe` and the `data` folder.
4. **Play.** Hold **R** while you launch any game to open the Homebrew Menu, then choose
   **Spider-Man 2 (Xbox)**.

> [!WARNING]
> Always open the Homebrew Menu with **R + a game** (title takeover). From the Album, homebrew only gets
> about 440 MB of memory, and the game needs about 1.3 GB.

<details>
<summary><b>What the folder looks like on the SD card</b></summary>

```
sd:/switch/SpiderMan2Xbox/
├── SpiderMan2Xbox.nro    the port you built (never share it: it holds code made from the game)
├── README.txt            the player's guide
├── game/                 YOUR game, one of:
│   ├── Spider-Man 2.iso      a disc image (.iso .xiso .cci .cso), or all the parts of a split one
│   └── default.xbe, data/    or the files from the disc
├── hdd/                  the Xbox hard disk: your saves (made at the first start)
├── logs/                 sm2_NNN.log and crash.txt: attach them to bug reports
├── settings.ini          the debug menu's settings (delete it for the recommended ones)
└── shaders.cache         the shaders the game needed, built again at the next start
```

</details>

The [full install guide](docs/INSTALL.md) covers getting your game, building on Windows with WSL, updating
and [troubleshooting](docs/INSTALL.md#troubleshooting).

### Updating

Build the new version the same way, then copy its `out/sd/switch` folder over the old one. Your `game/`
and `hdd/` folders stay as they are.

## Controls

The game shows the Xbox controller's buttons. They are mapped by **position**, so the button the game
asks for is where it is on the Xbox controller:

| Xbox | Switch | | Xbox | Switch |
|---|---|---|---|---|
| A (bottom) | **B** | | Left / right trigger | **ZL** / **ZR** |
| B (right) | **A** | | White / black button | **L** / **R** |
| X (left) | **Y** | | Start / Back | **+** / **−** |
| Y (top) | **X** | | Sticks, D-pad | the same |

Hold **+ and −** together for one second to quit. Hold **ZL + ZR** and press **−** for the port's
[debug menu](#debug-menu).

To find where a problem comes from, hold one of these while the port starts: **L** turns the sound off,
**R** uses the old CPU renderer (a still picture every few seconds, without the graphics chip), **ZR** keeps
the graphics work on the game's own thread (no render thread). They last for that start only.

## Debug menu

Hold **ZL + ZR** and press **−** (in any order) to open it; the same, **B** or **+** close it. **Up/Down**
picks a setting, **Left/Right** or **A** changes it, and the touch screen works too. While it is open the
game gets no input (press **+** first if you want the game paused). Changes are saved at once in
`settings.ini` beside the `.nro`.

| Setting | Choices | What it does |
|---|---|---|
| Language | Auto, English, Español, Français, Deutsch, Italiano | The game's menus and subtitles. Auto follows the console; a language the game doesn't have is English. The voices are English in every language. From the next start. |
| CPU clock | Stock (1020 MHz), 1224, 1428, 1581, 1785 MHz | The game's translated code runs on the CPU: raise it if the counter stays under 30 in busy places. |
| GPU clock | Stock, 307.2, 384, 460.8, 614.4, 768 MHz | The graphics chip. 460.8 MHz is the highest Nintendo uses in handheld mode. |
| Memory clock | Stock, 1331.2, 1600 MHz | 1600 MHz is the docked memory clock. |
| Frame-rate counter | Off, FPS, Full | In the top left corner, beside the picture. Full adds the time of a frame, the draw calls, the clocks and the memory. |
| Sound volume | 100% to 25%, Off | The game's sound, before the console's own volume. |
| Renderer | GPU, CPU | CPU is many times slower: only to tell whether a fault in the picture comes from the GPU renderer. From the next start. |
| Render thread | On, Off | The graphics work on a core of its own. From the next start. |
| Start-up log | Off, On | The newest log lines on the start screen while the game loads, for a photo when it doesn't start. From the next start. |

The clocks only offer rates Nintendo's own performance profiles use. The console's own clocks come back
while the port is in the HOME menu and when it closes, and the port puts yours back after docking or sleep.
"Restore recommended settings" puts everything back to the defaults (the console's language, stock clocks,
no counter, full volume, the GPU renderer on its own thread).

## Status

> [!NOTE]
> **Test build.** It has not yet been played on a Switch: back up your SD card and use emuMMC.

| Part | State |
|---|---|
| The game's code on the Switch's processor | ✅ Recompiled and compiled; runs into the city on the ARM test build |
| Reading your copy: disc images and files | ✅ All formats checked byte for byte against the extracted files, on PC and ARM |
| Starting on a Switch | 🧪 It finds the game and starts it; it froze right after in 0.0.3. 0.0.4 and later fix the causes found and log enough to find the next one |
| Picture | 🧪 The Switch's GPU through OpenGL, every frame; matches the CPU renderer on the PC test build. Untested on a console |
| Menus, cinematics, demos, the city | ✅ On the PC test build |
| Sound | 🧪 Through the Switch's speakers and headphones; untested on a console |
| Controls | 🧪 Mapped, untested on a console |
| Saves | 🧪 In `hdd/` on the SD card; untested on a console |
| Language | ✅ The console's: English, Spanish, French, German or Italian texts (voices in English, as on the disc); on the PC test build |
| Debug menu, clocks, counter | 🧪 Drawn and checked on the PC; the clocks only take effect on a console |
| Online | ❌ None: the port doesn't use the network |

**Known issues**
- How fast it runs on a Switch is not known yet: the first tests will tell (the debug menu's counter shows it).
- In Spanish, French, German and Italian the voices are in English: the disc only has the English ones.
- Only the release above is supported: other releases need their own recompiler config.

See the [changelog](CHANGELOG.md) for what changed in each version.

## Help wanted: testers

The port is tested on a PC build that runs the same recompiled code and on the ARM build in an emulator.
What's missing is real consoles. Reports in English or Spanish help most, with the `logs` folder:

| What | How to help |
|---|---|
| 🎮 **Does it start?** | Build it, start it, and tell what the screen shows. A photo of the start screen says how far it got. |
| 🧊 **Freezes** | If the console freezes, take a photo, restart, and send the `logs` folder and the newest files in `atmosphere/crash_reports/`. Then try once holding **L** at start (no sound), and once holding **R** (the CPU renderer). |
| 🎨 **The picture** | A photo of anything drawn wrong, and the `logs` folder: its `perf:` lines say how fast it runs and where the time goes. |
| 💿 **Your image format** | If your `.cci`, `.cso` or split image isn't accepted, send the log: it says why. |

Open an [issue](../../issues/new/choose): the form asks for everything needed.

## FAQ

<details>
<summary><b>Is this an emulator?</b></summary>

**No, at least not for the game's code.** An emulator reads the game's x86 instructions one by one while it
runs (or translates them on the fly). This port uses **static recompilation**: every function of the game's
`default.xbe` is translated to C++ once, on your computer, and that C++ is compiled into a normal Switch
program. The Switch then runs the game's logic as its own code.

What the game asks the Xbox for (the kernel, files, threads, Direct3D 8, DirectSound, the controllers) is
answered by the port's own code, written for the Switch. The picture works much like a modern emulator's
graphics: the game's Direct3D commands for the Xbox's GPU are turned into OpenGL for the Switch's GPU, with
its shaders translated. See [How it works](docs/TECHNICAL.md).
</details>

<details>
<summary><b>Why is there no ready-to-play download?</b></summary>

Because the `.nro` *is* the game's code, translated to the Switch's processor. Sharing it would be sharing
the game. So the releases hold the port's source and the builder, and you make the `.nro` from your own copy
on your computer. It's one command, and the builder checks your copy first. Keep the `.nro` you make for
yourself.
</details>

<details>
<summary><b>Does it work with the PlayStation 2, GameCube or PC version?</b></summary>

No. Those are different programs: each version of *Spider-Man 2* (2004) was built for its own console, and
the PC version is a different game altogether. This port runs the **original Xbox** version only. The newer
*Spider-Man* games (for example *Marvel's Spider-Man 2*, 2023) are unrelated.
</details>

<details>
<summary><b>Which release of the Xbox game works?</b></summary>

The one whose `default.xbe` has the SHA-1 `853bcb253c5baea2d7e2d9b59e5888b4c87bb402` (title ID `4156002B`,
version 4), in English. `build.sh` checks it and tells you if yours is different. The recompiled code
matches that one file exactly; another release (another region, for example) would need its own recompiler
config. If you have another release, an issue with its SHA-1 (the builder prints it) helps.
</details>

<details>
<summary><b>Where do I get the game files?</b></summary>

From your own disc. This project can't help with that, and issues asking for game files will be closed.
[Getting your game](docs/INSTALL.md#getting-your-game) explains the usual ways to make a copy of your own disc.
</details>

<details>
<summary><b>Which disc image formats work?</b></summary>

- **`.iso` / `.xiso`**: a trimmed XISO (what `extract-xiso` makes) or a full disc dump (the video partition
  first, as Redump's are).
- **`.cci`**: the compressed images Repackinator makes for Cerbios.
- **`.cso`**: the compressed images (version 2, LZ4) Repackinator and `ciso` make for StellarOS.
- Any of them **split** into parts (`name.1.iso`, `name.2.iso`, ... or `.1.cci`, `.1.cso`): put all the parts
  in the folder.
- Or the **files** from the disc: `default.xbe` and the `data` folder.

Not read: `.zip`, `.7z` or `.rar` archives (extract them first), Xbox 360 images, and the old zlib CSO
(version 1) made for the PSP.
</details>

<details>
<summary><b>My SD card is FAT32 and my image is over 4 GB</b></summary>

FAT32 can't hold a file over 4 GB. Use a split image (Repackinator splits by itself, and the port reads the
parts as one), a compressed `.cci` or `.cso` (this game fits in about 1.5 GB), or the disc's files. Or format the card as exFAT.
</details>

<details>
<summary><b>Does it run at full speed?</b></summary>

Not known yet: the builds that draw with the Switch's GPU haven't been played on a console yet. The debug
menu's frame-rate counter shows it on screen, and if it stays under 30 you can raise the CPU clock there
(up to 1785 MHz, a rate Nintendo's own profiles use). Every 5 seconds the log has a `perf:` line with the frame
rate and where a frame's time goes, so a test run tells what to speed up next.
</details>

<details>
<summary><b>Can I play in Spanish?</b></summary>

Yes. The game takes the console's language: set to Spanish (Spain or Latin America), French, German or
Italian, its menus and subtitles are in that language, from the texts on your disc. The voices stay in English,
as the disc only has the English ones. To choose another language than the console's, use the debug menu
(**ZL + ZR + −**), then start the port again.
</details>

<details>
<summary><b>Does the port go online? Can I get banned?</b></summary>

The port never connects to the internet. Running any homebrew needs custom firmware, which is against
Nintendo's terms and can get a console banned from Nintendo's online services. Running homebrew on an emuMMC
and keeping your SD card backed up is the usual advice.
</details>

<details>
<summary><b>Where is my save, and how do I back it up?</b></summary>

The port keeps an Xbox hard disk in `switch/SpiderMan2Xbox/hdd/`, and the game saves there as it would on the
Xbox. Copy that folder somewhere safe from time to time.
</details>

<details>
<summary><b>Can I build it on Windows without WSL, or on a Mac?</b></summary>

The builder is a Linux shell script, and WSL is the easy way on Windows: [step by step](docs/INSTALL.md#on-windows-with-wsl).
devkitPro also runs on macOS, but the builder hasn't been tried there.
</details>

## How it works

The game is the x86 program `default.xbe`, made for the Xbox's Pentium III and its NV2A graphics chip. The port:

1. **Recompiles the game's code ahead of time** with the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp)
   SDK's `xbe_recomp`: every function becomes C++, compiled for the Switch's ARM CPU with devkitA64.
2. **Rebuilds the Xbox's system around it**: the kernel's 120 functions the game imports, threads and
   timers, files (the disc's or the disc image's), Direct3D 8 with its push buffers and vertex programs,
   DirectSound and the controllers, as native code.
3. **Fits the Xbox's 4 GB address space** into the memory a Switch homebrew program can have, in 256 MB slices.
4. **Draws** the game's Direct3D commands with OpenGL on the Switch's GPU (Mesa's driver, from devkitPro): the
   vertex programs and pixel combiners become shaders, the textures are decoded from the game's memory once.

More in [docs/TECHNICAL.md](docs/TECHNICAL.md): the recompiler, the Switch layer, the disc image formats, the
test builds and the source layout.

## Building from source

`build.sh` does everything: it builds the PC tools, recompiles your `default.xbe` and builds the `.nro` with
[devkitPro](https://devkitpro.org/wiki/Getting_Started)'s `switch-dev` tools and `switch-mesa` (OpenGL):

```sh
bash build.sh "/path/to/your/Spider-Man 2.iso"     # or the game's folder, or default.xbe
```

The result is `build-switch/SpiderMan2Xbox.nro`, laid out for the SD card in `out/sd/`.
[docs/BUILDING.md](docs/BUILDING.md) has the details, the PC test build used for development, the tests, and
how a release is made.

## Credits

Standing on the shoulders of others:

- **Treyarch** and **Activision**, who made the game. Please support the original.
- [**Xbox XBE Recomp**](https://github.com/TGP482/XboxXBE-Recomp) by TGP482: the recompiler and the Xbox runtime
  this port is built on (in `sdk/`, with the changes listed in [docs/TECHNICAL.md](docs/TECHNICAL.md)).
- [**Zydis**](https://github.com/zyantific/zydis) (x86 decoding) and [**toml++**](https://github.com/marzer/tomlplusplus),
  used by the recompiler.
- [**XbSymbolDatabase**](https://github.com/Cxbx-Reloaded/XbSymbolDatabase), [**Cxbx-Reloaded**](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded)
  and [**xemu**](https://github.com/xemu-project/xemu): finding the XDK's functions, and the reference for how the
  Xbox and its GPU behave.
- [**libnx**](https://github.com/switchbrew/libnx) and the switchbrew community, [**devkitPro**](https://devkitpro.org)
  (devkitA64, switch-tools, and the Switch port of [**Mesa**](https://mesa3d.org) the GPU renderer draws with) and
  [**Atmosphère**](https://github.com/Atmosphere-NX/Atmosphere).
- [**sys-clk**](https://github.com/retronx-team/sys-clk): the clock rates of the Switch's performance configurations,
  which the debug menu's clock settings keep to.
- The disc image formats: [**extract-xiso**](https://github.com/XboxDev/extract-xiso),
  [**Repackinator** and XboxToolkit](https://github.com/Team-Resurgent/Repackinator) by Team Resurgent (CCI), and
  [**ciso**](https://github.com/antangelo/ciso) by antangelo (CSO), whose formats the port reads; LZ4 by Yann Collet.

Full list with licenses: [THIRD_PARTY.md](THIRD_PARTY.md).

## License

The port's own code (`switch/`, `gl/`, `src/`, `tools/`, `config/`, `build.sh`) is under the [MIT License](LICENSE). The
SDK in `sdk/` keeps its own MIT license ([sdk/LICENSE](sdk/LICENSE)), and the other third-party code its own
([THIRD_PARTY.md](THIRD_PARTY.md)). The game is not included and is not covered by these licenses in any way.

## Disclaimer

This is an unofficial fan project. It is not affiliated with, endorsed or sponsored by Activision, Treyarch,
Marvel, Sony Pictures, Microsoft or Nintendo. *Spider-Man* is a trademark of Marvel; Xbox is a trademark of
Microsoft; Nintendo Switch is a trademark of Nintendo. No game code or data is distributed here: you need your
own copy of the game, and what you build from it is for you only.

Running homebrew requires custom firmware, which is against Nintendo's terms of service and can get a console
banned from online services; that decision is yours. The software is provided as is, without warranty (see
[LICENSE](LICENSE)).

---

<div align="center">

A project by **SantiBros** · If you like it, star the repo ⭐ and share it

</div>
