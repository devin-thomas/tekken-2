Tekken 2 Recompiled 1.0.0
=========================

Tekken 2 Recompiled is a native Mac and Windows version of Tekken 2, Namco's
1996 fighting game for the PlayStation. The game's own code was translated to C with psxrecomp and
compiled for your computer, so it runs as an ordinary app, not inside an
emulator. It plays from your own disc image of the game.

This download holds no disc image, no PlayStation BIOS and no game files. It
starts the game with OpenBIOS, a free replacement BIOS, which is included.


The disc it needs
-----------------

Tekken 2 (USA) (Rev 1), serial SLUS-00213, the Greatest Hits pressing. Other
releases (the first USA pressing, Europe and Japan) do not work with 1.0.0.

Use a BIN/CUE image with all three tracks. Tracks 2 and 3 are the CD audio.
Track 1 of the right disc matches the Redump record:

    Size    717,922,128 bytes
    CRC32   670a8546
    MD5     14c6b7308e1bf796fb36e523adde34d7
    SHA-1   699bd5831fbe3b662c59906ebf0acd3d1c3393f3

To check it on a Mac, open Terminal and run:

    shasum -a 1 "Tekken 2 (USA) (Rev 1) (Track 1).bin"

On Windows, open PowerShell and run:

    Get-FileHash -Algorithm SHA1 "Tekken 2 (USA) (Rev 1) (Track 1).bin"

The SHA-1 it prints must match the one above.


Mac
---

Needs a Mac with Apple silicon (M1 or later) and macOS 11 or later.

1. Unzip the download and move "Tekken 2 Recompiled" to your Applications
   folder.
2. Double-click it. The app is not notarized by Apple, so macOS stops it the
   first time and says it cannot check the app for malicious software.
   Click Done.
3. Open System Settings, click Privacy & Security, scroll down and click
   Open Anyway. Confirm with Open when the warning comes back. macOS
   remembers this, and the app opens normally from then on.
4. The app asks for your disc. Choose the .cue file. It remembers the
   choice.

Apple's own page on this is "Safely open apps on your Mac":
https://support.apple.com/en-us/102445

Settings, memory cards and the remembered disc are kept in
~/Library/Application Support/Tekken 2 Recompiled/. To choose a different
disc, delete disc-path.txt in that folder and open the app again.


Windows
-------

Needs 64-bit Windows 10 or 11.

1. Unzip the whole download to a folder of your own, such as
   Documents\Tekken 2 Recompiled. Running it from inside the zip does not
   work.
2. Double-click "Tekken 2 Recompiled.exe". The app is not signed, so
   Windows SmartScreen may say "Windows protected your PC". Click More info,
   then Run anyway.
3. The game asks for your disc. Choose the .cue file. It remembers the
   choice.

Settings, memory cards and the remembered disc are kept in
%LOCALAPPDATA%\Tekken 2 Recompiled\. To choose a different disc, delete
disc.cfg in that folder and start the game again.


Controls
--------

Every connected controller and the keyboard work at once. The first
controller to connect is player 1 and the second is player 2. Controllers
can be plugged in and out while the game runs.

A PlayStation controller maps one to one: X, Circle, Square, Triangle, L1,
R1, L2 and R2 are themselves, Create or Share is Select, Options is Start,
and the d-pad and the left stick move. On an Xbox or other controller the
bottom face button is X, the right one Circle, the left one Square and the
top one Triangle.

    Keyboard                          PlayStation
    Arrow keys                        D-pad
    X, S, Z, A                        X, Circle, Square, Triangle
    Q, W, E, R                        L1, R1, L2, R2
    Return                            Start
    Right Shift                       Select

Fullscreen: F11, Alt+Enter, or a double-click on the picture. On a Mac the
window's green button works too.


Settings
--------

Hold Select on any controller for a second, or press F1 or Esc, to open the
settings. Up and down choose a row, left and right change it, and X selects.

    Player 1 controls, Player 2 controls   Change the key or button for
                                           each PlayStation button
    Swap players' controllers              Player 1 becomes player 2
    Internal resolution                    How sharp the 3D picture is
    Fullscreen                             On or off
    Audio latency                          Raise it if the sound crackles
    FPS display                            Frames per second in a corner
    Mode                                   Enhanced or Original
    Quit                                   Closes the game

The title of the panel shows the version.

Enhanced is the default mode: discs load faster and the game never drops
frames. Original keeps the PlayStation's loading times and slowdowns. A
change of mode or audio latency applies after a restart, which the panel
offers.


Known issues
------------

- Original mode does not yet match a real PlayStation frame for frame.
- Only Tekken 2 (USA) (Rev 1) is supported.
- The Mac app runs only on Apple silicon Macs, and macOS asks you to
  approve it once, as described above.
- The Windows app is not signed, so SmartScreen may warn about it the
  first time.
- Linux, Steam Deck, Android and iOS versions are not part of this release.


Credits and licences
--------------------

Tekken 2 Recompiled is by Lil Gohan
(Twitter: https://twitter.com/gohan__FGC, https://fgc.lilgohan.com).

It is built with psxrecomp by Matthew Stanley, which is licensed under the
PolyForm Noncommercial License 1.0.0 with the additional permissions in
PERMISSIONS.md. Tekken 2 Recompiled is free: nobody may charge for it.

Every licence and notice for the parts it uses is in the LICENSES folder
next to this file; CREDITS.txt lists them.

Tekken is a registered trademark of Bandai Namco Entertainment Inc. Tekken 2,
its code and its artwork are copyright Namco (now Bandai Namco Entertainment
Inc.). The app icon is Namco's own artwork, the "T2" lettering from the
game's stage cards, taken from the game. This project is not made, approved
or endorsed by Bandai Namco. PlayStation is a
registered trademark of Sony Interactive Entertainment Inc.
