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

/** Write the ROM's file name, without directory or extension, followed by `.srm`. */
void smx_sram_file_name(char *name, Uint32 size, const char *rom_path);

/**
 * Write `<user data>/SuperMetroidX/` plus `smx_sram_file_name` into `path`.
 * The environment variable `SMX_DATA_DIR`, when set, replaces the user-data
 * directory. False when SDL has no user-data directory.
 */
bool smx_sram_default_path(char *path, Uint32 size, const char *rom_path);

/** Bind `bus->sram` to `path` and load it. A missing file leaves SRAM zeroed. */
bool smx_sram_open(SmxSram *sram, const char *path, SmxBus *bus);

/** Save SRAM if it differs from the file. True when the file is current. */
bool smx_sram_flush(SmxSram *sram, const SmxBus *bus);

void smx_sram_close(SmxSram *sram);

#endif /* SMX_SRAM_H_ */
