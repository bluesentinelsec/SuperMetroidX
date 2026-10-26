#include "translate_rom.h"

#include <SDL3/SDL.h>

/* Write the synthetic test ROM and its entry seeds into the directory in argv[1]. */
int main(int argc, char **argv) {
  static Uint8 rom[SMX_TROM_SIZE];
  char path[1024];
  char line[32];
  SDL_IOStream *seeds = NULL;
  Uint32 slot = 0;

  if (argc != 2) {
    SDL_Log("usage: smx_test_translate_rom DIR");
    return 2;
  }
  smx_trom_build(rom);
  SDL_CreateDirectory(argv[1]);
  SDL_snprintf(path, sizeof(path), "%s/test.sfc", argv[1]);
  if (!SDL_SaveFile(path, rom, sizeof(rom))) {
    SDL_Log("cannot write %s: %s", path, SDL_GetError());
    return 1;
  }
  SDL_snprintf(path, sizeof(path), "%s/seeds.txt", argv[1]);
  seeds = SDL_IOFromFile(path, "wb");
  if (seeds == NULL) {
    SDL_Log("cannot write %s: %s", path, SDL_GetError());
    return 1;
  }
  for (; slot < SMX_TROM_SLOTS; slot += 1) {
    Sint32 length = SDL_snprintf(line, sizeof(line), "%x\n", (unsigned)smx_trom_slot_key(slot));
    SDL_WriteIO(seeds, line, (size_t)length);
  }
  SDL_CloseIO(seeds);
  return 0;
}
