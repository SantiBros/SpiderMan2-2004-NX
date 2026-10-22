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
| `kernel/` | The 120 kernel functions the game imports (one, `IoQueryVolumeInformation`, is avoided by replacing the XAPI function that calls it): threads, events, semaphores, timers, DPCs and APCs, the file system over a host folder or a disc image, the memory manager, the hard disk under `hdd/`, the EEPROM settings (language 1, English). |
| `kernel/memory.cpp` | The guest's 4 GB address space. On the PC it is reserved whole. On the Switch, where homebrew can't reserve that much with pages committed on demand, the address space is cut into 16 slices of 256 MB, and the used ones (code and heap, physical memory and its mirror, the system area, the stacks) sit in one 1 GB block through a table. The generated code goes through the table (`XBE_SEGMENTED`). |
| `xdk/d3d8.cpp` | Direct3D 8 as the XDK has it: the device, resources, render states, and the push buffer the game writes NV2A commands into, read back as draws. This game draws mostly with vertex programs selected by address and with recorded push buffers (`RunPushBuffer`), both done on the host. |
| `gpu/` | The draws as the backend gets them: vertex formats, textures (swizzled, compressed), the shader translator (vertex programs and register combiners). |
| `xdk/dsound.cpp` | DirectSound: voices, buffers and streams mixed on the host, with the effects image the game loads. |
| `xdk/xapi.cpp` | The controllers, and XAPI's file and save functions. |

## The picture

Two backends take the draws:

- The **GPU renderer** ([`gl/`](../gl)), what the Switch draws with: OpenGL 4.3 core through Mesa's nouveau
  driver (devkitPro's `switch-mesa`), on Linux through any Mesa driver for the test build. It follows the SDK's
  Direct3D 12 renderer at render scale 1. Console surfaces become GL textures the draws render into, read back to
  guest memory when the CPU locks them and uploaded again when the CPU writes them; textures are decoded from guest
  memory (swizzled, S3TC, palettes) and kept while their bytes stay the same. The shader translator's vertex
  programs, register combiners and fixed function stages come out as GLSL ([`gl/glsl.cpp`](../gl/glsl.cpp), carried
  over line for line from the HLSL). The game's own vertex bytes go to the GPU as they are; the input assembler
  converts them (NORMPACKED3 is unpacked in the shader). Vertices, indices and constants go through persistently
  mapped buffers fenced in quarters, and a draw changes only the GL state that differs from the last one. It runs
  on a render thread of its own beside the game's (the SDK's `gpu/render_thread.cpp`), and logs a `perf:` line
  every 5 seconds with where a frame's time goes.
- The CPU **reference renderer** ([`sdk/harness/reference_renderer.cpp`](../sdk/harness/reference_renderer.cpp)),
  which draws into guest memory in software, register combiners and vertex programs included. It is exact enough
  to compare against and slow (seconds per city frame). The test build uses it, and the Switch falls back to it
  (a frame drawn every so often while the game runs at its own speed) when GL doesn't start, or with R held at
  start.

## The Switch layer

`switch/` is the program around the runtime on the Switch (libnx):

| File | What it does |
|---|---|
| `main.cpp` | Checks the launch mode (title takeover), finds the game (`kernel/game_source`), asks before a different release, mounts the disc image, boots the game, and keeps the applet alive. Quitting (+ and −) takes no lock the game could hold. |
| `screen.cpp` | The 1280×720 window. With GL (the default): an EGL window surface and an OpenGL 4.3 core context; the main thread draws the text screens (libnx's console font, as an image) until the renderer takes the context, then the renderer puts the status text over the frames until the game draws its own picture. Without: libnx's framebuffer, presented by the main thread, with the reference renderer's frames scaled to the screen's height. |
| `backend.cpp` | The GPU renderer's host (the window, the context, the swap), falling back to the reference renderer when GL doesn't start; the reference renderer paced for the Switch (60 frames a second, a frame drawn when as long as the last drawing has passed, at least half a second). Held at start: R the reference renderer, ZR the GPU renderer on the game's thread (no render thread). |
| `audio.cpp` | audout at 48 kHz, four 20 ms buffers filled by DirectSound's mixer; L at start turns it off. |
| `input.cpp` | The Xbox controller from the Switch's, mapped by position. |
| `watch.cpp` | Two threads above the game's priority: one puts the log on the SD card every 250 ms (a reset console loses cached writes), one logs a status line every 2 s with each game thread's current kernel or library function. |
| `crash.cpp` | The exception handler: registers, the module offset of the crash, return addresses from the stack and the game thread's current function, into `logs/crash.txt` and the run's log. |
| `pthread_detach.cpp` | `pthread_detach`, which libnx doesn't have (below). |

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
SHA-1, then one with the same entry point; every file it can't use is listed with the reason.

## What the Switch does differently

Things the PC test build couldn't show, found on the console or by reading libnx, and how the port handles them:

- **No `pthread_detach`.** libnx leaves newlib's detach hook unimplemented, so `std::thread::detach` throws
  `std::system_error` ("Function not implemented"). The port supplies the hook: detached threads go to a reaper
  that joins them when their kernel handle signals.
- **No second open of a file open for writing.** The Switch's file system refuses it (and a rename of an open
  file), where Windows and Linux allow it, and the game opens its packs more than once. Existing files open
  read-only unless the game asks to write (never on the disc), renames close the file around them, and resizes go
  through the open file. Conflicting opens are logged.
- **Cached writes are lost on a reset.** The log is `fsync`ed every 250 ms from its own thread, so a frozen
  console still leaves the log behind.
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
- `runtime/kernel/game_source.{h,cpp}` (new): the player's copy found in folders, matched by SHA-1 and entry point.
- `runtime/kernel/files.cpp`: read-only opens unless written, renames and resizes around open files (above).
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
  game's vertices as they are while the reference renderer has them converted.
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
  as x86 does.

Added to `config/game.toml` by hand (located with [XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase)
and by reading the XDK code that uses them): `D3D_Streams`, `D3DDevice_BeginPush`, `D3DDevice_EndPush`,
`D3DDevice_SetPalette`, `D3DDevice_Reset`, the DirectSound methods above, and `XGetFilePhysicalSortKey`.
`D3DDevice_Reset` and `XGetFilePhysicalSortKey` are replaced in [`src/game/hooks.cpp`](../src/game/hooks.cpp).

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
VERSION              the port's version
config/game.toml     recompiler input for the supported default.xbe: addresses and names only
src/common/          the game's description for the runtime (title, log name)
src/game/hooks.cpp   the game's own replacements of recompiled functions
gl/                  the GPU renderer: OpenGL 4.3 (loader, GLSL translation, the backend)
switch/              the Switch program: start-up, screen, renderer host, sound, controls, watch, crash handler
tools/xbe/           the PC tools build (xbe_recomp, xbe_symbols, xbe_extract)
tools/test/          the disc image tests
tools/analysis/      Python scripts that survey an XBE (header, imports, functions)
tools/make_release.sh  the release zip
sdk/                 Xbox XBE Recomp with the changes above (MIT, TGP482)
docs/                this documentation
assets/ generated/ build-switch/ build-tools/ out/   your copy and what is made from it (ignored by git)
```
