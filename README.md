# SuperMetroidX

A static recompilation of Super Metroid for desktop PCs. At build time, the 65816 program in your ROM is translated to C and compiled into the game, which runs on a cycle-timed SNES runtime (CPU, PPU, S-SMP, S-DSP, DMA). The ROM is supplied by the person building the project and is never committed.

## Requirements

- CMake 3.25 or newer
- Ninja
- A C11 compiler
- clang-format, for `make fmt`
- Doxygen, for `make docs`
- GNU make. On Windows, use Git Bash.

## Build

Put your Super Metroid ROM (NTSC, headered or not) at `SuperMetroid.sfc` in the repository root, or point CMake at it with `-DSMX_ROM=/path/to/rom.sfc`.

```sh
make build     # Debug, -O0, debug symbols
make test      # Debug build plus unit tests
make release   # Optimized build
make package   # Release archive: .dmg, .zip, or .tar.gz
make fmt       # clang-format
make docs      # Doxygen HTML in docs/html
make clean
```

The first build translates the ROM. `smxrecomp` plays the attract demos and the opening of a new game in its interpreter to find code, which takes a few minutes. It caches what it found in `build/<config>/game/trace.txt`, so later builds reuse it. Configure with `-DSMX_RECOMP_TRACE=OFF` to translate from static analysis alone. Without a ROM, the build still succeeds: the shell links an empty translation and interprets everything.

`make build` writes `build/debug/SuperMetroidX`. On macOS the binary is `build/debug/SuperMetroidX.app/Contents/MacOS/SuperMetroidX`. On Windows it is `SuperMetroidX.exe`.

```sh
./build/release/SuperMetroidX               # loads SuperMetroid.sfc from the working directory
./build/release/SuperMetroidX path/to/rom.sfc
./build/debug/SuperMetroidX --smoke
```

`--smoke` opens the window, presents a few frames, and exits. CI sets `SDL_VIDEODRIVER=dummy`.

## Controls

| SNES | Keyboard | Xbox-style gamepad |
| --- | --- | --- |
| D-pad | Arrow keys | D-pad or left stick |
| B / Y | Z / X | A / X (south / west) |
| A / X | A / S | B / Y (east / north) |
| L / R | Q / W | Shoulders or triggers |
| Start / Select | Return / Right Shift | Start / Back |

F11 or Alt+Enter toggles fullscreen.

## Saves

The cartridge's battery-backed SRAM is kept as `<rom name>.srm` in your user-data directory: on macOS `~/Library/Application Support/SuperMetroidX/SuperMetroidX/`, on Windows `%APPDATA%\SuperMetroidX\SuperMetroidX\`, and on Linux `~/.local/share/SuperMetroidX/SuperMetroidX/`. The game saves at save stations as it would on a console, and the file is updated within a second.

## The translator

`smxrecomp` is the command-line front end to the translator library in `include/smx/translate.h`:

```sh
smxrecomp [--no-trace] [--seeds FILE] ROM OUTDIR
```

It decodes code from the reset and interrupt vectors, follows branches, calls, and jump tables while tracking the M, X, and E flags, and adds the entry points its trace reached. Each straight-line run of instructions becomes a C function with a label per instruction. The generated code calls the same cycle-timed CPU operations as the interpreter. Code the translator never found, such as code in RAM, still runs in the interpreter, and the shell logs how many instructions ran each way when it exits.

SDL3 `release-3.4.18` is fetched and compiled as a static library by CMake.
