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
make run       # Debug build, then play SuperMetroid.sfc (ROM=path, ARGS=flags)
make debug     # Debug build under lldb (macOS) or gdb (Linux); backtrace on a crash
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

F11 or Alt+Enter toggles fullscreen. F5 saves a quick-save state and F9 loads it. `--load-state` on the command line starts from the quick-save.

## Saves

The cartridge's battery-backed SRAM is kept as `<rom name>.srm` in your user-data directory: on macOS `~/Library/Application Support/SuperMetroidX/SuperMetroidX/`, on Windows `%APPDATA%\SuperMetroidX\SuperMetroidX\`, and on Linux `~/.local/share/SuperMetroidX/SuperMetroidX/`. Quick-save states go next to it as `<rom name>.state`. They hold everything but SRAM, so loading one never undoes the game's own saves. A state only loads into the build and ROM that made it. Set `SMX_DATA_DIR` to keep saves somewhere else, such as next to a portable install. The game saves at save stations as it would on a console, and the file is updated within a second.

## Play testing

`playtest.sh` starts the game in play at a chosen section, skipping the intro and Ceres:

```sh
./playtest.sh norfair                 # the first Norfair save station, with Varia
./playtest.sh maridia --items screw   # add items or beams to the section's loadout
./playtest.sh --debug tourian         # under lldb or gdb, with a backtrace on a crash
./playtest.sh --keep norfair          # continue the last Norfair session
./playtest.sh --list                  # sections and loadout options
```

The sections are `ceres`, `landing`, `crateria`, `brinstar`, `norfair`, `wrecked-ship`, `maridia`, and `tourian`. Each starts with clear progress flags: bosses are alive and the Wrecked Ship is unpowered. The script uses `smxsave`, which plays the ROM headless: the game formats its own SRAM, slot A is written at the section's save station with its loadout, and the tool presses through the title and file select to a quick-save state in play. Saves and states from a play test stay in `build/playtest/<section>/`, apart from your own.

## The translator

`smxrecomp` is the command-line front end to the translator library in `include/smx/translate.h`:

```sh
smxrecomp [--no-trace] [--seeds FILE] ROM OUTDIR
```

It decodes code from the reset and interrupt vectors, follows branches, calls, and jump tables while tracking the M, X, and E flags, and adds the entry points its trace reached. Each straight-line run of instructions becomes a C function with a label per instruction. The generated code calls the same cycle-timed CPU operations as the interpreter. Code the translator never found, such as code in RAM, still runs in the interpreter, and the shell logs how many instructions ran each way when it exits.

SDL3 `release-3.4.18` is fetched and compiled as a static library by CMake.
