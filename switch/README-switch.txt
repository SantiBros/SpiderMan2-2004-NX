Spider-Man 2 (Xbox) for Nintendo Switch - TEST BUILD
====================================================
by SantiBros

This is an early test of the recompiled Xbox game on the Switch. It is NOT a
playable release yet:

  - The picture is drawn by a slow software renderer and updates only every
    second or two. The real Switch graphics come later.
  - Sound goes through the Switch speaker/headphones, but may stutter while a
    picture is being drawn.
  - This .nro was built from YOUR copy of the game. It contains code derived
    from the game, so keep it to yourself: do not share it.

What it tests: that the game runs on the Switch's processor and memory, reads
its files from the SD card, and reacts to your controls.


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

Sound off: hold L while you start it (until the text screen appears). Use it
to test whether a problem comes from the sound.


3. Controls
-----------

The game shows Xbox buttons. They are mapped by position:

  Xbox A (bottom)  = Switch B        Xbox B (right) = Switch A
  Xbox X (left)    = Switch Y        Xbox Y (top)   = Switch X
  Start = +    Back = -    triggers = ZL / ZR    white / black = L / R
  sticks and D-pad as on the Xbox

Hold + and - together for one second to quit.

Because the picture updates slowly, press a button and hold it for about a
second, then wait for the screen to change.


4. What to send back
--------------------

The whole sdmc:/switch/SpiderMan2Xbox/logs/ folder (the newest sm2_NNN.log
and crash.txt if there is one), plus a photo of the screen if something looks
wrong or it stops. If the console had to be restarted, also send the newest
files in sdmc:/atmosphere/crash_reports/ and sdmc:/atmosphere/fatal_reports/
if they are from that time.
