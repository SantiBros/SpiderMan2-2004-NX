# Changelog

All notable changes to the port. These are test builds: none has been played through on a Switch yet.

## [0.1.0]

### Added
- **A debug menu.** Hold **ZL + ZR** and press **−** for the port's settings, over the game: the language, the
  Switch's CPU, GPU and memory clocks, a frame-rate counter, the sound volume, the renderer (GPU or CPU), the render
  thread and the start-up log, with a line of help each and the frame rate, frame time and draw calls of the moment.
  Saved in `settings.ini` beside the `.nro`. The touch screen works too. While it is open the game gets no input, and
  the − of the combination never reaches it.
- **The Switch's clocks**, from the debug menu: only the rates Nintendo's own performance profiles use (up to
  1785 MHz CPU, 768 MHz GPU, 1600 MHz memory), put back after docking or sleep, with the console's own clocks back in
  the HOME menu and when the port closes. Left at stock unless you change them.
- **A frame-rate counter** in the black bar left of the picture: the frame rate, or also the time of a frame, the
  draw calls, the clocks and the memory.
- **The game in your language.** It follows the console's language: menus and subtitles in Spanish, French, German
  or Italian, from the texts on the disc (English for any other language). The disc's `game.ini` names the English
  release, which keeps the game in English; the port hands it the multi-language one. The voices stay in English, as
  the disc only has the English ones. The debug menu can pick another language than the console's.

### Changed
- **A clean start screen**: "Loading..." until the game draws, instead of the log lines scrolling by. The debug menu's
  start-up log brings them back, for a photo when the game doesn't start.
- **Faster game code.** The recompiled functions now tell the compiler that the game's memory never overlaps the
  x86 registers, so the registers stay in the processor's own between memory writes: the game's code is about 5%
  smaller for the Switch's processor, and the game thread needs about 14% less time a frame on the PC test build
  (6.4 instead of 7.2 ms for the game, 1.4 instead of 1.9 ms for recording its drawing, in the city).
- **Less work recording each draw**: the drawing state is compared a kilobyte at a time (with the Switch's NEON
  instructions), and a vertex program the game selects again is found without building it first.
- The buttons held at start (**L**, **R**, **ZR**) still work, for that start only; the debug menu keeps the same
  choices for every start.
- **A new icon** in the Homebrew Menu: Spider-Man's mask with the game's logo, instead of the placeholder.

## [0.0.10]

### Fixed
- **A system call the game uses had no implementation** (`IoQueryVolumeInformation`, for the free space and
  details of a drive): had the game called it, it would have stopped. It now answers like the other drive queries.
  The PC test build doesn't reach a call to it in what was played, so no stop was seen; the Switch build now has the
  answer ready when the game asks.

### Changed
- The PC test build was played through the logos, menus, name entry, opening cinematic and the first tutorial on the
  rooftop, and compared against the CPU renderer frame by frame. The CPU renderer now fogs like the GPU (the game's
  distant buildings are hazy by design) and no longer draws a quad's diagonal twice, so the comparisons point only at
  real differences. The test build's pad script gained the right stick and every button, and a run can be played live.

## [0.0.9]

### Fixed
- **The opening videos ran twice too fast** (the Activision and Treyarch logos, the intro, every cutscene video).
  The game shows a frame every two vertical blanks, 30 a second, and its videos are timed for that; the port showed
  one every blank. It now keeps to the pace the game asks for, in the videos and in play.
- **Building shadows were in the wrong place, and half the screen went dark.** The game fills its shadow volumes in
  the GPU's 2X multisample mode, where the screen counts half as many pixels across, and then darkens what they
  cover; the port drew the volumes squeezed into the left half and darkened the left half of the screen. Shadows of
  buildings now fall where they should, on the roofs and streets.
- **Shadow volumes drawn with another mesh's vertices.** A mesh placed far into a shared vertex buffer (more than
  2047 bytes in) was drawn from the start of the buffer.
- **The shadow's fade with distance** read the depth buffer as colour from an old copy; it reads the depth the frame
  drew.

### Changed
- **Fewer stutters the second time.** Every shader the game needed is kept in `shaders.cache` beside the `.nro`
  and built again at the next start, a little each frame (the Switch's graphics driver keeps no shader cache of its
  own), so a scene doesn't stall on shaders it already had. The first time through a new place still builds them.
- Mesa runs without its error checks and compiles each shader's variants when it is built rather than at its first
  draw.
- The log names stutters: a `hitch:` line for a frame that took far longer than the ones around it, with where the
  time went (drawing, waiting for the GPU, shader building, the game itself); the `perf:` line also says how long
  the game's own thread took.

## [0.0.8]

The first public release.

### Added
- **The Switch's graphics chip draws the game.** A new renderer turns the game's Direct3D 8 drawing into
  OpenGL 4.3 for the Switch's GPU (through Mesa, devkitPro's `switch-mesa`), every frame, instead of a still
  picture from the CPU every few seconds. The game's vertex programs, register combiners and fixed function
  stages become GLSL shaders; its textures (swizzled, compressed, with palettes) are decoded once and kept while
  they don't change; its vertices go to the GPU as the game made them.
