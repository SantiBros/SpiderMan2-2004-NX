# How the port works

[← Back to the README](../README.md)

*Spider-Man 2* for the original Xbox is one x86 program, `default.xbe`, written for the Xbox's 733 MHz Pentium III,
its NV2A graphics chip (a relative of the GeForce 3) and Microsoft's XDK libraries, version 5849. The port turns
that program into a Switch program in two steps: the game's code is **recompiled ahead of time** to C++, and the
**Xbox's system is rebuilt** around it as native code. This page explains both, the Switch layer, the disc images,
and the test builds.

- [Pipeline](#pipeline)
- [The recompiler](#the-recompiler)
- [The runtime: the Xbox rebuilt](#the-runtime-the-xbox-rebuilt)
- [The picture](#the-picture)
- [The Switch layer](#the-switch-layer)
- [Disc images](#disc-images)
- [What the Switch does differently](#what-the-switch-does-differently)
- [Test builds](#test-builds)
- [Changes to the SDK](#changes-to-the-sdk)
- [Supporting another release](#supporting-another-release)
- [Source layout](#source-layout)

## Pipeline

```
your default.xbe ──► xbe_recomp ──► generated/game/*.cpp ──┐
  (config/game.toml: addresses and names)                   ├─► devkitA64 ──► SpiderMan2Xbox.nro
sdk/runtime (the Xbox rebuilt) + src/ + switch/ ────────────┘
your disc image or files ──────────────────────────────────────► read at run time from the SD card
```

Only `default.xbe` is recompiled. The game's data (`data/`: packs, sound banks, movies) is read at run time,
from your disc image or folder, exactly as the Xbox reads it from the disc.

## The recompiler

[Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482 (in `sdk/`) supplies `xbe_recomp`. It
decodes the XBE's code sections with Zydis, finds the functions (direct calls, pointers found in the data, gaps
filled, C++ exception handlers), and writes each function as a C++ function that works on an `X86Context` (the
registers) and the guest memory:

- 26,876 functions, 1.37 million x86 instructions, in 128 source files (`partitions = 128`).
- x87 floating point is kept in `double`s, SSE in 128-bit unions; flags are computed only where something reads them.
- Calls between functions are C++ calls; calls through pointers go through a table from guest address to function.
- Every recompiled function is a *weak* symbol: a host function with the same name replaces it. That's how the
  port's code takes over the XDK's functions (Direct3D, DirectSound, XAPI) and fixes the game where needed.

[`config/game.toml`](../config/game.toml) tells it where things are in this `default.xbe`: the XDK functions the
host replaces (found by `xbe_symbols` from the XDK's signatures, and a few by hand) and data addresses such as the
Direct3D device's streams. It holds addresses and names only, nothing of the game itself.

## The runtime: the Xbox rebuilt

`sdk/runtime/` is what the game runs on:

| Part | What it does |
|---|---|
| `kernel/` | The 120 kernel functions the game imports (one, `IoQueryVolumeInformation`, is avoided by replacing the XAPI function that calls it): threads, events, semaphores, timers, DPCs and APCs, the file system over a host folder or a disc image, the memory manager, the hard disk under `cache/` with the title's saves in `save/`, the EEPROM settings (language 1, English). |
| `kernel/memory.cpp` | The guest's 4 GB address space. On the PC it is reserved whole. On the Switch, where homebrew can't reserve that much with pages committed on demand, the address space is cut into 16 slices of 256 MB, and the used ones (code and heap, physical memory and its mirror, the system area, the stacks) sit in one 1 GB block through a table. The generated code goes through the table (`XBE_SEGMENTED`). |
| `xdk/d3d8.cpp` | Direct3D 8 as the XDK has it: the device, resources, render states, and the push buffer the game writes NV2A commands into, read back as draws. This game draws mostly with vertex programs selected by address and with recorded push buffers (`RunPushBuffer`), both done on the host. |
| `gpu/` | The draws as the backend gets them: vertex formats, textures (swizzled, compressed), the shader translator (vertex programs and register combiners). |
| `xdk/dsound.cpp` | DirectSound: voices, buffers and streams mixed on the host, with the effects image the game loads. |
| `xdk/xapi.cpp` | The controllers, and XAPI's file and save functions. |

## The picture

Two backends take the draws:

- The **GPU renderer** ([`gl/`](../gl)), what the Switch draws with: OpenGL 4.3 core through Mesa's nouveau
  driver (devkitPro's `switch-mesa`), on Linux through any Mesa driver for the test build. It follows the SDK's
  Direct3D 12 renderer, its render scale too. Console surfaces become GL textures the draws render into, read back to
  guest memory when the CPU locks them and uploaded again when the CPU writes them; textures are decoded from guest
  memory (swizzled, S3TC, palettes) and kept while their bytes stay the same. The shader translator's vertex
  programs, register combiners and fixed function stages come out as GLSL ([`gl/glsl.cpp`](../gl/glsl.cpp), carried
  over line for line from the HLSL). The game's own vertex bytes go to the GPU as they are; the input assembler
  converts them (NORMPACKED3 is unpacked in the shader). Vertices, indices and constants go through persistently
  mapped buffers fenced in quarters, and a draw changes only the GL state that differs from the last one. It runs
  on a render thread of its own beside the game's (the SDK's `gpu/render_thread.cpp`), and logs a `perf:` line
  every 5 seconds with where a frame's time goes.

  Frames are shown at the pace the game asks for with `D3DRS_PRESENTATIONINTERVAL`: a frame every two vertical
  blanks, 30 a second, which is also what its movies are timed for. Every GLSL program the game needed is kept in
  `cache/shaders.cache` beside the `.nro` and built again at the next start, a little each frame, so a scene played once
  doesn't stall the next time on shaders it already had (the Switch's Mesa keeps no shader cache of its own). The
  list is headed by the GL driver and the shader generator's version (`kShaderGenerator`, changed whenever any
  program's text does), so a new port version that makes the same GLSL keeps it. A
  depth buffer the game reads as a colour texture (its shadow pass looks up `LIN_A8R8G8B8` texels over the depth
  target) is copied into a colour view of its 24-bit words first.

  Two hardware rules the game's shadows depend on: with `D3DRS_MULTISAMPLEMODE` at 2X or 4X the GPU takes the target
  as two or four samples a pixel, so the game's screen space (positions, viewport, scissors, clear rects) is half as
  wide (and high), and the game uses that to fill its stencil shadow volumes at half the pixels with every sample
  depth tested: `gpu::State::aa_x`, `aa_y` scale it back. And an element offset of a whole number of vertices (a
  shadow volume placed further into a shared buffer) moves into the buffer's start, since GL takes element offsets up
  to 2047 bytes only.
  **Render scale.** With the debug menu's Resolution at 720p or 1080p every target at least half the frame's size
  each way (the frame, its depth buffer, copies of it) is made that much bigger alike (the guest size times one
  scale across and one down: 1.5 for 720p, 2.25 for 1080p, more across for the 16:9 picture); smaller ones (the
  game's 256×256 shadows and glow passes) stay at the console's size, their pixels hardly showing and their cost
  growing with the scale. 1080p is drawn in handheld mode as 720p, the screen's own. What is in guest pixels is
  scaled with the target: viewports, scissors, clear rects, copies, the slope of
  the depth bias, point sizes, the half pixel render target reads move back by (`ext.xy` in the shaders), and the
  pixels a visibility test counts (divided back to the console's, by each query's own target). The game's own screen space stays 640×480 in the
  shaders' `surf` constant. CPU writes into a target go in at the console's size and are drawn bigger; read-backs
  are brought down first. A pass shrinking an upscaled target (the glow chain: the frame into 256×256 targets)
  averages each tap over a box of console texels, as the D3D12 renderer does, or its taps skip texels and flicker.
  A change in play makes every target again at the new size, its contents drawn over. A texture's change check, once
  a frame for every texture drawn with, hashes its bytes with the processor's CRC32C instructions (four chains, eight
  bytes a cycle on the Cortex-A57).

  **Presenting.** The frame goes to the 1280×720 screen through the gamma ramp: bilinear, a box of bilinear taps
  when it is drawn smaller (1080p), Catmull-Rom when it is enlarged and Upscaling is Sharp (movie frames, the YUY2
  textures, are sampled through Catmull-Rom in the draw too). A 4:3 frame's sides can be the frame itself, zoomed to
  the screen's width, from a small mip level of a copy, blurred and dimmed, or a spider web drawn in the shader, in
  grey on dark red and blue or in the suit's colours (raised black webbing on red, a seam, blue beyond); a web is
  drawn once for the screen's size and copied to the bars each frame. Anisotropic filtering goes on every
  mipmapped linear sampler when asked for.

  **The game's 16:9 picture.** The engine has 16:9 display modes the retail game never lists (its start-up code
  fills the mode table with the 4:3 ones only), and its camera takes an aspect: 16:9 there keeps the horizontal
  field of view and shows less height. [`src/game/widescreen.cpp`](../src/game/widescreen.cpp) sets the camera's
  aspect to 16:9 and widens its field of view so that it shows 4:3's height (the game's culling cone widens with
  it), before the projection is made (`0x00343930`); the field of view the game reads back is the one it set. The
  640×480 frame is then shown across the 16:9 screen (and drawn 1280×720 or 1920×1080 when scaled). What the game
  draws flat, every vertex at w exactly 1 (the HUD, menus, text), is narrowed to 4:3 in the middle in the vertex
  shader. Which draws: a small draw's box on screen is worked out on the CPU through the reference renderer's vertex
  stage (`reference::ScreenBox`, which stops at the first vertex in perspective; a draw found in perspective is
  remembered by its program, buffers and range for 30 frames, as most small draws are 3D: about 0.3 ms a frame on the
  PC test build instead of 11), and one from the top to the bottom, or across the width and touching the top or
  the bottom, stays across (full screen passes, the shadows' darkening drawn in halves, fades, bars) unless it shows
  a picture (a menu's layers, the logo); a bigger draw is narrowed vertex by vertex but for those right on the left
  and right edges (at 0 or the width, or half a pixel off: full screen passes), those past the edges included, so
  text placed partly off the 4:3 screen keeps its shape. Movie frames are narrowed too, but those letterboxed for the
  cinema (the cutscenes: black bands across the top and bottom eighth of every frame, a picture in the middle; each
  movie texture is looked at as it changes) fill the width instead: drawn across the whole screen with a GL viewport
  4/3 as tall, their bands off the screen. What the game places at a
  point of the world through its own world-to-screen function (`0x00343FF0`: the city map's icons, district names
  and lines, the markers over the city in play) is drawn flat too, so it would be narrowed after the wider
  projection had already put it nearer the middle: the hook widens that function's x by 4/3 about the middle when
  the camera is the widened one, and the narrowing puts it back on its point. A view drawn into part of the frame,
  the minimap in the bottom right corner, is 3D and so shown where its rectangle is across the 16:9 screen, while
  its pins and its N are placed through WorldToScreen into that rectangle and drawn flat, a quarter of their
  distance nearer the middle: the view's screen scale and offset (`0x003443A0`, made from its rectangle for its
  vertex shaders) are made from the rectangle moved by that quarter, so the map lands under its pins, the rectangle
  itself left as it is for what the game places in it.

  **Its close view** (`app::WideClose()`) is the engine's own 16:9 instead: the camera's aspect set to 16:9 and its
  field of view left as the game set it, across the width, so what it shows is a third bigger and a little less is
  seen above and below. What is drawn flat is narrowed by 0.75 × 1.1 and made 1.1 times taller about the middle
  (`Picture::hud`; the vertex shader scales the height by the narrowing over 3/4), menus' pictures from the top to
  the bottom included, so a menu's art and its logo keep their places; movies stay 0.75 and their height. The views
  in part of the frame (the minimap) keep the wide view's camera, so they go with the HUD: their rectangle is moved
  and enlarged as the HUD is (its middle nearer the screen's by 0.75 × 1.1 across, 1.1 down, its size 1.1 times),
  and WorldToScreen's points in a full-frame view are moved by the opposite of the flat drawing (x by 1/(0.75 × 1.1),
  y by 1/1.1 about the middle), so the map icons and markers land on their points.

  The shader list of an earlier version is kept when the GLSL changes: `glsl::ListEdits(generator)` carries the
  changes since that list's shader generator as text replaced in its programs. The list is named by the shader
  generator's version only (`sm2nx glsl list 2, shaders N`): the GLSL is the same whatever the driver, so a port
  built with another Mesa keeps it.
- The CPU **reference renderer** ([`sdk/harness/reference_renderer.cpp`](../sdk/harness/reference_renderer.cpp)),
  which draws into guest memory in software, register combiners and vertex programs included. It is exact enough
  to compare against and slow (seconds per city frame). The test build uses it, and the Switch falls back to it
  (a frame drawn every so often while the game runs at its own speed) when GL doesn't start, or with R held at
  start.

## The Switch layer

`switch/` is the program around the runtime on the Switch (libnx):

| File | What it does |
|---|---|
| `main.cpp` | Checks the launch mode (title takeover), loads `settings.ini`, reads the console's language, finds the game (`kernel/game_source`), asks before a different release, mounts the disc image, boots the game, and keeps the applet alive, running the debug menu. Until the game draws, the start screen says "Loading..." (or, with the start-up log on, shows the newest log lines). Quitting (+ and −) takes no lock the game could hold. |
| `screen.cpp` | The 1280×720 window. With GL (the default): an EGL window surface and an OpenGL 4.3 core context; the main thread draws the text screens (libnx's console font, as an image) until the renderer takes the context, then the renderer puts the status text over the frames until the game draws its own picture. Without: libnx's framebuffer, presented by the main thread, with the reference renderer's frames scaled to the screen's height. Two overlay layers (the frame-rate counter and the debug menu), RGBA pictures the main thread draws on the CPU, are blended over every frame: by the renderer's thread with `glr::DrawOverlay` after the frame, or into the framebuffer. |
| `menu.cpp` | The debug menu (ZL + ZR + −): its own pad state on the main thread, so it works whatever the game does; the game's controller reads nothing while it is open, nor the − of the combination, nor the buttons that closed it until they are let go. The touch screen picks rows and steps values. The frame-rate counter is counted from the frames presented, every half second. |
| `settings.cpp` | `settings.ini` beside the `.nro`: language, voices, resolution, screen, upscaling, texture filtering, vibration, buttons, clocks, counter, volume, renderer, render thread, start-up log; written whole (a new file, then renamed) at every change. The buttons held at start override it for that start. The picture settings reach the renderer's thread and the game's through atomics (`state.h`) and change in play. |
| `clocks.cpp` | CPU, GPU and memory clocks through `clkrst` (`pcv` before 8.0.0), as the NFS Most Wanted port does: only the rates of Nintendo's performance configurations, the configuration's own clocks found through `apm:sys`, put back twice a second after the system resets them (docking, sleep, performance mode changes), given back while out of focus (the focus mode notifies the port of the HOME menu) and at exit; while custom clocks are in use the exit is locked so that closing from HOME first restores them. |
| `backend.cpp` | The GPU renderer's host (the window, the context, the swap), falling back to the reference renderer when GL doesn't start; the reference renderer paced for the Switch (60 frames a second, a frame drawn when as long as the last drawing has passed, at least half a second). Held at start: R the reference renderer, ZR the GPU renderer on the game's thread (no render thread). |
| `audio.cpp` | audout at 48 kHz, four 20 ms buffers filled by DirectSound's mixer, at the settings' volume; L at start turns it off. |
| `input.cpp` | The Xbox controller from the Switch's, mapped by position; nothing while the debug menu has it. |
| `rumble.cpp` | The game's rumble (`XInputSetState`'s two motors) on the HD rumble of a Pro Controller, two Joy-Con or the handheld Joy-Con: the heavy left motor as the low band (160 Hz), the light right one as the high band (320 Hz), at the settings' strength. A request left unchanged for 2 s fades out over 0.6 s; nothing is sent while the debug menu is open or the port is out of focus, and the motors stop at quit. |
| `saves.cpp` | The copy of `save/` in `sdmc:/config/SpiderMan2Xbox/save/`: kept up to date a few seconds after the game saves and at quit, put back at start when `save/progress` has no saves, and the saves found listed in the log. |
| `watch.cpp` | Two threads above the game's priority: one writes the log to the SD card every 250 ms, the only thread that does (a reset console loses cached writes), one logs a status line every 2 s with each game thread's current kernel or library function. |
| `crash.cpp` | The exception handler: registers, the module offset of the crash, return addresses from the stack and the game thread's current function, into `logs/crash.txt` and the run's log. |
| `pthread_detach.cpp` | `pthread_detach`, which libnx doesn't have (below). |

## Languages and editions

One `default.xbe` serves the game's three editions, told apart by their disc's `game.ini` (read at `0x001F3490`):
`SKU=Xbox-E` English, `SKU=Xbox-Multi` French, German or Spanish (the Xbox's language setting, `XC_LANGUAGE` read
with `ExQueryNonVolatileSetting`, picks one: German, Spanish, and French for any other) and `SKU=Xbox-It` Italian. The
discs hold the text of all five languages (`packs/`), so [`src/game/language.cpp`](../src/game/language.cpp) asks the
file layer for a `game.ini` naming the edition of the text's language (`kernel::RewriteDiscFile`: the copy is made at
the first open, in `cache/disc/`, and opened in its place; none when the disc's already names it), and the kernel
answers that language (`kernel::g_language`, set from the Switch's system language, or the settings').

The voices are in sound banks of their own for each language (`NAME_EN.WBK`, `NAME_FR.WBK`, ...), their lip sync in
the pack for all five. The US disc has only the English banks; the European discs have French, German and Spanish, or
Italian. The game names the banks and the lip sync from its language (`0x0021B2D0` the two letters, `0x0021B320` the
whole name); the port's hooks answer the voices' language instead: the one set in the debug menu, or with Auto the
text's when the copy has its banks (`STREAMS_VOICE_<XX>.WBK`), else the copy's own (English, French, then Italian
first). A bank still missing is opened in another language's place, and a movie only one edition has in the other's
(`NAME.XMV` and the 50 Hz `NAME_PAL.XMV`) (`kernel::g_missing_disc_file`), as the game would otherwise find no file.

| Switch | Xbox (`XC_LANGUAGE`) |
|---|---|
| English (US, UK) | 1 English |
| Spanish (Spain, Latin America) | 5 Spanish |
| French (France, Canada) | 4 French |
| German | 3 German |
| Italian | 6 Italian |
| any other | 1 English |

## The locked .nro and the installer page

The release's `.nro` is encrypted with AES-256-GCM ([`tools/lock_nro.py`](../tools/lock_nro.py)), so only someone
with the game can make it. Up to 0.3.1 (lock version 1) the key is the SHA-256 of a label and the whole `default.xbe`
of the supported release. From 0.4.0 (version 2) it is the SHA-256 of another label and the xbe's **game code**
([`sdk/runtime/xbe/code.h`](../sdk/runtime/xbe/code.h): each section the title loads, with its address and sizes,
then the entry point and the kernel imports' address; not the header and certificate, which name the edition's
regions, title and signature), so another edition built from the same code opens it too. The locked file is a 64 byte
header (`SM2NXLK1` or `SM2NXLK2`, the 12 byte nonce, the `.nro`'s size, the SHA-1 of the `default.xbe` or of the game
code it needs; all of it authenticated) and the ciphertext with GCM's tag.

The [installer page](../installer/index.html) is one HTML file served by GitHub Pages beside a folder for each
version (its locked `.nro` and the SD card folder's text files) and `versions.json`, which lists them. The player
picks the version, the newest unless they choose another (or the address names one: `?v=0.2.3`). In the browser,
with no upload: it finds `default.xbe` in what the player chooses (the file
itself, or inside an XISO, a full disc dump, a CCI or a CSO, split or not: the same formats as the port, read with
`File.slice` so a 1.7 GB image is never loaded whole), checks its SHA-1 or its game code's (another edition, for
the versions locked with it, listed with `lock: 2` in `versions.json`), unlocks the `.nro` with WebCrypto and
writes `SpiderMan2Xbox-SD.zip` (stored, no compression) with the `switch` folder for the SD card.

## In-app updates

The debug menu's **Update** row ([`switch/update.cpp`](../switch/update.cpp)) does on the console what the
installer page does in a browser. It asks GitHub's API for the repository's releases and takes the newest that has a
`SpiderMan2Xbox-<version>.nro.locked`, newer than the running port's version. That file is downloaded from the
release's own download link, as a browser would, so GitHub counts it among the release's downloads. It is unlocked
as it arrives, AES-256-GCM in pieces ([`switch/update_core.cpp`](../switch/update_core.cpp): AES from libnx, GHASH
with 4-bit tables), with the key worked out at start from the game code of the game's `default.xbe` (lock version 2:
the US edition's, or another edition's of the same code).
The unlocked `.nro` goes to `SpiderMan2Xbox.nro.new` and only once GCM's tag is right does it take the running
`.nro`'s place, the old one renamed `SpiderMan2Xbox.nro.old`. A again quits as usual and asks the Homebrew loader to
start the new one (`envSetNextLoad`). The connection is the Switch's own TLS (the `ssl` service, with its trusted
certificates and host name check), over libnx's sockets, after `nifm` says the console is online; nothing of it is
started until the row is used. The HTTP, the release lookup and the unlocking are tested on a computer:
`tools/test_update.sh <default.xbe>`.

## Disc images

[`kernel/image.cpp`](../sdk/runtime/kernel/image.cpp) reads the containers an Xbox image comes in, and
[`kernel/disc.cpp`](../sdk/runtime/kernel/disc.cpp) the Xbox file system (XDVDFS) inside:

| Container | Layout |
|---|---|
| Plain (`.iso`, `.xiso`) | The image as it is, in one file or several (`name.1.iso`, `name.2.iso`, ... read as one). |
| CCI (`CCIM`) | Header of 32 bytes (size, index offset, block 2048, version 1, index alignment 2), the sectors, then an index of 32-bit entries: position >> 2, bit 31 when LZ4 compressed. A compressed sector is a byte with the padding count, the LZ4 block and the padding. A split CCI is several whole CCI files whose sectors follow each other. As Team Resurgent's XboxToolkit writes it. |
| CSO version 2 (`CISO`) | Header of 24 bytes (size, block size, version 2, alignment), the index right after it (bit 31 when compressed), then the sectors. A compressed sector is the data block of an LZ4 frame: its 32-bit size (bit 31 when stored) and the block. A split CSO is one file cut at byte positions. As antangelo's `ciso` writes it. |

Consecutive sectors are read in one go (up to 4 MB) and then decoded, so a large read of the game costs one read of
the SD card, not one per 2 KB sector. The LZ4 decoder is the port's own, bounds-checked on every step.

Inside, XDVDFS starts at the image's beginning (an XISO) or at `0x18300000`, past the video partition (a full disc
dump of an original Xbox disc). Images with the file system at the Xbox 360's offsets are named as such.
[`kernel/game_source.cpp`](../sdk/runtime/kernel/game_source.cpp) looks in `game/` and beside the `.nro` for the
disc's files and for images (a split one counted once), and picks the copy whose `default.xbe` matches the build's
SHA-1 or its game code's SHA-1 (another edition of the same code: the build writes it, `kImageCode`), then one with
the same entry point; every file it can't use is listed with the reason. The log names each copy's game code, title
version and regions.

## What the Switch does differently

Things the PC test build couldn't show, found on the console or by reading libnx, and how the port handles them:

- **No `pthread_detach`.** libnx leaves newlib's detach hook unimplemented, so `std::thread::detach` throws
  `std::system_error` ("Function not implemented"). The port supplies the hook: detached threads go to a reaper
  that joins them when their kernel handle signals.
- **Asynchronous I/O is still in progress when the call returns.** On the console a read or write to the hard
  disk through a handle opened without `FILE_SYNCHRONOUS_IO_*` returns `STATUS_PENDING`, and the title collects the
  result later (`GetOverlappedResult`). The port does the transfer at once but returns `STATUS_PENDING` all the same,
  with the status block, the event and the file already set. Spider-Man 2 reads its saves (opened that way, and
  the only files it opens that way) 512 bytes a frame, and takes a read that completes at once for the end of the
  file: before 0.2.3 it stopped after 1 KB and called the save damaged. The disc's files, and reads with a
  completion routine, still complete as before. A rename over an existing file removes it first, as the Switch
  refuses to rename over one.
- **Saves are flushed and copied.** A file written in the save folders is `fsync`ed when it closes, and the files
  open for writing when the port quits (`kernel::FlushOpenFiles`). `switch/saves.cpp` keeps a copy of `save/` in
  `sdmc:/config/SpiderMan2Xbox/save/`, made 4 seconds after the last change to the saves (`kernel::SaveChanges`)
  and at quit, never while `save/progress` has no save game, and put back at start when `save/progress` has none.
- **No second open of a file open for writing.** The Switch's file system refuses it (and a rename of an open
  file), where Windows and Linux allow it, and the game opens its packs more than once. Existing files open
  read-only unless the game asks to write (never on the disc), renames close the file around them, and resizes go
  through the open file. Conflicting opens are logged.
- **Cached writes are lost on a reset, and a write to the card is slow.** Once the watch thread runs, a thread
  that logs only adds its line to a buffer in memory (`logx::Background`), and the watch thread writes it to the card
  every 250 ms, when lines were added since (`fwrite`, `fflush`, `fsync`): no game thread waits for the card, warnings
  included. A card slow for long fills the buffer up to 1 MB, and the lines after that are counted. A message
  logged more than 100 times in 10 s (its format string) is counted for the rest of them, and a line says how many
  were left out; errors are always written. A crash writes the buffer by the log's descriptor before its report.
  Before 0.2.2 every line was flushed by the thread that logged it, and 16:9's camera line, logged several times a
  frame, made the game wait for the card: a tester's 0.2.1 log shows the game taking 45 ms a frame instead of 7.5,
  and stopping for up to 4 s. A frozen console still leaves the log behind.
- **Cores.** libnx's pthreads start on the process's default core, core 0, with the process's whole core mask
  (cores 0 to 2), so the scheduler moves a thread to an idle core. `kernel::NameHostThread` gives the renderer core 1
  and the sound and the port's main and watch threads core 2 as their preferred cores, the game's threads core 0.
- **The scheduler only time-slices priority 59 (0x3B) on cores 0 to 2,** libnx's default for threads; the game's
  threads stay there, and the port's watch threads sit above them.
- **Float to integer conversion** out of range gives `INT_MIN` on x86 and saturates on ARM. The recompiled code
  reproduces x86 exactly; the reference renderer now does too, and clips triangles at the near plane instead of
  leaving corners at screen positions in the billions (which x86 skipped by accident and ARM drew as smears).
- **Plain `char` is unsigned on ARM.** The generated code never uses it.

## Test builds

- **PC test build** ([`sdk/harness`](../sdk/harness/README.md)): the runtime and the game code on Linux, headless,
  with the reference renderer or the GPU renderer (`GL=1`, on Mesa), the Switch build's pacing (`SWITCH=1`) and
  status line (`PULSE=1`), scripted presses and frame captures. Most of the port's testing happens here.
- **ARM test build**: the same, compiled for AArch64 with the Switch build's memory layout, run under
  `qemu-aarch64`. The game reaches the city there too.

Both are described in [BUILDING.md](BUILDING.md).

## Changes to the SDK

The port's changes to the Xbox XBE Recomp SDK, kept in `sdk/`:

- `runtime/kernel/image.{h,cpp}` (new): the disc image containers (plain, split, CCI, CSO v2) and the LZ4 decoder;
  `kernel/disc.cpp` reads through it and names Xbox 360 images.
- `runtime/kernel/game_source.{h,cpp}` (new): the player's copy found in folders, matched by SHA-1, game code
  (`runtime/xbe/code.h`, new) and entry point.
- `runtime/kernel/files.cpp`: read-only opens unless written, renames and resizes around open files (above);
  `IoQueryVolumeInformation`, which the game imports, answers as `NtQueryVolumeInformationFile` does (it was missing, and
  a call would have stopped the game); `RewriteDiscFile` (a disc file the title layer rewrites, opened from a copy)
  and `g_missing_disc_file` (a disc file that isn't there opened as another), for the languages; `DiscFileExists`.
- `runtime/cpu/x86_ops.h`, `kernel/memory.cpp`: generated functions take guest memory's base pointer `__restrict`,
  and in the segmented layout reach every guest byte from it (`x86_seg_rel`, the slices' offsets from the base), so
  the compiler knows a guest store never writes the `X86Context`: the x86 registers stay in host registers across
  stores, and the generated code is about 5% smaller on AArch64. The backward jumps' compiler barriers still
  make spinning loops read memory again.
- `runtime/gpu/render_thread.cpp`: the state recorded with each draw (about 14 KB) compared a KB at a time before
  its 64 byte pieces, with NEON on AArch64.
- `runtime/xdk/d3d8.cpp`: a vertex program selected from the program slots is looked up by its key without
  building a `VertexShader` first (the game selects and loads programs many times a frame).
- `runtime/cpu/dispatch.cpp`: the continuation table a byte per address instead of `std::vector<bool>` (read on every
  indirect call).
- `runtime/kernel/kernel.h`, `exports.h`, `xdk/hle.h`: every kernel and library entry marks the calling thread with
  its name and counts its calls, for the status line and crash reports.
- `runtime/core/log.cpp`: the newest lines for the start screen, `Sync` (flush and `fsync`), the descriptor for the
  crash handler.
- `runtime/kernel/threads.cpp`, `boot.cpp`, `disc.cpp`, `memory.cpp`, `cpu/x86_ops.h`: the Switch's paths (16 MB
  thread stacks, segmented memory, packed-struct memory access for GCC, no `pread`).
- `runtime/kernel/xboxkrnl.cpp`: `MmQueryAddressProtect` answers from the committed pages (the game walks up its
  stack in 64 KB steps); `NtUserIoApcDispatcher` for ReadFileEx/WriteFileEx completions, with
  `RtlNtStatusToDosError`'s table shared as `StatusToDosError`.
- `runtime/gpu/vertex.{h,cpp}`: raw vertices a per-call choice (`Assemble(..., raw)`), so the GPU renderer reads the
  game's vertices as they are while the reference renderer has them converted; whole vertices of an element's offset
  go into the buffer's start (a buffer per stream and offset).
- `runtime/gpu/state.h`, `xdk/d3d8.cpp`, `gpu/d3d12.cpp`: the multisample mode's samples a pixel (`aa_x`, `aa_y`)
  for the back buffer and other targets, which every renderer scales the game's screen space by; the presentation
  interval passed on with `Backend::SetSwapInterval`.
- `runtime/gpu/backend.h`, `render_thread.cpp`: the render thread a runtime choice (`gpu::g_render_thread`), started
  with an 8 MB stack outside Windows (drivers compile shaders on it; `std::thread` gets 128 KB on the Switch).
- `runtime/kernel/objects.cpp`, `xboxkrnl.cpp`, `cpu/dispatch.cpp`, `xdk/d3d8.cpp`, `xdk/dsound.cpp`: every clock the
  title reads and every timeout it waits for goes through one guest clock (`kernel::GuestNs`), which the test build
  can run slower than real time (`TIMESCALE`) to record smooth clips.
- `runtime/xdk/d3d8.cpp`: the `RunPushBuffer` interpreter, and vertex programs by address (program slots,
  `LoadVertexShaderProgram`, `LoadVertexShader`, `SelectVertexShader`, `SetVertexShaderInput`);
  `xdk/5849/layout.toml` gains `CDevice_VertexShaderHandle` and `CDevice_VertexShaderStart`.
- `runtime/xdk/dsound.cpp`: host `IDirectSoundBuffer_SetMode`, `IDirectSoundBuffer_Pause`,
  `CDirectSoundStream_SetMode`, `CDirectSoundStream_SetAllParameters`, `CDirectSoundStream_SetRolloffCurve`.
- `harness/`: `SWITCH`, `PULSE` and `CAP` for the test build, the GPU renderer on headless EGL (`gl_harness.cpp`:
  `GL`, `RT`, `GLCAP`, `REFAT`, `GLDUMP`, `GLREC`), `TIMESCALE`, ARM builds (`ARCH`, `DEFS`, `GEN_OPT`, `LDFLAGS`),
  a game folder or disc image as the argument; the reference renderer clips at the near plane and converts floats
  as x86 does, takes `oFog` as the fog factor under a vertex program with table fog off, nudges pixel centres off
  shared edges, and traces a pixel through every triangle over it (`TRACE`, `DUMPTEX`, `DUMPTRIS`, `DRAWLIST`, `IDMAP`);
  the pad script has the right stick and every button, and `cmd` and `capnow` files play a run live; `XBE_LANGUAGE`
  sets the console's language.

Added to `config/game.toml` by hand (located with [XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase)
and by reading the XDK code that uses them): `D3D_Streams`, `D3DDevice_BeginPush`, `D3DDevice_EndPush`,
`D3DDevice_SetPalette`, `D3DDevice_Reset`, the DirectSound methods above, and `XGetFilePhysicalSortKey`.
`D3DDevice_Reset` and `XGetFilePhysicalSortKey` are replaced in [`src/game/hooks.cpp`](../src/game/hooks.cpp). So is
`XInputGetCapabilities` (0x00445438, which the symbol finder doesn't know): the XDK's body asks the USB driver about
the device behind the handle, which the port's controller handles aren't, and answered no rumble motors, so the game's
`SetVibration` (0x00287900, which checks them first) never reached `XInputSetState`. The port answers for every pad:
a standard controller with both motors.

## Supporting another release

The recompiled code and `config/game.toml` match one `default.xbe`. Another release (another region or version)
needs its own config:

1. `build-tools/xbe_symbols <its default.xbe>` writes a new config from the XDK's signatures.
2. Add by hand what [`config/game.toml`](../config/game.toml) has beyond that (above), at that release's addresses.
3. Recompile, build the PC test build, and run it until it reaches the city.

A pull request with the config (addresses only, never the `.xbe`) is welcome.

## Source layout

```
build.sh             the builder: your copy in, the .nro and the SD card folder out
build-windows.cmd    the same on Windows, through WSL (tools/windows/build-windows.ps1)
VERSION              the port's version
config/game.toml     recompiler input for the supported default.xbe: addresses and names only
src/common/          the game's description for the runtime (title, log name)
src/game/hooks.cpp   the game's own replacements of recompiled functions
src/game/language.cpp  the game in the console's language (game.ini's SKU, the English voices)
src/game/widescreen.cpp  the game's own 16:9 camera, widened
src/game/swing_log.cpp   each web swing's direction and animations, in the log
src/game/debug_hooks.cpp hooks that only log, for the test build
gl/                  the GPU renderer: OpenGL 4.3 (loader, GLSL translation, the backend)
switch/              the Switch program: start-up, screen, debug menu, settings, clocks, renderer host, sound,
                     controls, rumble, watch, crash handler, in-app updates
tools/xbe/           the PC tools build (xbe_recomp, xbe_symbols, xbe_extract)
tools/test/          the disc image tests
tools/analysis/      Python scripts that survey an XBE (header, imports, functions)
tools/make_release.sh  the release zip
tools/lock_nro.py    the release's .nro locked with the supported default.xbe's game code (AES-256-GCM; up to
                     0.3.1 with the whole file), and unlocked; `code` prints an xbe's game code SHA-1
tools/make_installer_site.sh  the installer page's site (gh-pages) and the release's locked .nro and symbols
installer/           the installer page: unlocks the .nro in the browser with the player's own game
tools/art/           the README's banner, social preview and install steps (make_art.py), and clips
sdk/                 Xbox XBE Recomp with the changes above (MIT, TGP482)
docs/                this documentation
assets/ generated/ build-switch/ build-tools/ out/   your copy and what is made from it (ignored by git)
```
