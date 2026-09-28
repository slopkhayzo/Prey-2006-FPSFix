# PreyHFR

> [!WARNING]
> This patch has been pretty much entirely been generated using AI; 
> I won't and will never claim to have enough knowledge or expertise to do this 
> kind of reverse-engineering by myself; I've tested the entire game at 360hz + 
> briefly tested at 120, 144 and 240 and so far spotted 
> no major issues (on my machine ofc, if you have issues feel free to open an Issue),
> save for a small issue at first boot in which there could be a persistent
> frametime/interpolation hitch, a game restart is sufficient to fix it.

PreyHFR is an experimental high-frame-rate presentation fix for the Windows
Steam release of *Prey* (2006) 1.4. It keeps the original 16 ms (62.5 Hz)
simulation while allowing higher presentation rates, then interpolates the
camera, first-person weapon, world transforms, skeletal animation, and mouse
look between simulation ticks, with a frame-by-frame override for mouse deltas,
to restore low input latency.

The patch is a reversible runtime injection. It does not replace or modify
`prey.exe`, `base/gamex86.dll`, PK4 archives, saves, or other retail files.
The tracked repository and release packages contain no game files: you must
supply your own legally acquired copy of *Prey*. A developer may keep private
retail files and reverse-engineering material under the ignored `research/`
directory described below; none of it is part of the project or its releases.

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

Version `1.0.3` adds automatic recovery when a checkpoint or other short stall
leaves the presentation clock outside its supported interpolation horizon. It
also provides a configurable, presentation-only emergency reset key, defaulting
to `F10`. The checkpoint reproducer that previously left interpolation
permanently clamped recovered on the next snapshot in focused live testing.

The underlying patch stack completed a full single-player playthrough at 360 Hz
with no major issues observed and has also been exercised in focused tests at
120, 144, 165, 240, and 360 Hz across windowed, exclusive, and primary-display
borderless modes. The default configuration enables the complete interpolation
stack, retains the native simulation rate, and detects the desktop refresh rate
for its presentation cap.

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
`build-x86/release/PreyHFR-1.0.3-windows-x86.zip`.

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
PreyHFRLauncher.exe --interpolation-trace
PreyHFRLauncher.exe --timeline-reset-key F10
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
issue. For a repeatable interpolation problem, enable
`diagnostics.interpolation_trace` in the INI (or pass
`--interpolation-trace`). This adds one structured `interp_trace` record per
gameplay presentation, including the camera clock, reset cause, interpolation
alphas, snapshot generation, and current/pending entity alignment. It grows the
log quickly and is disabled by default. Clean shutdown restores all temporary
hooks and window state.

The interpolation clock automatically rebases if a short renderer stall leaves
the next simulation snapshot more than the supported 32 ms presentation
horizon behind wall time. `diagnostics.timeline_reset_key` provides a manual,
presentation-only recovery as a fallback; it defaults to `F10`, accepts `F1`
through `F24`, and can be disabled with `none`. Pressing it does not alter the
simulation or save state.

To remove a packaged copy, close the game and run
`uninstall-preyhfr.cmd`; it removes only PreyHFR files.

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
- `research` (ignored, local only): private development material. In the
  current workspace, `research/game` contains the retail installation copy,
  `research/tools` contains Ghidra/JDK/PresentMon, `research/ghidra` contains
  analysis projects, `research/runtime` and `research/captures` contain
  generated evidence, and `research/third_party` contains reference source.

Always configure builds from the repository root. Copied CMake caches under
`research/build*` refer to earlier source locations and must not be reused.

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
