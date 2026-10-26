# Changelog

All notable changes to the port. 0.1.0 is the first version played on a Switch; the versions before it were test
builds.

## [0.2.3]

**Saved games load again, and the log can no longer hold the game up**, for two reports that the game failed
after updating and one that it didn't keep its progress. The logs sent in
are 0.2.1's: in 4:3 at 720p it held 30 frames a second, but in 16:9 the game took 45 ms a frame instead of 7.5 and
stopped for up to 4 seconds at a time, down to 2.6 frames a second, waiting for the SD card. 0.2.2 removed the
16:9 lines that caused it; 0.2.3 makes sure no log line can do it again.

### Fixed
- **Saved games never loaded.** The game reads a save 512 bytes a frame with asynchronous reads, and on the Xbox
  such a read is always still in progress when the call returns. The port's reads complete at once, and the game
  takes a read that completes at once for the end of the file: it stopped after 1 KB of the 48 KB save, found it
  damaged and said "UNABLE TO LOAD SPIDER-MAN 2 SAVED GAME", then started a new game. Saving always worked, so the
  saves already on the card load now. Asynchronous reads and writes to the hard disk now say they are in progress,
  as on the console, with the result already there (only the save files are opened that way).
- **A rename over a file that is there replaces it**, as on the Xbox: the Switch's file system refuses it, where
  Windows and Linux allow it, so a game that saves to a new file and renames it would have failed every save after
  the first on the Switch only.
- **No game thread writes to the SD card for the log any more, warnings included.** Once the port's watch thread
  runs, a line only goes into a buffer in memory, and the watch thread writes it to the card every 250 ms, without
  holding the log's lock. In 0.2.2 warnings were still written by the thread that logged them, and the watch thread
  held the lock while it wrote, so a thread that logged during a slow write waited for the card. A card slow for long
  fills the buffer up to 1 MB, and the lines after that are counted, not waited for.
- **A message logged too often is cut short:** more than 100 times in 10 seconds, the rest of that time it is only
  counted, and a line says how many were left out. Errors are always written. A line logged every frame, like 16:9's
  camera line in 0.2.0 and 0.2.1 (over 9,000 lines in 80 seconds of 0.2.1's 16:9), can no longer fill the log.
- A crash writes the lines still in memory to the log before its report.
- **The builder says why it could not install devkitPro's tools.** It downloaded devkitPro's installer silently, so a
  failed download stopped it with no reason given. It now shows the download, tries three times (with `curl` if
  `wget` fails), and names the step that failed.

### Added
- **A copy of your saves outside the port's folder**, in `sdmc:/config/SpiderMan2Xbox/save/`, brought up to date a
  few seconds after the game saves and when the port quits. If `save/progress` has no saves at start (an update that
  replaced the whole `SpiderMan2Xbox` folder, or the port started from another folder), the copy is put back by
  itself, and the start screen says so. A save written is flushed onto the card when the game closes it, and every
  file the game is writing when the port quits.
- The log lists the saves found at start, in `save/` and in the copy.

### Changed
- PC test build: `LOGBG=1` writes the log the way the Switch build does, from a thread of its own; `FILETRACE=text`
  logs every open, read, write and query of a file whose path holds the text (`FILETRACE=UDATA` for the saves).

## [0.2.2]

**The 16:9 picture's slowdown, found**, for the report that 0.2.1 still ran badly, slower still in 16:9, and a new
4:3 decoration.

### Added
- **4:3, suit sides**, a fifth Screen choice: the web at the sides in the suit's colours, red with raised black
  webbing hanging from the top corners, a dark seam, and blue below it. Like the web sides it is drawn once and
  copied, so it costs nothing a frame (`screen=4:3-suit` in `settings.ini`).

### Fixed
- **16:9 wrote to the SD card several times a frame.** The game sets its camera back to 4:3 every frame, for each of
  its passes, and every time the port made it 16:9 again it wrote a line to the log, and on the Switch every line
  went to the card at once: a write the game waited for, several times a frame (over 20,000 lines in a short session
  of the PC test build). It is logged the first few times only now.
- **No log line waits for the SD card any more.** Warnings and errors still go to the card at once; the other lines
  are kept in a 64 KB buffer and written out by the port's watch thread every 250 ms, when there is something new, so
  a burst of lines can never slow the game again. A crash still leaves its report, and the log up to a quarter of a
  second before it.
- The PC test build's captures were black with web sides since 0.2.1 (the sides were copied to the wrong
  framebuffer there; on the Switch it was the right one).

