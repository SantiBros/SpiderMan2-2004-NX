# Changelog

All notable changes to the port. 0.1.0 is the first version played on a Switch; the versions before it were test
builds.

## [0.4.1]

**The European French, German and Spanish disc.** Its `default.xbe` turned out to be a later build of the game
(version 5, four days after the US one's version 4), not the US code under another header, so 0.4.0 turned it away.
Its disc's data is the US disc's but for `game.ini`, a small part of one pack, the voices and the 50 Hz movies.

### Added
- **The European French, German and Spanish edition** ("Spider-Man 2 (Europe) (Fr,De,Es)", `default.xbe` SHA-1
  `ad4f0d360fc5a1869005331fe50f54a711426029`). A patch makes its `default.xbe` into the supported one, and the port
  runs it with the disc's own data: its French, German and Spanish voices (the **Voices** setting), its 50 Hz movies,
  and the text in any of the five languages, English included. The patch is carried inside the `.nro` and, for the
  installer page, published locked with that edition's game code: it does nothing without its `default.xbe`.
  Tested on a computer with the disc; not yet on a console.

## [0.4.0]

**Updates from inside the port, voices in another language, other editions of the game, the Switch's button
layout, and a closer 16:9.** The debug menu can now update the port by itself from its GitHub release; the voices
can be in another language than the text, from the European discs; a European edition is accepted when its game
code is the US one's; the buttons can follow the Switch's letters instead of the Xbox's positions
([#2](https://github.com/SantiBros/SpiderMan2-2004-NX/issues/2)); 16:9 has a close view with a bigger HUD; the
cutscenes fill a 16:9 screen ([#1](https://github.com/SantiBros/SpiderMan2-2004-NX/issues/1)); and in 16:9 the
minimap's pins sit on the map again.

### Added
- **Update, in the debug menu.** A asks GitHub for the newest version; a newer one is downloaded from its release (so
  it counts among the release's downloads), unlocked on the console with your own `default.xbe` as the installer page
  does, and put in place of the `.nro`, the old one kept as `SpiderMan2Xbox.nro.old`. A again quits and starts the
  new version. It uses the Switch's own TLS; nothing of it runs until the row is used. Updating to 0.4.0 itself is
  done the usual way (installer page); from then on it can be done from the port.
- **Voices, in the debug menu: their own language.** The game keeps each language's voices in sound banks of their
  own, and a disc carries only its edition's: the US disc English, the European discs French, German and Spanish (one
  disc) or Italian. **Auto** speaks the text's language when your copy has those voices, else the copy's own; or pick
  any language your copy has, apart from the text (Spanish voices with English text, say). The lip sync follows the
  voices. The menu says which voices your copy has.
- **16:9, close view** (debug menu, Screen), next to the old 16:9, now called **16:9, wide view**. The close view is
  the engine's own 16:9: the field of view the game sets across the width, so Spider-Man and the city look a third
  bigger, with a little less above and below. The HUD and menus are a tenth bigger; the map icons, the markers and
  the minimap follow them.
- **Other editions of the game.** The port and the installer page now tell a release by its game code (its
  `default.xbe`'s sections, not its header and certificate, which name the region): a European edition built from
  the same code as the US one is accepted as it is. 0.4.0's locked `.nro` opens with that game code (lock version 2);
  the earlier versions' still open with the US `default.xbe` only. An edition with other code is named on screen with
  its regions and both SHA-1s, to report.
- **Buttons: Xbox layout or Switch layout** (debug menu). The Xbox layout, by position, stays the default; the
  Switch layout maps by letter: the game's A is the Switch's A, B is B, X is X, Y is Y. It changes at once.

### Fixed
- **Italian text.** The game shows Italian only as its Italian edition; the port asked for it as the French, German
  and Spanish edition, which shows French for any other language. Each text language now gets its edition, on any
  disc: English text on the European French, German and Spanish disc too.
- **16:9: the cutscenes fill the screen.** The story's movies are letterboxed for the cinema inside their 4:3 frame,
  so in 16:9 they were a small picture with black on all four sides. Movies with black bands across their top and
  bottom are now enlarged to the screen's width, the bands cut to fit (nothing of the picture is lost). The logos
  and the attract demo, whose picture reaches the top and bottom, stay 4:3 in the middle.
- **16:9: the minimap.** Its 3D map is drawn where its rectangle is across the whole 16:9 screen, but its frame and
  pins (drawn flat, with the HUD) are kept 4:3 in the middle, so they sat to the left of the map under them. The map
  is now drawn where the HUD puts its frame.
- A sound bank or a movie only another edition has is opened in its place: another language's bank, or the 50 Hz
  movie (`NAME_PAL.XMV`) for the 60 Hz one.

### Changed
- The debug menu's rows are a little shorter, to fit the three new ones.
- The shader list of 0.3.x is kept, its programs updated for the close view.

## Installer page, 2026-11-03

Changes to the [installer page](https://santibros.github.io/SpiderMan2-2004-NX/) only: no new version of the port.

### Added
- **Every version to choose from.** A version menu with every release from 0.0.8 to 0.3.1, the newest picked by
  default (`?v=<version>` in the address picks another). The older ones were built from their own releases' code
  and locked like 0.3.x.
- **Each version's status and changes** under the menu: recommended (the newest), runs on a Switch with known
  problems (0.1.0 to 0.3.0) or early test that doesn't start on a Switch (0.0.8 to 0.0.10); its main changes in
  English and Spanish, with a link to all its notes; and what to watch out for: 0.3.0's pauses, 0.2.0 and 0.2.1's
  slow 16:9, saved games not loading before 0.2.3.
- **The early tests in a folder of their own** (`switch/SpiderMan2Xbox-<version>`). They keep their saves in
  `hdd/`, and a newer version that finds `hdd/` beside `save/` with the same save in both goes on using the one in
  `hdd/`: in the main folder they could hide your saves.

### Changed
- **Step 1 lists the files it takes:** a disc image (`.iso`, an Xbox ISO or a full disc dump, `.xiso`, `.cci`,
  `.cso`), all the parts of a split one, or only `default.xbe` from the game's files; and what it doesn't take
  (`.zip`, `.rar`, `.7z`, Xbox 360 and other consoles' versions). Every version reads the same files.
- **Step 2 says that with `default.xbe`, the `data` folder beside it goes into `game/` too.**

## [0.3.1]

**Smoother than 0.3.0.** 0.3.0 was reported much slower than 0.2.3 with the same settings. Its log shows 28–30
frames a second between long stops: the game waited up to 2.4 s at a time, the counter dropping to 1–14 frames a
second, while the graphics driver built shaders, because the port's list of them was empty. 0.3.1 keeps that list,
drops a GPU measurement 0.3.0 made at every frame, and makes the CRT looks cost nothing while they are off. And a
second CRT TV for 4:3, from the 2000s.

### Fixed
- **The GPU timer is gone from the console build.** 0.3.0 measured the GPU's time for each frame with a timer query;
  checking whether its result had arrived makes the Switch's graphics driver (Mesa's nouveau) send its commands to
  the GPU in the middle of the frame, and timer queries can make the GPU wait for its earlier work. The measurement
  is now only in the PC test build (`GLGPUTIME=1`).
- **The picture without the CRT look costs what it did in 0.2.3.** The CRT filter and the CRT TV's tube have a
  shader of their own, used only while one of them is on; otherwise the picture goes to the screen through 0.2.3's.
- **The blurred sides are made as in 0.2.3** (the frame copied whole, then its mipmaps), the way known to hold the
  game's speed on a console; 0.3.0's half-size copy is back out until it is measured on one.
- **The shader list is kept whatever Mesa built the port.** The list (`cache/shaders.cache`, the shaders built at
  start so they don't stall the game the first time it needs them) was named by the graphics driver's exact version
  too, and a list from any other build was thrown away and started over: every new effect then stalled the game
  while its shader was built (up to 2 s at a time in a 0.3.0 log). The shaders are the same whatever the driver, so
  the list is now named by the shader generator alone, and the lists of earlier versions are kept whatever driver
  they came from.
- The test build's draw trace (`DRAWTRACE`) no longer works anything out for each draw while it is off.

### Added
- **A 2000s CRT TV for 4:3** (debug menu, Screen: "4:3, 2000s CRT TV"; `screen=4:3-tv2000`): the TVs the game was
  played on in its time. Silver plastic, the glossy black bezel round a flat-faced tube (squarer corners, evener
  edges and lighter scanlines than the 70s set's), a stereo speaker's perforated grille on each side, and on the
  right the front panel: a round power button and its green light, the channel and volume buttons, and the yellow,
  white and red A/V inputs. 0.3.0's CRT TV is now "4:3, 70s CRT TV" (its `settings.ini` value unchanged).

### Added (test builds)
- `FENCETRACE=frame`: from that frame, the game's fences (where it waits for the GPU, and on what) in the log. The
  game waits for the frame before the one it just made, so it already works on the next frame while the last one
  is drawn.

## [0.3.0]

**Install from your phone, the city map right in 16:9, and a CRT look.** The port no longer has to be built on a computer: the
release holds the port locked, and a web page unlocks it with your own copy of the game, on an Android phone or a
computer. In 16:9 the map's icons sit on their places again. And for the retro look, a CRT filter and a CRT TV
around the 4:3 picture.

### Added
- **An installer page** (GitHub Pages, linked at the top of the README): choose your game (a disc image of any
  format the port reads, its split parts, or just its `default.xbe`) and it gives you `SpiderMan2Xbox-SD.zip`, the
  `switch` folder for your SD card. It works in the browser, on a phone too: nothing is uploaded, your game never
  leaves your device. Building on a PC (`build.sh`, `build-windows.cmd`) still works as before.
- **The release's .nro is locked** (`SpiderMan2Xbox-<version>.nro.locked`): AES-256-GCM, its key made from the
  whole `default.xbe` of the supported release, so it is of no use without the game. The page unlocks it in your
  browser; `tools/lock_nro.py unlock` does the same on a computer.
- **A symbols file** with every release (`SpiderMan2Xbox-<version>-symbols.elf`): the .nro's function names and
  debug information, without its code or data, to read the addresses in a crash report.
- **A CRT filter** (debug menu, CRT filter: Off, Light, Strong; `crt=` in `settings.ini`), with any Screen choice:
  the picture as on an old CRT television. Scanlines (one every 3 screen pixels at 720 rows, 4 at 1080, thinner on
  bright parts as a beam spreads), an aperture grille's red, green and blue stripes a screen pixel each, a soft glow
  around bright parts and darker edges, brightened back for what the stripes and lines take away; Strong makes them
  deeper and curves the glass. One pass over the screen, in the step that puts the picture on it.
- **A new 4:3 decoration: CRT TV** (debug menu, Screen: "4:3, CRT TV"; `screen=4:3-tv` in `settings.ini`): the
  picture on an old television, a walnut cabinet around it with the speaker's cloth on the left and a metal panel
  on the right (channel and volume knobs, the power button and its red light, the speaker's slots). The picture
  gets the tube's rounded corners, darker edges, a faint glare and scanlines, one every 3 screen pixels at 720 rows
  (4 at 1080), so they stay even. The cabinet is drawn once, like the web.
- **The GPU's time in the log's performance lines** (`perf: the GPU ... ms a frame`): next to the frame's time it
  tells whether the GPU or the CPU holds the frame rate back, for reports on 720p and 1080p.

### Fixed
- **16:9: the city map's icons moved with the map.** Spider-Man's marker, the blue stars and the $ icons were placed
  for the wider 16:9 view and then narrowed again with the HUD: they slid towards the middle, missed their places
  and moved more slowly than the map under them. The game's own world-to-screen function now gives positions for the
  HUD's 4:3 middle, so they land on their places: the markers over the city in play are placed the same way.
- **16:9: text and lines placed partly off the 4:3 screen kept their shape.** The map's district names were smeared
  where they crossed the 4:3 picture's edge: only points right on the screen's left and right edges (full screen
  passes) stay there now; the shader list of earlier versions is kept, its programs updated.

### Changed
- **Faster blurred sides** (4:3 with the picture blurred at the sides): the frame is copied at half its size, a
  quarter of the bytes, before it is blurred, instead of whole with all its mipmaps. Most useful at 720p and 1080p.

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
