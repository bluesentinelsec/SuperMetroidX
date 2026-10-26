#ifndef SMX_STATE_H_
#define SMX_STATE_H_

#include "smx/apu.h"
#include "smx/bus.h"
#include "smx/clock.h"
#include "smx/cpu.h"
#include "smx/dma.h"
#include "smx/ppu.h"

#include <SDL3/SDL.h>

/** The parts of a running console a save state covers. All are borrowed. */
typedef struct SmxMachine {
  SmxBus *bus;
  SmxCpu *cpu;
  SmxDma *dma;
  SmxPpu *ppu;
  SmxApu *apu;
  SmxClock *clock;
} SmxMachine;

/**
 * Save states hold the whole machine except the cartridge: CPU, WRAM, I/O
 * registers, PPU memory and registers, DMA, the SPC700 and S-DSP, and the
 * clock. SRAM is left out, so loading a state never rewinds the game's own
 * saves. A state is tied to the ROM it was made with and to the layout of
 * this build's structures; bump `SMX_STATE_VERSION` when a saved structure
 * changes meaning without changing size. Restoring keeps every pointer and
 * hook of the live machine.
 */
#define SMX_STATE_VERSION 1u

/** Bytes a state of this build takes. */
Uint32 smx_state_size(void);

/** Write the machine into `out`, which holds `smx_state_size()` bytes. */
void smx_state_capture(const SmxMachine *machine, Uint32 rom_hash, Uint8 *out);

/** Restore a captured state. False, changing nothing, when it is from another ROM or build. */
bool smx_state_restore(SmxMachine *machine, Uint32 rom_hash, const Uint8 *in, Uint32 size);

/** Capture into a file at `path`, written to a temporary name and renamed. */
bool smx_state_save(const SmxMachine *machine, Uint32 rom_hash, const char *path);

/** Restore from a file. False when it is missing or does not fit this ROM and build. */
bool smx_state_load(SmxMachine *machine, Uint32 rom_hash, const char *path);

/** The quick-save state beside an SRAM file: `sram_path` with `.srm` replaced by `.state`. */
void smx_state_path(char *path, Uint32 size, const char *sram_path);

#endif /* SMX_STATE_H_ */