### Changed
- **Texture filtering's help** says what anisotropic filtering costs the Switch's graphics chip in the city: try
  Standard first when the frame rate drops.
- The renderer asks the driver for its largest texture once, not every frame.
- **Correction:** 0.2.1's notes said the port's threads all shared one of the Switch's cores. They did not: libnx
  already lets every thread run on all three. 0.2.1 only gave the renderer and the sound a starting core of their own.

## [0.2.1]

**Faster**, for the report that 0.2.0 was unplayable on a Switch Lite. The changes below are checked on the PC test
build; the Switch's own frame rate is still to be measured on a console.

### Fixed
- **The renderer and the sound start on cores of their own** (the second and the third), the game on the first,
  each still free to move to an idle one. (These notes first said the threads had shared one core: they had not,
  as 0.2.2 found. libnx already lets every thread run on all three, so this changed little.)
- **The 16:9 picture cost the renderer about 11 ms a frame** on the PC test build (several times that on a Switch):
  to keep the HUD 4:3, every small draw went through its vertex program on the CPU. Now a draw's first vertex in
  perspective ends that (most small draws are 3D: sparks, debris, signs), and the 3D ones are remembered for a second:
  about 0.3 ms a frame.

### Changed
- **Auto clocks, the new default** in the debug menu: 1785 MHz CPU, 1600 MHz memory, and a GPU clock for the
  picture: 460.8 MHz at 480p in handheld mode, 768 MHz at 720p and docked. All are clocks Nintendo uses. A
  `settings.ini` from an older version with stock clocks (the old default) changes to Auto; a clock you chose stays.
  Stock is still in the menu.
- **720p and 1080p enlarge only the main picture** and its copies: the game's smaller images (its 256x256 shadows and
  glow passes) stay at the console's size, so the extra work grows with the main picture only. They looked the same.
- **1080p is for docked play**: in handheld mode it draws 720p, the screen's own.
- **The web sides are drawn once**, and the blurred sides only in the bars beside the picture.
- **No new stutter after the update**: the list of shaders 0.2.0 built is kept, as the shaders did not change. The
  list now follows the shaders' version, not the port's.
- **Texture checks on the processor's CRC32 instructions**: every texture a frame draws with is checked for changes
  once a frame, megabytes of it.
- **The log writes less to the SD card**: put on the card only when something new was written, and the swing logged
  in one line per swing (the set it played, the poses it went through).
- **The performance log** has a line more: where each draw's time goes (its textures, program, state, constants),
  and with 16:9 what keeping the HUD 4:3 costs. A log from a console says what to make faster next.

## [0.2.0]

**A sharper, fuller picture, and vibration.** The new picture settings and the vibration are checked on the PC test
build; how fast 720p and 1080p run on a console, and how the vibration feels, are still to be tried on one.

### Added
- **720p and 1080p.** The debug menu's **Resolution** draws the game at 480p (the Xbox's), 720p or 1080p: every
  surface the game draws is made bigger alike (the picture, the shadows and glows it renders first, the movies'
  frames), and the game is told nothing, so it plays the same. 720p fills the handheld screen pixel for pixel; 1080p
  is drawn bigger and shrunk, with smoother edges. The glows and blurs the game makes from its own picture keep their
  size, and the movies are enlarged with a sharper filter.
- **Full screen 16:9, nothing stretched.** The game's camera knows 16:9 (its display mode table has 16:9 modes the
  retail game never lists): **Screen → 16:9 full screen** turns it on, with a field of view that keeps 4:3's height
  and shows more of the city at the sides. What the game draws flat (the HUD, menus, text, the movies) stays 4:3 in
  the middle; full-screen washes and fades cover the whole screen.
