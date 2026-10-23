Spider-Man 2 (Xbox) for Nintendo Switch
=======================================
by SantiBros

The original Xbox game, recompiled for the Switch. It is playable: it has been
played on a Switch Lite in handheld mode, smoothly and with its music, sound
effects and voices.

  - The picture is drawn by the Switch's graphics chip (OpenGL), every frame.
    The debug menu's counter (ZL + ZR + -) shows the frame rate.
  - Sound goes through the Switch speaker/headphones.
  - Not confirmed on a console yet: playing docked on a TV, and saving.
  - This .nro was built from YOUR copy of the game. It contains code derived
    from the game, so keep it to yourself: do not share it.


1. Put your copy of the game on the SD card
-------------------------------------------

Into sdmc:/switch/SpiderMan2Xbox/game/ put ONE of these:

  a) A disc image of your game, any of:
       .iso / .xiso   an XISO (game partition only) or a full disc dump
       .cci           compressed, as Repackinator makes for Cerbios
       .cso           compressed, as Repackinator or ciso make (version 2)
     Split images work too: name.1.iso, name.2.iso, ... (or .1.cci, .1.cso):
     put ALL the parts in the folder. Split images are what a FAT32 card
     needs, as it cannot hold a file over 4 GB.

       game/
         Spider-Man 2.iso                 (any name)

  b) The files from your disc:

       game/
         default.xbe
         data/
           game.ini
           movies/
           packs/
           sound/

Not read: .zip, .7z or .rar archives (extract the image or the folder from
them first), Xbox 360 images, and the old zlib CSO (version 1) made for the
PSP.

It must be the same release of the game this build was made from (its
default.xbe SHA-1 is in the log). With another release the screen says so and
asks before starting, as it would most likely crash.


2. Start it with full memory
----------------------------

Open the Homebrew Menu by HOLDING R while you start any game (not from the
Album: the Album gives homebrew too little memory). Then start
"Spider-Man 2 (Xbox)".

Until the game draws its first picture, the screen says "Loading...". With
the debug menu's "Start-up log" on, it shows the newest lines of the log
instead, so a photo of it tells how far the game got.

The game is in the console's language: English, Spanish, French, German or
Italian menus and subtitles (any other language: English). The voices are in
English in every language: the disc only has those.

Hold while you start it (until the blue screen appears), for that start only,
to find where a problem comes from:

  L    sound off
  R    the old CPU renderer: a still picture every few seconds, without the
       graphics chip
  ZR   the graphics chip fed from the game's own thread (no render thread)

Your saves are in save/ beside the .nro: save/progress/ holds the game's
saves, save/data/ what the game keeps for itself. Back up the save folder
from time to time; updates don't touch it. cache/ holds the rest of the Xbox's
hard disk and the shader list, and can be deleted at any time. A version
before 0.1.0 kept everything in hdd/: its saves are moved to save/ at the
first start.


3. Controls
-----------

The game shows Xbox buttons. They are mapped by position:

  Xbox A (bottom)  = Switch B        Xbox B (right) = Switch A
  Xbox X (left)    = Switch Y        Xbox Y (top)   = Switch X
  Start = +    Back = -    triggers = ZL / ZR    white / black = L / R
  sticks and D-pad as on the Xbox

Hold + and - together for one second to quit.


4. Debug menu
-------------

Hold ZL + ZR and press - (in any order) to open the port's settings; the
same, B or + close it. Up/Down picks a setting, Left/Right or A changes it
(the touch screen works too). The game gets no input while it is open.

  Language            Auto (the console's), English, Espanol, Francais,
                      Deutsch, Italiano - from the next start
  Resolution          480p (the Xbox's), 720p, 1080p (docked; 720p in
                      handheld mode) - the whole picture and the movies;
                      more work for the GPU
  Screen              4:3 with black, blurred or web sides, or 16:9 full
                      screen (the game's own wide camera; the HUD, menus and
                      movies stay 4:3)
  Upscaling           smooth or sharp
  Texture filtering   standard, anisotropic 4x, 8x, 16x (much more work for
                      the GPU in the city)
  Vibration           off, low, medium, high (Joy-Con or a Pro Controller;
                      the Switch Lite has none of its own)
  CPU / GPU / memory  auto (the default: 1785 MHz CPU, 1600 MHz memory,
  clock               the GPU by the picture), stock, or rates Nintendo's
                      own profiles use (up to 1785 / 768 / 1600 MHz); the
                      console's own clocks come back in the HOME menu and
                      when you quit
  Frame-rate counter  off, FPS, full (top left)
  Sound volume        100% to 25%, off
  Renderer            GPU or CPU - from the next start
  Render thread       on or off - from the next start
  Start-up log        the log on the start screen - from the next start

The settings are kept in settings.ini beside the .nro: delete it to go back
to the recommended ones. If the game runs under 30 frames a second in busy
places, check that the clocks are on Auto, then try a lower resolution.



5. What to send back
--------------------

The whole sdmc:/switch/SpiderMan2Xbox/logs/ folder (the newest sm2_NNN.log
and crash.txt if there is one), plus a photo of the screen if something looks
wrong or it stops. Every 5 seconds the log has a "perf:" line that says how
fast it runs and where the time goes, and a "hitch:" line marks each stutter.
cache/shaders.cache keeps the shaders the game needed, so the next start
builds them before you need them: leave it there (a new version of the port
starts it again by itself). If the console had to be restarted, also send the newest
files in sdmc:/atmosphere/crash_reports/ and sdmc:/atmosphere/fatal_reports/
if they are from that time.
