#ifndef SMX_BUS_H_
#define SMX_BUS_H_

#include <SDL3/SDL.h>

/** Work RAM is the 128 KiB block at $7E:0000. */
#define SMX_BUS_WRAM_SIZE (128u * 1024u)

/**
 * LoROM cartridge bus.
 * `rom` is borrowed and must outlive the bus. WRAM and SRAM are owned.
 */
typedef struct SmxBus {
  const Uint8 *rom;
  Uint32 rom_size;
  Uint8 *wram;
  Uint8 *sram;
  Uint32 sram_size;
  Uint8 mmio[0x4500];
  Uint8 open_bus;
  bool dma_requested;
  Uint8 dma_channels;
  void (*mmio_write)(void *user, Uint16 addr, Uint8 value);
  Uint8 (*mmio_read)(void *user, Uint16 addr, Uint8 open_bus);
  void *mmio_user;
  void (*apu_write)(void *user, Uint8 index, Uint8 value);
  void *apu_user;
  /** Called before a write to `$2100-$2133` so the picture catches up to the beam. */
  void (*ppu_sync)(void *user);
  void *sync_user;
} SmxBus;

void smx_bus_init(SmxBus *bus, const Uint8 *rom, Uint32 rom_size, Uint32 sram_size);
void smx_bus_quit(SmxBus *bus);

Uint8 smx_bus_read(SmxBus *bus, Uint32 addr);
void smx_bus_write(SmxBus *bus, Uint32 addr, Uint8 value);

/**
 * Master clocks one 65816 access to `addr` takes.
 * WRAM, SRAM, slow ROM, and the bank $40-$7F space are 8. `$4000-$41FF` is 12.
 * The other I/O pages are 6. ROM at $80-$FF:8000 and $C0-$FF is 6 when `$420D`
 * bit 0 is set.
 */
Uint8 smx_bus_speed(const SmxBus *bus, Uint32 addr);

/** Latched MMIO value, including write-only registers. */
Uint8 smx_bus_mmio(const SmxBus *bus, Uint16 addr);

/** Direct WRAM byte. `addr` is in the range 0 to `SMX_BUS_WRAM_SIZE` - 1. */
Uint8 smx_bus_wram(const SmxBus *bus, Uint32 addr);

#endif /* SMX_BUS_H_ */
