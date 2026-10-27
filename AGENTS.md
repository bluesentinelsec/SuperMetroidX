# SuperMetroidX agent guide

This file is the convention source for people and coding agents working in this repository.

## Product

SuperMetroidX is a static recompilation of the SNES game Super Metroid. The 65816 program is translated to C at build time. This repository owns the SNES runtime, the translator, and the desktop shell. It does not vendor `snesrecomp` or any other emulator tree.

The ROM, SRAM, save states, and generated translation of the ROM are user-supplied build inputs. Never commit them. Never commit a header, source file, or test fixture that contains ROM bytes. With `SMX_EMBED_ROM` (on by default) `smxembed` writes the ROM as C into `build/*/game/smx_rom.c`; that file and every build made from it contain the ROM and are never committed or published. Hashes of the game's output, such as `tests/game_hashes.h`, are not ROM bytes.

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

Do not add any third party libraries by default.

## Layout

- `include/smx/` — public headers, one module per header
- `src/` — implementations and the executable entry point
- `tests/` — one executable per module, registered with CTest
- `cmake/` — helper modules when a single `CMakeLists.txt` is no longer enough
- `.github/workflows/` — CI for Windows, macOS, and Linux

The executable target is `SuperMetroidX`. The static library target is `smx`. The translator CLI is `smxrecomp`, built on the `smx_translate_*` API in `include/smx/translate.h`. `smxsave` writes play-test saves through the `smx_save_*` API in `include/smx/save.h`; `playtest.sh` wraps it. CMake runs it on `SMX_ROM` (default `SuperMetroid.sfc` at the repository root) and links the generated `smx_game` library into the shell. With `SMX_EMBED_ROM` it also links the ROM itself, written as C by `smxembed`, as `smx_game_rom` (`src/rom_stub.c` defines it as NULL otherwise). Without a ROM the shell links `src/game_stub.c` and interprets.

## Build

CMake is the build description. The Makefile is the only interface people should need.

- `make build` — Debug: no optimization, debug symbols
- `make release` — optimized, `NDEBUG`
- `make play` — Release build, then run it on `ROM` with extra `ARGS`
- `make run` — Debug build, then run it on `ROM` (default `SuperMetroid.sfc`) with extra `ARGS`
- `make debug` — the same under lldb on macOS or gdb on Linux, printing every thread's backtrace on a crash
- `make test` — Debug build, then CTest; with a ROM it also runs the tests labelled `rom`
- `make test-release` — Release build, then CTest
- `make fmt` — `clang-format` on tracked C and headers
- `make docs` — Doxygen HTML under `docs/html`
- `make clean` — delete `build/` and `docs/html`
- `make package` — Release build and a CPack archive (dmg, zip, or tar.gz)

Generated files stay under `build/`. Do not commit them. That includes `build/*/game/`, the translation of the ROM, and its `trace.txt`.

## Style

`.clang-format` is authoritative. Run `make fmt` before committing.

Name functions `smx_<module>_<verb>`. Name types `Smx<Name>` if a struct is part of the public API. Keep headers self-contained and wrapped in `#ifndef SMX_<NAME>_H_`.

Comment a function when the constraint is not obvious from its name. Do not comment the control flow.

## CI

GitHub Actions builds and tests Debug on `ubuntu-latest`, `macos-latest`, and `windows-latest` with `SMX_EMBED_ROM=OFF` and no ROM. The `raspberry-pi (ROM)` job runs on the owner's self-hosted Raspberry Pi with the ROM built in, and runs `make test` and `make test-release` with the `rom` tests. All four are required to merge. Jobs run only in `bluesentinelsec/SuperMetroidX` and never for forks or pull requests from forks; keep that guard on every job. A change that does not pass `make test` and `SuperMetroidX --smoke` with `SDL_VIDEODRIVER=dummy` is not done.

## What comes next

The SNES runtime (CPU bus, PPU, APU, DMA, HDMA) is written here, in C, against frame hashes from an NTSC reference log. The translator emits C that calls the same cycle-timed CPU operations as the interpreter, so translated and interpreted runs produce identical frames; `tests/test_translate.c` checks every opcode under every flag set against the interpreter. The shell presents the picture through an OpenGL 3.2 core context (`smx_present_*` in `include/smx/present.h`), with its shaders as `Uint8` arrays in `src/present_shaders.h`, and falls back to an SDL renderer without OpenGL. It maps an Xbox-style gamepad and the keyboard through SDL, keeps SRAM in the OS user-data directory, and keeps save states independent of SRAM. The shell paces frames to the display's refresh through `smx_pace_*` in `include/smx/pace.h` (`--sync display`, the default) and resamples audio to match; `--sync audio` keeps the console's exact rate. Display enhancements (fill scaling, the 8:7 television aspect, scanlines) default on and each has a command-line switch to turn it off; release builds start borderless fullscreen and debug builds windowed.

Version 1.0 plays the whole game. Later releases add modern features on top of it without changing what the runtime emulates.