- **4:3 with something at the sides** instead of black bars: **4:3 blurred sides** (the picture itself, blurred and
  dimmed) or **4:3 web sides** (a spider web).
- **Upscaling: Smooth or Sharp.** Sharp enlarges the picture and the movies with a Catmull-Rom filter, which keeps
  more detail than the smooth one.
- **Texture filtering**: standard, or anisotropic 4x, 8x or 16x, for streets and building fronts seen at a slant.
- **Vibration.** The game's rumble (in the web-swing, among others) on the HD rumble of Joy-Con or a Pro Controller, at
  the strength the debug menu sets: Off, Low, Medium (the default) or High. A rumble the game leaves on unchanged dies
  away after 2 seconds, so a controller is never left shaking on its own; none runs while the debug menu is open or
  the port is in the HOME menu, and it stops when the port closes. The Switch Lite has no rumble motor of its own:
  Joy-Con or a Pro Controller paired with it do vibrate.

### Fixed
- **The game never vibrated.** Before shaking a controller the game asks it which motors it has
  (`XInputGetCapabilities`), and the answer said none: the Xbox's library asked the USB driver about a pad the port
  doesn't model. The port now answers for every pad: a standard Xbox controller with both motors.

### Changed
- **The debug menu** has new rows: Resolution, Screen, Upscaling, Texture filtering and Vibration, each with its line
  of help. They change at once, while you play, and are saved in `settings.ini` (`resolution`, `screen`,
  `upscaling`, `anisotropic`, `rumble`). "Restore recommended settings" puts them back to 480p, 4:3 black sides,
  smooth, standard and medium.
- **The shaders are built again** at the first start of this version (they changed): the first time through each
  place can stutter briefly again, as after a first install.
- **Swing logging.** Each new web-swing, and each change of what it plays, is written to the log as a `swing:` line
  (the first 400 of a start): its angle, the two of the eight `SWING_V_` poses it blends, and which of the game's two
  sets of swing animations it drew lots for (`A`, or `B` for the `_B` ones). For the report of the swing showing only
  one animation.

### Known issues
- **Web-swinging showing only one animation** and **wrong reflections in building windows** were reported on the
  console. Neither is fixed yet. On the PC test build a swing goes through its poses as it should (from
  `SWING_V_270`/`315` down to `180`/`225` and back) and the game drew the `B` set; the windows are drawn the same as
  by the port's CPU renderer. A short video of each from the console, and the `logs` folder with its `swing:` lines,
  would show what differs there.

### For the PC test build
- `XBE_RES=720|1080`, `XBE_WIDESCREEN=1`, `XBE_SIDES=1|2|3` (blurred, web, suit), `XBE_SHARP=1` and `XBE_ANISO=N` set the
  picture as the debug menu does; `RUMBLELOG=1` logs the rumble the game asks for, `RUMBLETRACE=N` the game's rumble
  effects, `RUMBLETEST=N` shakes the pad at its Nth update; `RANDLOG=N` and `FMODCHECK=N` trace the game's `rand()`
  and x87 `fmod`.

## [0.1.0]

**Playable.** Played on a Switch Lite in handheld mode: the game runs smoothly at its own 30 frames a second, with
its music, sound effects and voices. Docked play and saving are still to be confirmed on a console.

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
- **Easier to build.** On Windows, double-click `build-windows.cmd` and choose your game: it installs WSL with
  Ubuntu if needed, builds the `.nro` inside Linux (installing the tools the first time), and makes an `SD card`
  folder with the port and your game in it, ready to copy. On Linux, `build.sh` offers to install the tools it
  needs on Ubuntu and Debian (`--yes` without asking), and `--copy-game` puts your game in the SD card folder too.
- **Saves in a folder of their own**, as in the NFS Most Wanted port: `save/progress/` holds the game's saves (the
  Xbox's `E:\UDATA` for this game) and `save/data/` the game's own data (`E:\TDATA`), apart from the rest of the
  Xbox's hard disk and the shader list, now in `cache/` (safe to delete). An `hdd/` folder from an older version is
  sorted out at the first start: its saves move to `save/`, the rest becomes `cache/`, nothing is deleted.

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
