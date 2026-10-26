#include "smx/bus.h"

void smx_bus_init(SmxBus *bus, const Uint8 *rom, Uint32 rom_size, Uint32 sram_size) {
  SDL_memset(bus, 0, sizeof(*bus));
  bus->rom = rom;
  bus->rom_size = rom_size;
  bus->wram = (Uint8 *)SDL_calloc(1, SMX_BUS_WRAM_SIZE);
  bus->sram_size = sram_size;
  if (sram_size > 0) {
    bus->sram = (Uint8 *)SDL_calloc(1, sram_size);
  }
}

void smx_bus_quit(SmxBus *bus) {
  SDL_free(bus->wram);
  SDL_free(bus->sram);
  SDL_memset(bus, 0, sizeof(*bus));
}

Uint8 smx_bus_mmio(const SmxBus *bus, Uint16 addr) {
  if (addr >= sizeof(bus->mmio)) {
    return 0;
  }
  return bus->mmio[addr];
}

Uint8 smx_bus_wram(const SmxBus *bus, Uint32 addr) {
  if (bus->wram == NULL || addr >= SMX_BUS_WRAM_SIZE) {
    return 0;
  }
  return bus->wram[addr];
}

Uint8 smx_bus_speed(const SmxBus *bus, Uint32 addr) {
  Uint8 bank = (Uint8)(addr >> 16);
  Uint16 off = (Uint16)addr;

  if ((bank & 0x40) != 0 || off >= 0x8000) {
    if ((bank & 0x80) != 0 && (bus->mmio[0x420D] & 0x01) != 0) {
      return 6;
    }
    return 8;
  }
  if (off < 0x2000 || off >= 0x6000) {
    return 8;
  }
  if (off >= 0x4000 && off < 0x4200) {
    return 12;
  }
  return 6;
}

static bool smx_bus_rom_offset(Uint8 bank, Uint16 off, Uint32 *file) {
  Uint8 mapped = (Uint8)(bank & 0x7F);

  if (off < 0x8000 || mapped >= 0x7E) {
    return false;
  }
  *file = ((Uint32)mapped << 15) | (Uint32)(off & 0x7FFF);
  return true;
}

static bool smx_bus_sram_offset(const SmxBus *bus, Uint8 bank, Uint16 off, Uint32 *index) {
  Uint8 mapped = (Uint8)(bank & 0x7F);

  if (bus->sram == NULL || bus->sram_size == 0 || off >= 0x8000) {
    return false;
  }
  if (mapped < 0x70 || mapped > 0x7D) {
    return false;
  }
  *index = (((Uint32)(mapped - 0x70) << 15) | off) % bus->sram_size;
  return true;
}

Uint8 smx_bus_read(SmxBus *bus, Uint32 addr) {
  Uint8 bank = (Uint8)(addr >> 16);
  Uint16 off = (Uint16)addr;
  Uint32 file = 0;
  Uint8 value = bus->open_bus;

  if (bank == 0x7E || bank == 0x7F) {
    value = bus->wram[((Uint32)(bank & 1) << 16) | off];
  } else if ((bank & 0x7F) < 0x40 && off < 0x2000) {
    value = bus->wram[off];
  } else if (smx_bus_sram_offset(bus, bank, off, &file)) {
    value = bus->sram[file];
  } else if (smx_bus_rom_offset(bank, off, &file) && file < bus->rom_size) {
    value = bus->rom[file];
  } else if ((bank & 0x7F) < 0x40 && off >= 0x2100 && off < 0x4500) {
    if (bus->time_sync != NULL &&
        ((off >= 0x2137 && off <= 0x213F) || (off >= 0x4210 && off <= 0x421F))) {
      bus->time_sync(bus->sync_user);
    }
    /* Status registers are readable. Write-only PPU registers read as open bus.
     * The PPU answers `$2134-$213F` and returns open bus for what it does not model. */
    if (off >= 0x2134 && off <= 0x213F && bus->mmio_read != NULL) {
      value = bus->mmio_read(bus->mmio_user, off, bus->open_bus);
    } else if ((off >= 0x2140 && off <= 0x2143) || (off >= 0x4210 && off <= 0x421F)) {
      if (off <= 0x2143 && bus->apu_sync != NULL) {
        bus->apu_sync(bus->sync_user, false);
      }
      value = bus->mmio[off];
      /* Reading $4210 acknowledges the NMI flag, and $4211 the IRQ. */
      if (off == 0x4210 || off == 0x4211) {
        bus->mmio[off] = (Uint8)(bus->mmio[off] & 0x7F);
      }
    }
  }

  bus->open_bus = value;
  return value;
}

void smx_bus_write(SmxBus *bus, Uint32 addr, Uint8 value) {
  Uint8 bank = (Uint8)(addr >> 16);
  Uint16 off = (Uint16)addr;
  Uint32 file = 0;

  bus->open_bus = value;
  if (bank == 0x7E || bank == 0x7F) {
    bus->wram[((Uint32)(bank & 1) << 16) | off] = value;
    return;
  }
  if ((bank & 0x7F) < 0x40 && off < 0x2000) {
    bus->wram[off] = value;
    return;
  }
  if (smx_bus_sram_offset(bus, bank, off, &file)) {
    bus->sram[file] = value;
    return;
  }
  if ((bank & 0x7F) < 0x40 && off >= 0x2100 && off < 0x4500) {
    if (off <= 0x2133 && bus->ppu_sync != NULL) {
      bus->ppu_sync(bus->sync_user);
    }
    if (off >= 0x2140 && off <= 0x2143 && bus->apu_sync != NULL) {
      bus->apu_sync(bus->sync_user, true);
    }
    bus->mmio[off] = value;
    if (off == 0x420B && value != 0) {
      bus->dma_requested = true;
      bus->dma_channels = value;
    }
    if (off == 0x4203) {
      Uint16 product = (Uint16)((Uint16)bus->mmio[0x4202] * value);
      bus->mmio[0x4216] = (Uint8)product;
      bus->mmio[0x4217] = (Uint8)(product >> 8);
    }
    if (off == 0x4206) {
      Uint16 dividend = (Uint16)(bus->mmio[0x4204] | ((Uint16)bus->mmio[0x4205] << 8));
      Uint16 quotient = 0xFFFF;
      Uint16 remainder = dividend;
      if (value != 0) {
        quotient = (Uint16)(dividend / value);
        remainder = (Uint16)(dividend % value);
      }
      bus->mmio[0x4214] = (Uint8)quotient;
      bus->mmio[0x4215] = (Uint8)(quotient >> 8);
      bus->mmio[0x4216] = (Uint8)remainder;
      bus->mmio[0x4217] = (Uint8)(remainder >> 8);
    }
    if (off <= 0x21FF && bus->mmio_write != NULL) {
      bus->mmio_write(bus->mmio_user, off, value);
    }
    if (off >= 0x2140 && off <= 0x2143 && bus->apu_write != NULL) {
      bus->apu_write(bus->apu_user, (Uint8)(off - 0x2140), value);
    }
  }
}
