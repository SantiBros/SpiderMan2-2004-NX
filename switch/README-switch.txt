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


1. Put the files on the SD card
-------------------------------

  sdmc:/switch/SpiderMan2Xbox/SpiderMan2Xbox.nro
  sdmc:/switch/SpiderMan2Xbox/game/default.xbe      <- from your disc
  sdmc:/switch/SpiderMan2Xbox/game/data/...         <- the whole data folder
                                                       from your disc

The game folder must look like this:

  game/
    default.xbe
    data/
      game.ini
      movies/
      packs/
      sound/


2. Start it with full memory
----------------------------

Open the Homebrew Menu by HOLDING R while you start any game (not from the
Album: the Album gives homebrew too little memory). Then start
"Spider-Man 2 (Xbox) test".


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

The newest file in sdmc:/switch/SpiderMan2Xbox/logs/ (sm2_NNN.log), plus a
photo of the screen if something looks wrong or it stops.
