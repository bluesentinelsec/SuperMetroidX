#ifndef SMX_GAME_H_
#define SMX_GAME_H_

#include "smx/cpu.h"

#include <SDL3/SDL.h>

/**
 * The translation linked into the shell. `smxrecomp` generates it at build
 * time from the user's ROM; without a ROM the build links a translation with
 * no blocks and everything runs in the interpreter. `rom_hash` is
 * `smx_translate_rom_hash` of the image it was made from, so the shell uses
 * `code` only for that ROM.
 */
typedef struct SmxGame {
  SmxCpuCode code;
  Uint32 rom_hash;
  Uint32 rom_size;
} SmxGame;

extern const SmxGame smx_game;

/**
 * The ROM image linked into the shell when it is built with `SMX_EMBED_ROM`
 * (the default when a ROM is given), so the game runs without the ROM file.
 * NULL and 0 otherwise. A build with an embedded ROM contains the whole ROM.
 */
extern const Uint8 *const smx_game_rom;
extern const Uint32 smx_game_rom_size;

#endif /* SMX_GAME_H_ */
