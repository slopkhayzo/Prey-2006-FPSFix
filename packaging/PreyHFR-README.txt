PreyHFR 1.0.4
===============

PreyHFR is an unofficial 32-bit ASI high-frame-rate fix for the Windows Steam
release of Prey (2006) version 1.4. It lets the game present at modern refresh
rates while keeping gameplay at its original speed.

No original game files are replaced or modified.


WHAT'S NEW IN 1.0.4
-------------------

- Stabilizes the confirmed native 16 ms async-timer thread so process-start
  clock phase can no longer produce recurring zero-tic/paired-tic cadence.
  Every other game thread continues to see the real Windows clock.
- Restores the lower-latency interpolation architecture as the default.
- Retains the one-native-tic authoritative buffer as the optional
  compatibility.buffered_two_tic_interpolation setting, defaulting to false.


QUICK INSTALL - BUNDLE WITH LOADER
----------------------------------

1. Close Prey if it is running.

2. Check the folder containing prey.exe for an existing dinput.dll. Do not
   overwrite one: it may belong to another mod or ASI loader.

3. If no dinput.dll exists, extract every file from the "with-loader" ZIP into
   that folder. It includes the Win32 dinput.dll build of Ultimate ASI Loader
   v9.7.4, its required Prey early-load dinput.ini, and its MIT license.

4. Launch Prey normally through Steam or prey.exe. There is no separate
   PreyHFR launcher.

A typical Steam location is:

    C:\Program Files (x86)\Steam\steamapps\common\Prey


PLUGIN-ONLY INSTALL
-------------------

Use the plugin-only ZIP if the game already has a compatible 32-bit ASI loader.
Copy PreyHFR.asi and PreyHFR.ini beside prey.exe, or into another ASI directory
supported by that loader while keeping both files together.

Ultimate ASI Loader supports ASIs in the game root, scripts, plugins, or update
directories. PreyHFR is loader-agnostic and uses no Ultimate-ASI-Loader-specific
API. If an existing proxy, ReShade, or mod already owns dinput.dll, follow that
project's chaining instructions instead of replacing it.


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

Open PreyHFR.ini in Notepad. The ASI reads it on each launch. Invalid or unknown
entries leave the patch inactive and are explained in PreyHFR.log.

To use a fixed frame-rate cap instead of the monitor's current refresh rate:

    presentation_fps = 144

To restore automatic refresh-rate detection:

    presentation_fps = desktop

To let V-Sync or another limiter control the frame rate:

    presentation_fps = 0

Avoid using two active frame limiters at the same time.

To use the game's configured display state:

    borderless = false
    mode = game
    resolution = game

To request an explicit exclusive resolution:

    borderless = false
    mode = exclusive
    resolution = 1920x1080

The display.vsync setting accepts game, on, or off. Borderless always uses the
primary desktop resolution and cannot be combined with exclusive mode.

The compatibility.buffered_two_tic_interpolation setting defaults to false.
Enabling it keeps one completed native simulation tic buffered to tolerate
irregular producer delivery, at the cost of one native tic of positional
presentation latency. The normal lower-latency interpolation path is preferred
now that PreyHFR stabilizes the game's asynchronous tic clock directly.


SUPPORTED GAME VERSION
----------------------

The ASI supports the tested Windows Steam 1.4 files with these SHA-256 hashes:

prey.exe:
CEA6D424FBB8E2FFBF307A5BEE509B45C2D35242F70BE31387224DB2A0EADD69

base\gamex86.dll:
74D436D376BA144762A28C940D0243135B4F9DB8FDD7EE597B9CB5E4277B43C6

The plugin stays inert if the files do not match. Other releases are not
currently supported.


KNOWN LIMITS
------------

- Singleplayer was validated through the historical launcher at 120, 144, 165,
  240, and 360 FPS, including a complete playthrough at 360 FPS. The equivalent
  complete retail matrix must be repeated through the ASI loader.
- Multiplayer, demos, timedemos, overlays, ReShade, other injectors, and
  multi-plugin combinations have not been validated.
- Saving at checkpoints may still cause a one-off frame-time hitch. The
  presentation clock automatically rebases if that hitch leaves it stale.
- Borderless mode uses the primary display at its desktop resolution.


TROUBLESHOOTING
---------------

- Keep PreyHFR.asi and PreyHFR.ini together in a location scanned by the ASI
  loader.
- Check PreyHFR.log beside the ASI after a launch problem.
- If the log is not created, the external ASI loader did not discover the
  plugin; check loader placement and architecture first.
- If another program controls frame pacing, set presentation_fps = 0.
- F10 manually resets only the presentation/interpolation timeline if motion
  remains uneven after a hitch. The key is configurable or disableable in the
  INI and does not alter simulation or save state.

For source code, updates, and issue reports, visit:

https://github.com/slopkhayzo/Prey-2006-FPSFix


UNINSTALLING
------------

Close the game and run uninstall-preyhfr.cmd. It removes only files owned by
PreyHFR. It intentionally does not remove dinput.dll: the external loader is
shared infrastructure and another installed ASI may depend on it. Remove that
loader separately only after checking its other users.


LICENSE
-------

PreyHFR is distributed under the MIT License. See PreyHFR-LICENSE.txt.

The loader-inclusive bundle also contains Ultimate-ASI-Loader-LICENSE.txt,
Ultimate-ASI-Loader-NOTICE.txt, and a checksum record for the pinned binary.
Ultimate ASI Loader is third-party software by ThirteenAG and is not part of
PreyHFR.

Prey is a trademark of its respective owners. This unofficial project is not
affiliated with or endorsed by Bethesda Softworks, ZeniMax Media, Human Head
Studios, or 3D Realms.


Misc
----

For more game fixes, see https://slop-blog.enkhayzomachines.net/fixes
