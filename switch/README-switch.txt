Spider-Man 2 (Xbox) for Nintendo Switch - TEST BUILD
====================================================
by SantiBros

This is an early test of the recompiled Xbox game on the Switch. It is NOT a
playable release yet:

  - The picture is drawn by the Switch's graphics chip (OpenGL), every frame.
    This is the first build that does: how fast it runs on a console is not
    known yet, and parts of the picture may be wrong.
  - Sound goes through the Switch speaker/headphones.
  - This .nro was built from YOUR copy of the game. It contains code derived
    from the game, so keep it to yourself: do not share it.

What it tests: that the game runs on the Switch's processor, graphics chip
and memory, reads its files from the SD card, and reacts to your controls.


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

Until the game draws its first picture, the screen shows the newest lines of
the log, so a photo of it tells how far the game got.

Hold while you start it (until the text screen appears), to find where a
problem comes from:

  L    sound off
  R    the old CPU renderer: a still picture every few seconds, without the
       graphics chip
  ZR   the graphics chip fed from the game's own thread (no render thread)


3. Controls
-----------

The game shows Xbox buttons. They are mapped by position:

  Xbox A (bottom)  = Switch B        Xbox B (right) = Switch A
  Xbox X (left)    = Switch Y        Xbox Y (top)   = Switch X
  Start = +    Back = -    triggers = ZL / ZR    white / black = L / R
  sticks and D-pad as on the Xbox

Hold + and - together for one second to quit.



4. What to send back
--------------------

The whole sdmc:/switch/SpiderMan2Xbox/logs/ folder (the newest sm2_NNN.log
and crash.txt if there is one), plus a photo of the screen if something looks
wrong or it stops. Every 5 seconds the log has a "perf:" line that says how
fast it runs and where the time goes, and a "hitch:" line marks each stutter.
shaders.cache beside the .nro keeps the shaders the game needed, so the next
start builds them before you need them: leave it there (a new version of the
port starts it again by itself). If the console had to be restarted, also send the newest
files in sdmc:/atmosphere/crash_reports/ and sdmc:/atmosphere/fatal_reports/
if they are from that time.