- **A render thread.** The graphics work runs on a thread of its own, beside the game's, on another core.
- **Where the time goes**, in the log: every 5 seconds a `perf:` line with the frame rate and what each part of
  a frame cost.
- **Start-up switches** for testing, held while the port starts: **R** the old CPU renderer, **ZR** no render
  thread (**L** still turns the sound off). The port also falls back to the CPU renderer by itself when the
  graphics chip can't be set up.

### Fixed
- **Spider-Man's body wasn't drawn when you play** (only his shadow was). The game draws him with recorded push
  buffers that use the vertex arrays set before them, which weren't passed on.
- **The sky of the opening cinematic was grey noise.** Its cloud textures are 8-bit with a palette, and the
  palette set inside a push buffer wasn't used.

### Changed
- The repository is now [SpiderMan2-2004-NX](https://github.com/SantiBros/SpiderMan2-2004-NX), and the release
  download `SpiderMan2-2004-NX-<version>.zip`.
- Building needs `switch-mesa` too: `sudo dkp-pacman -S switch-dev switch-mesa`. `build.sh` says so when it's
  missing.

## [0.0.7]

Never published on its own: it went out as part of 0.0.8, the first public release.

### Added
- **Your disc image works as it is.** Put one of these in `game/` instead of the disc's files:
  - `.iso` or `.xiso`: a trimmed XISO, or a full disc dump with the video partition first (Redump's);
  - `.cci`: compressed, as Repackinator makes for Cerbios;
  - `.cso`: compressed (version 2, LZ4), as Repackinator, `ciso` or `xdvdfs` make for StellarOS;
  - any of them split into parts for a FAT32 card (`name.1.iso`, `name.2.iso`, ..., also `.1.cci`, `.1.cso`),
    read as one image.

  Every format was checked by reading all 128 files of the game back out of it and comparing them byte for byte
  with the disc's files, on the PC and on the ARM build, and the game boots from each.
- **The port finds your copy and says what's wrong.** It looks in `game/` and beside the `.nro`, takes the disc's
  files first and then the images, and picks the copy whose `default.xbe` is the release the port was built from.
  For a near release (same program entry, other file) it asks before starting; another game or version is refused.
  Files it can't use are listed with the reason: a `.zip`, `.7z` or `.rar` to extract, an Xbox 360 image, a
  PSP-style CSO, a file that isn't an Xbox image.
- **`build.sh`: the `.nro` from your own copy in one command.** Give it your disc image, the game's folder or
  `default.xbe`: it checks the release, builds the PC tools, recompiles, builds the `.nro` and lays out the SD card
  folder in `out/sd/`.

### Changed
- The Homebrew Menu shows the port as "Spider-Man 2 (Xbox)"; the version comes from the new `VERSION` file.
- Long lines on the start screen wrap at spaces.

## [0.0.6]

### Fixed
- **The rooftop under Spider-Man was missing** (the street below showed through it) on the PC, and drawn as
  smeared blocks on ARM. Triangles that cross the camera's near plane were clipped too close to the eye, leaving
  corners at screen positions in the billions, and turning those into whole numbers is undefined: x86 skipped the
  triangles by accident, ARM drew them. The renderer now clips at the near plane as the Xbox's GPU does, and
  converts numbers the way x86 does on every processor.

## [0.0.5]

### Fixed
- **The game's own files could be refused on the Switch.** Every file was opened for writing, the game opens its
  big data packs twice, and the Switch's file system refuses a second open of a file that is open for writing
  (Linux and Windows allow it). Files now open read-only unless the game asks to write, and never for writing on
  the disc. Renaming and resizing save files, which had the same problem, now work around open files.

## [0.0.4]

### Added
- **A start screen with the newest lines of the log**, until the game draws its first real picture: a photo of it
  says how far the game got.
- **The log survives a frozen console**: it reaches the SD card every 250 ms, where a reset used to lose the newest
  lines.
- **A status line every 2 seconds** in the log: frames, draws, sound buffers, and the kernel or library function
  each of the game's threads is in, marked `STILL` when one hasn't moved.
- **Hold L at start to turn the sound off**, to test whether a problem comes from the sound.
- Crash reports also go into the run's log, with the game thread's current function.

### Changed
- One framebuffer for the whole run, instead of the text console being swapped for a framebuffer when the first
  (black) frame came, which was one of the two Switch-only things happening when 0.0.3 froze.
- Quitting with + and − works even when the game hangs.

## [0.0.3]

### Fixed
- **It closed right after the start** with "Function not implemented": libnx has no `pthread_detach`, and the
  port detaches its background threads (the Xbox clock first). The port now supplies it.

### Added
- A clear message when the `data` folder is missing, or its contents sit directly in `game/`.

## [0.0.2]

### Added
- Diagnostics for the first test: every log line reaches the card, the newest one is shown during the start, and a
  crash handler writes `logs/crash.txt` (and uncaught C++ exceptions too).
- A message when the game files aren't found, and the release zip with the `game/` and `game/data/` folders ready.

## [0.0.1]

### Added
- The first Switch build: the recompiled game with the Xbox's system on libnx (threads, files on the SD card,
  memory in 256 MB slices), the CPU renderer on the screen, sound through audout, the controller mapped by
  position. A check for title takeover (the Album's memory is too small).
