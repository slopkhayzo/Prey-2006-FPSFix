# PreyHFR

> [!WARNING]
> This patch has been pretty much entirely been generated using AI; 
> I won't and will never claim to have enough knowledge or expertise to do this 
> kind of reverse-engineering by myself; I've tested the entire game at 360hz + 
> briefly tested at 120, 144 and 240 and so far spotted 
> no major issues (on my machine ofc, if you have issues feel free to open an Issue).

PreyHFR is an experimental high-frame-rate presentation fix for the Windows
Steam release of *Prey* (2006) 1.4. It keeps the original 16 ms (62.5 Hz)
simulation while allowing higher presentation rates, then interpolates the
camera, first-person weapon, world transforms, skeletal animation, and mouse
look between simulation ticks, with a frame-by-frame override for mouse deltas,
to restore low input latency.

The patch is a 32-bit ASI plugin loaded in-process by a standard external ASI
loader. It does not replace or modify
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

The ASI remains inert unless both retail files have these SHA-256 hashes:

| File | SHA-256 |
| --- | --- |
| `prey.exe` | `CEA6D424FBB8E2FFBF307A5BEE509B45C2D35242F70BE31387224DB2A0EADD69` |
| `base/gamex86.dll` | `74D436D376BA144762A28C940D0243135B4F9DB8FDD7EE597B9CB5E4277B43C6` |

Other releases are not patched. The plugin validates the files and the live
timing signature at runtime; ambiguous or missing signatures are treated as
errors and partially installed hooks are rolled back.

## Current status

Version `1.0.4` stabilizes the retail asynchronous tic scheduler itself. Only
the confirmed native 16 ms timer thread receives an exact synthetic
`timeGetTime` timeline; all other threads retain real Windows time. This removes
the launch-dependent zero-tic/paired-tic cadence that could make player,
camera, weapon, and animation interpolation pulse several times per second.
Nine repeated launches recorded 45,571 confirmed timer wakes without an
ordinary zero-tic callback; the rare multi-tic advances matched genuine
QPC-measured missed periods.

The lower-latency interpolation architecture is again the default. The newer
one-native-tic authoritative buffer remains available through
`compatibility.buffered_two_tic_interpolation`, defaulting to `false`, for
systems that benefit from additional producer lookahead. Automatic checkpoint
stall recovery and the configurable presentation-only `F10` reset remain
available as independent safeguards.

The underlying patch stack completed a full single-player playthrough at 360 Hz
with no major issues observed and has also been exercised in focused tests at
120, 144, 165, 240, and 360 Hz across windowed, exclusive, and primary-display
borderless modes. The default configuration enables the complete interpolation
stack, retains the native simulation rate, and detects the desktop refresh rate
for its presentation cap.

Known limits:

- Only the exact Steam 1.4 executable and game DLL listed above are supported.
- The ASI architecture has build/probe coverage, but its complete retail
  gameplay matrix through Ultimate ASI Loader must be repeated before release.
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
cmake --build build-x86 --target PreyHFRBundlePackage
```

`PreyHFRPackage` creates and verifies the plugin-only
`build-x86/release/PreyHFR-1.0.4-windows-x86.zip`.
`PreyHFRBundlePackage` creates the separate `-with-loader.zip`, pins and
checksum-verifies Ultimate ASI Loader v9.7.4, and includes its upstream license
and provenance. Neither build downloads dependencies.

The build never needs the retail game files. They are required only when
running the ASI against your own installation.

## Running

Two release forms are supported:

- The convenience bundle contains `PreyHFR.asi`, `PreyHFR.ini`, the pinned
  Win32 `dinput.dll` build of Ultimate ASI Loader, and its Prey-specific
  early-load `dinput.ini`. If the game directory has no existing `dinput.dll`,
  extract the bundle beside `prey.exe`.
- The plugin-only archive contains no proxy loader. Use it when an installed
  32-bit ASI loader already loads `.asi` files from the game directory.

Never overwrite an existing `dinput.dll` blindly: it may belong to another mod
or loader. In that case, install only `PreyHFR.asi` and `PreyHFR.ini` if the
existing loader is compatible, or arrange proxy chaining according to that
loader's documentation. Then launch Prey normally through Steam or `prey.exe`;
there is no PreyHFR launcher executable.

The supplied INI detects the primary monitor's current refresh rate, selects
its desktop resolution, and starts in borderless mode. To choose a fixed cap,
edit `PreyHFR.ini` and replace `presentation_fps = desktop` with a value such as
`presentation_fps = 144`. Set `borderless = false` to return to the game's
configured display mode.

Set `presentation_fps = 0` if V-Sync or an external limiter owns pacing; avoid
running two active limiters. `display.mode`, `display.vsync`, and
`display.resolution` replace the old launcher's display arguments. Every
interpolation layer has an independent diagnostic toggle. Invalid settings are
reported to the log and leave the patch inactive.

`compatibility.buffered_two_tic_interpolation` defaults to `false`. Enabling it
keeps one completed native tic as authoritative presentation lookahead, which
can tolerate irregular producer delivery but adds about 16 ms of positional
presentation latency. It is not normally required with the stabilized async
clock.

`PreyHFR.log` is written beside `PreyHFR.asi`. Include it when reporting an
issue. For a repeatable interpolation problem, enable
`diagnostics.interpolation_trace` in the INI. This adds one structured
`interp_trace` record per
gameplay presentation, including the camera clock, reset cause, interpolation
alphas, snapshot generation, and current/pending entity alignment. It grows the
log quickly and is disabled by default. Failed initialization restores changes
owned by the plugin; normal process exit discards its in-memory hooks.

The interpolation clock automatically rebases if a short renderer stall leaves
the next simulation snapshot more than the supported 32 ms presentation
horizon behind wall time. `diagnostics.timeline_reset_key` provides a manual,
presentation-only recovery as a fallback; it defaults to `F10`, accepts `F1`
through `F24`, and can be disabled with `none`. Pressing it does not alter the
simulation or save state.

To remove a packaged copy, close the game and run
`uninstall-preyhfr.cmd`; it removes only PreyHFR-owned files. It intentionally
does not remove `dinput.dll`, because that shared third-party loader may be in
use by another ASI plugin.

## Repository layout

- `src/common`: strict ASI configuration and retail-file identity checks.
- `src/launcher`: unshipped historical/development comparison harness.
- `src/hook`: ASI bootstrap, timing patch, presentation pacing,
  interpolation, input, and borderless hooks.
- `src/hook_probe`: deterministic hook and pacing preflight executable.
- `src/runtime_dump`: optional developer utility for mapping a personally
  owned retail executable at runtime; generated dumps remain untracked.
- `cmake`: release manifest generation and package verification.
- `packaging`: plain-text documentation included in binary releases.
- `vendor/ultimate-asi-loader`: pinned optional loader binary, provenance, and
  upstream MIT license used only by the convenience-bundle target.
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
