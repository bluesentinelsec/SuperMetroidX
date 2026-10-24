# SuperMetroidX agent guide

This file is the convention source for people and coding agents working in this repository.

## Product

SuperMetroidX is a static recompilation of the SNES game Super Metroid. The 65816 program will be translated to C. This repository owns the SNES runtime and the desktop shell. It does not vendor `snesrecomp` or any other emulator tree.

The ROM, SRAM, save states, and generated translation of the ROM are user-supplied build inputs. Never commit them. Never commit a header, source file, or test fixture that contains ROM bytes.

## Language

The runtime and the shell are C11. Do not add C++ translation units.

`main` returns `int` because the C standard requires it. `argc` stays `int` for the same reason. Everywhere else, use SDL's explicit integer types: `Uint8`, `Uint16`, `Uint32`, `Uint64`, `Sint8`, `Sint16`, `Sint32`, and `Sint64`. Booleans are `bool`, `true`, and `false` as provided by the SDL3 headers. SDL3 renamed `SDL_bool`. Do not use `int`, `long`, `short`, `unsigned`, or `size_t` for project state. When an SDL or C API demands a different type, convert at the call boundary.

## Standard library

Use SDL3's C runtime wrappers from `SDL3/SDL.h` instead of the matching libc calls.

- Memory: `SDL_malloc`, `SDL_calloc`, `SDL_realloc`, `SDL_free`, `SDL_memset`, `SDL_memcpy`, `SDL_memmove`, `SDL_memcmp`
- Strings: `SDL_strlen`, `SDL_strcmp`, `SDL_strncmp`, `SDL_snprintf`, `SDL_strlcpy`, `SDL_strlcat`
- Files later in the project: `SDL_IOFromFile` and the `SDL_IOStream` API
- Logging: `SDL_Log`

Do not include `<stdio.h>`, `<stdlib.h>`, `<string.h>`, or `<stdint.h>` in project sources. SDL already provides the wrappers and the integer types.

## Dependencies

SDL3 is the platform layer: window, input, audio, threads, and filesystem. CMake pulls the pinned tag with FetchContent and builds it static (`SDL_STATIC` on, `SDL_SHARED` off). Link `SDL3::SDL3`.

OpenGL, when the shader presenter arrives, is the system OpenGL library reached through SDL's GL context. Shader sources live in the binary as `Uint8` arrays in headers. Do not ship loose `.glsl` files.

Do not add another third-party library without a written reason in the pull request. The unit-test runner is a C `main` that returns 0 or 1. Do not bring in a test framework.

## Layout

- `include/smx/` — public headers, one module per header
- `src/` — implementations and the executable entry point
- `tests/` — one executable per module, registered with CTest
- `cmake/` — helper modules when a single `CMakeLists.txt` is no longer enough
- `.github/workflows/` — CI for Windows, macOS, and Linux

The executable target is `SuperMetroidX`. The static library target is `smx`.

## Build

CMake is the build description. The Makefile is the only interface people should need.

- `make build` — Debug: no optimization, debug symbols
- `make release` — optimized, `NDEBUG`
- `make test` — Debug build, then CTest
- `make fmt` — `clang-format` on tracked C and headers
- `make docs` — Doxygen HTML under `docs/html`
- `make clean` — delete `build/` and `docs/html`
- `make package` — Release build and a CPack archive (dmg, zip, or tar.gz)

Generated files stay under `build/`. Do not commit them.

## Style

`.clang-format` is authoritative. Run `make fmt` before committing.

Name functions `smx_<module>_<verb>`. Name types `Smx<Name>` if a struct is part of the public API. Keep headers self-contained and wrapped in `#ifndef SMX_<NAME>_H_`.

Comment a function when the constraint is not obvious from its name. Do not comment the control flow.

## CI

GitHub Actions builds and tests Debug on `ubuntu-latest`, `macos-latest`, and `windows-latest`. A change that does not pass `make test` and `SuperMetroidX --smoke` with `SDL_VIDEODRIVER=dummy` is not done.

## What comes next

The SNES runtime (CPU bus, PPU, APU, DMA, HDMA) is written here, in C, against frame hashes from an NTSC reference log. The translator that emits C from the ROM is a separate tool. The shell then presents the framebuffer through OpenGL, with scanline shaders embedded in headers, an Xbox-style gamepad and keyboard mapped through SDL, SRAM in the OS user-data directory, and save states that are independent of SRAM.
