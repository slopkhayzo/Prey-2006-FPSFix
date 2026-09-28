PreyHFR 1.0.2
===============

PreyHFR is an unofficial high-frame-rate fix for the Windows Steam release of
Prey (2006) version 1.4. It lets the game present at modern refresh rates while
keeping gameplay at its original speed.

No original game files are replaced or modified.


QUICK INSTALL
-------------

1. Close Prey if it is running.

2. Extract every file from this ZIP into the Prey installation folder. This is
   the folder that contains prey.exe.

   A typical Steam location is:

   C:\Program Files (x86)\Steam\steamapps\common\Prey

3. Double-click PreyHFRLauncher.exe whenever you want to play with the fix.

No command-line options are required. Do not launch prey.exe directly when you
want to use the patch.


DEFAULT SETTINGS
----------------

The included PreyHFR.ini automatically:

- detects the primary monitor's current refresh rate;
- uses the primary monitor's desktop resolution;
- starts the game in borderless mode;
- enables camera, weapon, world, animation, and mouse interpolation; and
- keeps the original 62.5 Hz simulation rate so gameplay does not speed up.


CHANGING SETTINGS
-----------------

Open PreyHFR.ini in Notepad. The comments in that file describe every setting.

To use a fixed frame-rate cap instead of the monitor's current refresh rate:

    presentation_fps = 144

To restore automatic refresh-rate detection:

    presentation_fps = desktop

To let V-Sync or another limiter control the frame rate:

    presentation_fps = 0

Avoid using two active frame limiters at the same time.

To disable the borderless default and use the game's configured display mode:

    borderless = false

To choose a custom windowed or exclusive-mode resolution, disable borderless
and set, for example:

    borderless = false
    resolution = 1920x1080

Command-line options remain available for advanced use. Run:

    PreyHFRLauncher.exe --help


SUPPORTED GAME VERSION
----------------------

The launcher supports the tested Windows Steam 1.4 files with these SHA-256
hashes:

prey.exe:
CEA6D424FBB8E2FFBF307A5BEE509B45C2D35242F70BE31387224DB2A0EADD69

base\gamex86.dll:
74D436D376BA144762A28C940D0243135B4F9DB8FDD7EE597B9CB5E4277B43C6

The launcher stops without patching or starting the game if the files do not
match. Other releases are not currently supported.


KNOWN LIMITS
------------

- Singleplayer has been tested at 120, 144, 165, 240, and 360 FPS, including a
  complete playthrough at 360 FPS.
- Multiplayer, demos, timedemos, overlays, ReShade, and other injectors have
  not been validated.
- Saving at checkpoints may cause a frame-time hitch. Briefly pausing and
  unpausing the game may clear it.
- Borderless mode uses the primary display at its desktop resolution.


TROUBLESHOOTING
---------------

- Keep PreyHFRLauncher.exe, PreyHFRHook.dll, and PreyHFR.ini together in the
  same folder as prey.exe.
- Check PreyHFR.log in the game folder after a launch problem.
- If another program controls frame pacing, set presentation_fps = 0.
- To test the game without applying the patch, launch:

      PreyHFRLauncher.exe --disabled

For source code, updates, and issue reports, visit:

https://github.com/slopkhayzo/Prey-2006-FPSFix


UNINSTALLING
------------

Close the game and run uninstall-preyhfr.cmd. It removes only PreyHFR files and
does not remove or restore any retail game files, saves, or configuration.


LICENSE
-------

PreyHFR is distributed under the MIT License. See PreyHFR-LICENSE.txt.

Prey is a trademark of its respective owners. This unofficial project is not
affiliated with or endorsed by Bethesda Softworks, ZeniMax Media, Human Head
Studios, or 3D Realms.
