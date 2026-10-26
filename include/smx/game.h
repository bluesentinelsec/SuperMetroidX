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

#endif /* SMX_GAME_H_ */
