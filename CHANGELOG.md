# Changelog

All notable changes to the port. Versions before 0.1.0 are early test builds: they have not yet been played
through on a Switch.

## [0.0.7]

The first public release.

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
