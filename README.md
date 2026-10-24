# SuperMetroidX

A native PC port of Super Metroid. The ROM is supplied by the person building the project and is never committed.

This tree currently builds a static SDL3 window so the project wiring can be checked before the recompiler and the SNES runtime land.

## Requirements

- CMake 3.25 or newer
- Ninja
- A C11 compiler
- clang-format, for `make fmt`
- Doxygen, for `make docs`
- GNU make. On Windows, use Git Bash.

## Build

```sh
make build     # Debug, -O0, debug symbols
make test      # Debug build plus unit tests
make release   # Optimized build
make package   # Release archive: .dmg, .zip, or .tar.gz
make fmt       # clang-format
make docs      # Doxygen HTML in docs/html
make clean
```

`make build` writes `build/debug/SuperMetroidX`. On macOS the binary is `build/debug/SuperMetroidX.app/Contents/MacOS/SuperMetroidX`. On Windows it is `SuperMetroidX.exe`.

```sh
./build/debug/SuperMetroidX
./build/debug/SuperMetroidX --smoke
```

`--smoke` opens the window, presents a few frames, and exits. CI sets `SDL_VIDEODRIVER=dummy`.

SDL3 `release-3.4.18` is fetched and compiled as a static library by CMake.
