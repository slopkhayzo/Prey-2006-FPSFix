# PreyHFR

> [!WARNING]
> This patch has been pretty much entirely been generated using AI; 
> I won't and will never claim to have enough knowledge or expertise to do this 
> kind of reverse-engineering by myself; I've tested the entire game at 360hz + 
> briefly tested at 120, 144 and 240 and so far spotted 
> no major issues (on my machine ofc, if you have issues feel free to open an Issue), 
> save for frametime & interpolation hiccups upon saving checkpoints or first time loading 
> (solvable by briefly pausing/unpausing the game, or at most a game reboot if 
> it's persistent but the latter has happened very rarely to me, once in the entire 
> playthrough I think) and I am working to fix that too

PreyHFR is an experimental high-frame-rate presentation fix for the Windows
Steam release of *Prey* (2006) 1.4. It keeps the original 16 ms (62.5 Hz)
simulation while allowing higher presentation rates, then interpolates the
camera, first-person weapon, world transforms, skeletal animation, and mouse
look between simulation ticks, with a frame-by-frame override for mouse deltas,
to restore low input latency.

The patch is a reversible runtime injection. It does not replace or modify
`prey.exe`, `base/gamex86.dll`, PK4 archives, saves, or other retail files.
This repository contains no game files: you must supply your own legally
acquired copy of *Prey*.

> [!IMPORTANT]
> This is an unofficial runtime patch. Back up saves, test conservatively, and
> expect incompatibilities with multiplayer, demos, overlays, ReShade, or
> other injectors until those combinations have been validated.

## Supported game build

The launcher fails closed unless both retail files have these SHA-256 hashes:

| File | SHA-256 |
| --- | --- |
| `prey.exe` | `CEA6D424FBB8E2FFBF307A5BEE509B45C2D35242F70BE31387224DB2A0EADD69` |
| `base/gamex86.dll` | `74D436D376BA144762A28C940D0243135B4F9DB8FDD7EE597B9CB5E4277B43C6` |

Other releases are not patched or started. Signatures are validated at runtime
after the protected retail executable has initialized; ambiguous or missing
signatures are treated as errors.

## Current status

Version `1.0.2` uses the patch stack that completed a full single-player
playthrough at 360 Hz with
no major issues observed. It has also been exercised in focused tests at 120,
144, 165, 240, and 360 Hz across windowed, exclusive, and primary-display
borderless modes. The default configuration enables the complete interpolation
stack, retains the native simulation rate, and caps presentation at 240 Hz.

Known limits:

- Only the exact Steam 1.4 executable and game DLL listed above are supported.
- Multiplayer, demos, timedemos, overlays, ReShade, and other injectors are not
  accepted compatibility paths yet.
- Borderless mode uses the primary display at its desktop resolution.
- Presentation below the native 62.5 Hz simulation rate is diagnostic only;
  formal validation begins at 120 Hz.

## Building from source

Requirements:

- Windows
- Visual Studio C++ build tools with an **x86** developer environment
- CMake 3.21 or newer
- Ninja (recommended)
- A C++20-capable MSVC toolchain

From an x86 Visual Studio developer command prompt:

```bat
cmake -S . -B build-x86 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-x86 --parallel
ctest --test-dir build-x86 --output-on-failure
cmake --build build-x86 --target PreyHFRPackage
```

The package target stages, hashes, architecture-checks, and validates the exact
release payload before and after creating
`build-x86/release/PreyHFR-1.0.2-windows-x86.zip`.

The build never needs the retail game files. They are required only when
running the launcher against your own installation.

## Running

Extract the package directly into the game directory containing `prey.exe`,
keeping `PreyHFRLauncher.exe`, `PreyHFRHook.dll`, and `PreyHFR.ini` together.
Double-click `PreyHFRLauncher.exe` to start the game with the patch. No command
line is required.

The supplied INI detects the primary monitor's current refresh rate, selects
its desktop resolution, and starts in borderless mode. To choose a fixed cap,
edit `PreyHFR.ini` and replace `presentation_fps = desktop` with a value such as
`presentation_fps = 144`. Set `borderless = false` to return to the game's
configured display mode.

Keeping the patch outside the game directory remains available for development
or advanced setups by providing the installation explicitly:

```bat
PreyHFRLauncher.exe --game-dir "C:\Path\To\Prey"
```

Useful checks and examples:

```bat
PreyHFRLauncher.exe --validate-config
PreyHFRLauncher.exe --game-dir "C:\Path\To\Prey" --dry-run
PreyHFRLauncher.exe --game-dir "C:\Path\To\Prey" --fps 144 --borderless
PreyHFRLauncher.exe --game-dir "C:\Path\To\Prey" --disabled
PreyHFRLauncher.exe --help
```

`--validate-config` parses and prints the effective configuration without
starting the game. `--dry-run` starts the unmodified game, validates the live
timing signature, and detaches without installing the full patch. `--disabled`
starts the hash-validated game without DLL injection or renderer changes.

Command-line options override `PreyHFR.ini`; `--fps desktop` restores automatic
refresh-rate detection after a numeric INI setting. Set `presentation_fps = 0`
if V-Sync or an external limiter owns pacing; avoid running two active
limiters. Every interpolation layer has an independent diagnostic toggle.

`PreyHFR.log` is written beside the launcher. Include it when reporting an
issue. Clean shutdown restores all temporary hooks and window state. To remove
a packaged copy, close the game and run `uninstall-preyhfr.cmd`; it removes
only PreyHFR files.

## Repository layout

- `src/launcher`: validates the retail build, starts the game, and injects the
  patch after runtime signatures resolve.
- `src/hook`: presentation pacing, interpolation, input, and borderless hooks.
- `src/hook_probe`: deterministic hook and pacing preflight executable.
- `src/runtime_dump`: optional developer utility for mapping a personally
  owned retail executable at runtime; generated dumps remain untracked.
- `cmake`: release manifest generation and package verification.
- `packaging`: plain-text documentation included in binary releases.
- `PreyHFR.ini`: documented release defaults.

No retail binaries, SDK source, decompilations, generated dumps, captures, or
downloaded reference projects belong in this repository. The `.gitignore`
contains defense-in-depth exclusions for those materials.

## Contributing

Keep changes fail-closed and version-specific. Do not submit copyrighted game
assets or binaries. When adding support for another build, document hashes,
use signatures with structural validation rather than fixed addresses, and
verify clean rollback as well as high-rate behavior.

## License

PreyHFR is available under the MIT License. The license is included with both
the source tree and release package.

*Prey* is a trademark of its respective owners. This project is unofficial and
is not affiliated with or endorsed by Bethesda Softworks, ZeniMax Media, Human
Head Studios, or 3D Realms.
