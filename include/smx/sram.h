#ifndef SMX_SRAM_H_
#define SMX_SRAM_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * Battery-backed cartridge RAM kept in a file. `saved` is what the file
 * holds, so a flush writes only when the game changed something. Writes go
 * to `path` plus `.tmp` and are then renamed over `path`, so a crash never
 * leaves a torn save.
 */
typedef struct SmxSram {
  char path[1024];
  Uint8 *saved;
  Uint32 size;
} SmxSram;

/**
 * Write `<user data>/SuperMetroidX/<rom file name>.srm` into `path`, with the
 * ROM's extension replaced. False when SDL has no user-data directory.
 */
bool smx_sram_default_path(char *path, Uint32 size, const char *rom_path);

/** Bind `bus->sram` to `path` and load it. A missing file leaves SRAM zeroed. */
bool smx_sram_open(SmxSram *sram, const char *path, SmxBus *bus);

/** Save SRAM if it differs from the file. True when the file is current. */
bool smx_sram_flush(SmxSram *sram, const SmxBus *bus);

void smx_sram_close(SmxSram *sram);

#endif /* SMX_SRAM_H_ */
