# Building Spider-Man 2 (Xbox) for Switch

[← Back to the README](../README.md)

Players: the [install guide](INSTALL.md#building-the-port) has the step-by-step version of this page
([en español](INSTALL.es.md#compilar-el-port)). This page is for building by hand, the test builds used for
development, the tests, and making a release.

- [The builder](#the-builder)
- [By hand](#by-hand)
- [The PC test build](#the-pc-test-build)
- [The ARM test build](#the-arm-test-build)
- [Tests](#tests)
- [Making a release](#making-a-release)

## The builder

```sh
bash build.sh <your game>      # a disc image, the game's folder, or default.xbe
```

| Step | What it does |
|---|---|
| 1. Tools | Checks git, CMake, Ninja, a C++20 compiler and devkitPro (`$DEVKITPRO`, `/opt/devkitpro` by default, with `switch-dev` and `switch-mesa`), then builds the PC tools in `build-tools/` from [`tools/xbe`](../tools/xbe): `xbe_recomp`, `xbe_extract` and `xbe_symbols`. CMake downloads Zydis and toml++ the first time. |
| 2. default.xbe | Takes it from the folder, or copies it out of the disc image with `xbe_extract` (the port's own disc reader), checks its SHA-1 against the supported release, and puts it in `assets/default.xbe`. |
| 3. Recompile | `xbe_recomp config/game.toml --xdk sdk/xdk` writes `generated/game/`: 128 partitions of C++, `x86_func_mapping.cpp` and `image_symbols.h`. |
| 4. The .nro | [`switch/build.sh`](../switch/build.sh) compiles the generated code and the host code (with the GPU renderer in [`gl/`](../gl)) with devkitA64, links Mesa's OpenGL from `switch-mesa`, and makes `build-switch/SpiderMan2Xbox.nro` (with its `.elf` and `.map`). |
| 5. SD card folder | `out/sd/switch/SpiderMan2Xbox/` with the `.nro`, `README.txt` and `game/`. |

Logs: `build-tools.log`, `build-recomp.log`, `build-switch.log`. Tested with Ubuntu 24.04 (also under WSL 2),
CMake 3.28, Ninja 1.11, GCC 13 and devkitA64 (GCC 16.1) with libnx and switch-mesa 20.1. `JOBS=N` sets the number of compile jobs
(all cores by default).

`assets/`, `generated/`, `build-switch/` and `out/` hold your copy of the game or code made from it. They are
in `.gitignore`: never commit them, never upload what's in them.

## By hand

```sh
cmake -S tools/xbe -B build-tools -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-tools
cp /path/to/your/default.xbe assets/
build-tools/xbe_recomp config/game.toml --xdk sdk/xdk          # writes generated/game
bash switch/build.sh                                          # writes build-switch/SpiderMan2Xbox.nro
```

`switch/build.sh` rebuilds only what changed: a partition of generated code when its `.cpp` is newer than its
object, host code when its source, any header or `VERSION` is newer. `SKIP_GEN=1` skips the generated code
check, `NO_LINK=1` stops before linking. The version comes from [`VERSION`](../VERSION) and goes into the
`.nro`'s metadata and onto the start screen.

## The PC test build

A Linux build of the same runtime and game code, headless, with the CPU renderer that the Switch build uses
([`sdk/harness`](../sdk/harness/README.md)). It is how the port is tested between console runs:

```sh
bash sdk/harness/build.sh -r . -g generated/game -o out/harness -n sm2
cd out/run && SWITCH=1 PULSE=1 ../harness/sm2 "/path/to/Spider-Man 2.iso"   # or the game's folder
```

| Variable | What it does |
|---|---|
| `SWITCH=1` | The Switch build's pacing: 60 frames a second, a frame drawn when as long as the last drawing has passed (at least half a second), saved as `cap/switch_<frame>.tga` from the first one with draws. |
| `PULSE=1` | The Switch build's status line every 2 seconds: the kernel or library function each game thread is in. |
| `SCRIPT=pad.txt` | Presses from a script, lines of `frame buttons [frames held]`, for example `1000 START 30`. Buttons: `UP DOWN LEFT RIGHT START BACK A B X Y LT RT LS LSD LSL LSR`, joined with `+`. |
| `CAP=1` | With `SWITCH=1`, every drawn frame's draws listed in `cap/draws_<frame>.txt`. |
| `REF=N` | Without `SWITCH`, every Nth frame drawn into `cap/ref_<frame>.tga` (the game then runs unpaced). |

[`tools/test/switch_screen.py`](../tools/test/switch_screen.py) puts a captured frame on a 1280×720 Switch screen,
as the Switch build shows it. To reach the city: `1000 START 30`, `1400 Y 30`, then `A 30` at 1800, 2200, 2600
and 3000.

### With the GPU renderer

The Switch build draws with the OpenGL renderer in [`gl/`](../gl). The test build can run it too, on a headless
EGL context (Mesa; llvmpipe when there is no GPU), into a 1280×720 screen like the Switch's:

```sh
sudo apt install libegl-dev libgl-dev libegl-mesa0 libgl1-mesa-dri   # EGL, GL headers, Mesa
DEFS="-DXBE_GL_HARNESS" LIBS="-lEGL" bash sdk/harness/build.sh -r . -g generated/game -o out/harness_gl \
    -n sm2 -e gl/gl_api.cpp -e gl/glsl.cpp -e gl/gl_backend.cpp
cd out/run && GL=1 RT=1 ../harness_gl/sm2 "/path/to/your/game"
```

| Variable | What it does |
|---|---|
| `GL=1` | The GPU renderer, every frame, paced to 60 a second. Every 5 seconds a `perf:` line says where a frame's time goes. |
| `RT=1` | The renderer on a thread of its own, as on the Switch. |
| `GLCAP=N`, `GLCAP_AT="f f ..."` | Every Nth frame, or those frames, saved as `cap/gl_<frame>.tga` (the 4:3 picture). |
| `REFAT="f f ..."` | Those frames drawn by the CPU renderer too, as `cap/ref_<frame>.tga`, to compare the two. |
| `GLDUMP=f` | Every colour target as frame `f` leaves them, as `cap/gltarget_*.tga`, and guest memory under each. |
| `GLREC="from to"` | Those frames into `cap/rec.mp4` at 60 frames a second, through `ffmpeg`. |
| `TIMESCALE=0.15` | The game's clock at that fraction of real time (also without `GL`): with `GL=1` every frame then covers a 60th of a second of the game however long it takes to draw, so a recording plays smoothly at the game's speed. |

A recording for clips: `RT=1 GL=1 TIMESCALE=0.15 GLREC="from to"` with a `SCRIPT`, then
[`tools/art/make_clip.sh`](../tools/art/make_clip.sh) for an animated WebP and
[`tools/art/make_reel.sh`](../tools/art/make_reel.sh) for an MP4 under 10 MB. Note that the title screen's attract
demos are the game's own video files (`data/movies/xbox/attract*.xmv`), not the engine drawing: clips meant to show
the port must come from play.

## The ARM test build

The same test build compiled for the Switch's processor (AArch64) and run under `qemu-aarch64`, with the Switch
build's memory layout. It catches what differs between x86 and ARM in the game's code and the runtime:

```sh
sudo apt install g++-aarch64-linux-gnu qemu-user-static
CXX=aarch64-linux-gnu-g++ ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57" DEFS="-DXBE_SEGMENTED=1" \
    GEN_OPT=-O2 LDFLAGS=-static bash sdk/harness/build.sh -r . -g generated/game -o out/harness_arm -n sm2
cd out/run_arm && SWITCH=1 PULSE=1 qemu-aarch64-static ../harness_arm/sm2 "/path/to/your/game"
```

It runs about ten times slower than the PC build: the title screen comes after about a minute, the city
after six to ten minutes.

## Tests

[`tools/test`](../tools/test) holds the tests for the disc image reader, run against your own copy:

| Tool | What it does |
|---|---|
| `make_xiso.py <folder> <out.iso> [--whole-disc] [--split BYTES]` | An XISO of the game's folder (checked against `extract-xiso`), optionally as a full disc layout or split into parts |
| `make_cci.py <in.iso> <out.cci> [--split BYTES] [--hc]` | A CCI as Repackinator's XboxToolkit writes it |
| `disc_check.cpp` | Reads every file of the folder back out of an image, whole and in random pieces from four threads, and compares byte for byte |
| `lz4_cases.py` + `lz4_check.cpp` | The LZ4 decoder against blocks compressed at every level, cut blocks included, under AddressSanitizer |

```sh
g++ -std=c++20 -O2 -I sdk/runtime tools/test/disc_check.cpp sdk/runtime/kernel/{disc,image}.cpp -o disc_check
./disc_check /path/to/game-folder "/path/to/Spider-Man 2.cci"
```

CSO files for testing come from [`ciso`](https://github.com/antangelo/ciso) (`cargo build --release`). The
images you make are your copy of the game: keep them out of the repository.

## Making a release

1. **The version:** raise [`VERSION`](../VERSION), add the version to [`CHANGELOG.md`](../CHANGELOG.md), and
   update both READMEs ([English](../README.md) and [Spanish](../README.es.md): the "New in" box, the status,
   the version in the install steps) and the player's guide [`switch/README-switch.txt`](../switch/README-switch.txt).
2. **Check it:** build with `build.sh` from a clean checkout, run it on the PC test build and, if you can, on a
   console.
3. **The zip:** commit, then `bash tools/make_release.sh` writes `out/SpiderMan2-2004-NX-<version>.zip` (the
   repository at that commit) and prints its SHA-256. It refuses a zip with game files in it.
4. **On GitHub:** tag `v<version>` on that commit, and publish a release with the zip attached, marked as a
   pre-release while the port is an early test.

The release description, in English and Spanish:

1. A one-line summary.
2. **"Which file do I download?" / "¿Qué archivo descargo?"**: a table. `SpiderMan2-2004-NX-<version>.zip` is the one
   to download: it holds the builder, which makes the `.nro` from your own copy. "Source code (zip)" and "Source
   code (tar.gz)" are added by GitHub: the same code, without the version's folder name. There is no `.nro` to
   download, and there never will be: it would contain the game's code.
3. What's new (from the changelog, in players' words), how to build and install it, updating from the previous
   version, known issues.
4. At the end, the zip's SHA-256 (as `make_release.sh` prints it).
