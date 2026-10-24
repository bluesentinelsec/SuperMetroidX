#include "smx/ppu.h"

static void smx_ppu_on_write(void *user, Uint16 addr, Uint8 value);

static Uint16 smx_ppu_translate(const SmxPpu *ppu, Uint16 addr) {
  addr = (Uint16)(addr & 0x7FFF);
  if (ppu->vram_map == 1) {
    return (Uint16)((addr & 0xFF00) | ((addr & 0x001F) << 3) | ((addr & 0x00E0) >> 5));
  }
  if (ppu->vram_map == 2) {
    return (Uint16)((addr & 0xFE00) | ((addr & 0x003F) << 3) | ((addr & 0x01C0) >> 6));
  }
  if (ppu->vram_map == 3) {
    return (Uint16)((addr & 0xFC00) | ((addr & 0x007F) << 3) | ((addr & 0x0380) >> 7));
  }
  return addr;
}

static void smx_ppu_vram_step(SmxPpu *ppu) {
  Uint16 step = 1;
  if (ppu->vram_inc == 1) {
    step = 32;
  } else if (ppu->vram_inc >= 2) {
    step = 128;
  }
  ppu->vram_addr = (Uint16)((ppu->vram_addr + step) & 0x7FFF);
}

static void smx_ppu_store_vram(SmxPpu *ppu, bool high, Uint8 value) {
  Uint16 word = smx_ppu_translate(ppu, ppu->vram_addr);
  ppu->vram[(Uint32)word * 2u + (high ? 1u : 0u)] = value;
  if (high == ppu->vram_inc_high) {
    smx_ppu_vram_step(ppu);
  }
}

void smx_ppu_init(SmxPpu *ppu, SmxBus *bus) {
  SDL_memset(ppu, 0, sizeof(*ppu));
  ppu->bus = bus;
  ppu->pixels = (Uint8 *)SDL_calloc((Uint32)SMX_PPU_WIDTH * SMX_PPU_HEIGHT, 4);
  ppu->picture_matches_oracle = false;
  if (bus != NULL) {
    bus->mmio_write = smx_ppu_on_write;
    bus->mmio_user = ppu;
    bus->mmio[0x2100] = 0x8F;
  }
}

void smx_ppu_quit(SmxPpu *ppu) {
  if (ppu->bus != NULL && ppu->bus->mmio_user == ppu) {
    ppu->bus->mmio_write = NULL;
    ppu->bus->mmio_user = NULL;
  }
  SDL_free(ppu->pixels);
  SDL_memset(ppu, 0, sizeof(*ppu));
}

void smx_ppu_write_cgram(SmxPpu *ppu, Uint8 index, Uint16 bgr555) {
  ppu->cgram[index] = (Uint16)(bgr555 & 0x7FFF);
}

static void smx_ppu_on_write(void *user, Uint16 addr, Uint8 value) {
  SmxPpu *ppu = (SmxPpu *)user;

  if (addr == 0x2115) {
    ppu->vram_inc = (Uint8)(value & 0x03);
    ppu->vram_map = (Uint8)((value >> 2) & 0x03);
    ppu->vram_inc_high = (value & 0x80) != 0;
  } else if (addr == 0x2116) {
    ppu->vram_addr = (Uint16)((ppu->vram_addr & 0xFF00) | value);
  } else if (addr == 0x2117) {
    ppu->vram_addr = (Uint16)((ppu->vram_addr & 0x00FF) | ((Uint16)value << 8));
  } else if (addr == 0x2118) {
    smx_ppu_store_vram(ppu, false, value);
  } else if (addr == 0x2119) {
    smx_ppu_store_vram(ppu, true, value);
  } else if (addr == 0x2121) {
    ppu->cgram_addr = value;
    ppu->cgram_high = false;
  } else if (addr == 0x2122) {
    if (!ppu->cgram_high) {
      ppu->cgram_latch = value;
      ppu->cgram_high = true;
    } else {
      smx_ppu_write_cgram(ppu, ppu->cgram_addr, (Uint16)(ppu->cgram_latch | ((Uint16)value << 8)));
      ppu->cgram_addr = (Uint8)(ppu->cgram_addr + 1);
      ppu->cgram_high = false;
    }
  } else if (addr == 0x2102) {
    ppu->oam_addr = (Uint16)((ppu->oam_addr & 0xFF00) | value);
  } else if (addr == 0x2103) {
    ppu->oam_addr = (Uint16)((ppu->oam_addr & 0x00FF) | ((Uint16)(value & 0x01) << 8));
  } else if (addr == 0x2104) {
    if (ppu->oam_addr < sizeof(ppu->oam)) {
      ppu->oam[ppu->oam_addr] = value;
    }
    ppu->oam_addr = (Uint16)((ppu->oam_addr + 1) & 0x01FF);
  }
}

static void smx_ppu_rgb(Uint16 bgr, Uint8 brightness, Uint8 *blue, Uint8 *green, Uint8 *red) {
  Uint8 b = (Uint8)(((bgr & 0x7C00) >> 10) * 255 / 31);
  Uint8 g = (Uint8)(((bgr & 0x03E0) >> 5) * 255 / 31);
  Uint8 r = (Uint8)((bgr & 0x001F) * 255 / 31);
  if (brightness < 15) {
    b = (Uint8)((b * brightness) / 15);
    g = (Uint8)((g * brightness) / 15);
    r = (Uint8)((r * brightness) / 15);
  }
  *blue = b;
  *green = g;
  *red = r;
}

static Uint16 smx_ppu_word(const SmxPpu *ppu, Uint16 addr) {
  Uint32 byte = (Uint32)(addr & 0x7FFF) * 2u;
  return (Uint16)(ppu->vram[byte] | ((Uint16)ppu->vram[byte + 1] << 8));
}

static Uint8 smx_ppu_plane(const SmxPpu *ppu, Uint16 base_word, Uint16 tile, Uint8 row, Uint8 col,
                           bool four) {
  Uint32 base = ((Uint32)(base_word & 0x7FFF) * 2u) + (Uint32)tile * (four ? 32u : 16u);
  Uint8 bit = (Uint8)(7u - (col & 7u));
  Uint8 p0 = ppu->vram[base + (Uint32)row * 2u];
  Uint8 p1 = ppu->vram[base + (Uint32)row * 2u + 1u];
  Uint8 index = (Uint8)(((p0 >> bit) & 1u) | (((p1 >> bit) & 1u) << 1));
  if (four) {
    Uint8 p2 = ppu->vram[base + 16u + (Uint32)row * 2u];
    Uint8 p3 = ppu->vram[base + 16u + (Uint32)row * 2u + 1u];
    index = (Uint8)(index | (((p2 >> bit) & 1u) << 2) | (((p3 >> bit) & 1u) << 3));
  }
  return index;
}

static void smx_ppu_sample_bg(const SmxPpu *ppu, Uint8 map_reg, Uint8 char_nibble, bool four,
                              Uint32 x, Uint32 y, bool *opaque, bool *priority, Uint16 *color) {
  Uint16 map = (Uint16)((map_reg & 0xFC) << 8);
  Uint16 chars = (Uint16)((char_nibble & 0x0F) << 12);
  Uint32 tx = (x >> 3) & 31u;
  Uint32 ty = (y >> 3) & 31u;
  Uint16 entry = smx_ppu_word(ppu, (Uint16)(map + ty * 32u + tx));
  Uint16 tile = (Uint16)(entry & 0x03FF);
  Uint8 palette = (Uint8)((entry >> 10) & 0x07);
  Uint8 row = (Uint8)(y & 7u);
  Uint8 col = (Uint8)(x & 7u);
  Uint8 index = 0;

  if ((entry & 0x8000) != 0) {
    row = (Uint8)(7u - row);
  }
  if ((entry & 0x4000) != 0) {
    col = (Uint8)(7u - col);
  }
  index = smx_ppu_plane(ppu, chars, tile, row, col, four);
  *priority = (entry & 0x2000) != 0;
  *opaque = index != 0;
  if (four) {
    *color = ppu->cgram[(Uint32)palette * 16u + index];
  } else {
    *color = ppu->cgram[(Uint32)palette * 4u + index];
  }
}

void smx_ppu_render(SmxPpu *ppu) {
  Uint8 inidisp = smx_bus_mmio(ppu->bus, 0x2100);
  bool blank = (inidisp & 0x80) != 0;
  Uint8 brightness = (Uint8)(inidisp & 0x0F);
  Uint8 mode = (Uint8)(smx_bus_mmio(ppu->bus, 0x2105) & 0x07);
  Uint8 main_screen = smx_bus_mmio(ppu->bus, 0x212C);
  Uint32 y = 0;
  static const Uint16 map_regs[4] = {0x2107, 0x2108, 0x2109, 0x210A};

  for (; y < SMX_PPU_HEIGHT; y += 1) {
    Uint32 x = 0;
    for (; x < SMX_PPU_WIDTH; x += 1) {
      Uint16 color = ppu->cgram[0];
      Uint8 blue = 0;
      Uint8 green = 0;
      Uint8 red = 0;
      Uint8 pass = 0;

      /* Back to front: low priority BG4..BG1, then high priority BG4..BG1. */
      if (!blank && brightness != 0 && (mode == 0 || mode == 1)) {
        for (; pass < 2; pass += 1) {
          Sint8 bg = 3;
          for (; bg >= 0; bg -= 1) {
            bool four = mode == 1 && bg < 2;
            bool opaque = false;
            bool priority = false;
            Uint16 sample = 0;
            Uint8 char_reg = 0;
            Uint8 nibble = 0;
            if (mode == 1 && bg == 3) {
              continue;
            }
            if ((main_screen & (Uint8)(1u << bg)) == 0) {
              continue;
            }
            char_reg = smx_bus_mmio(ppu->bus, bg < 2 ? 0x210B : 0x210C);
            nibble = (bg & 1) == 0 ? (Uint8)(char_reg & 0x0F) : (Uint8)(char_reg >> 4);
            smx_ppu_sample_bg(ppu, smx_bus_mmio(ppu->bus, map_regs[bg]), nibble, four, x, y,
                              &opaque, &priority, &sample);
            if (opaque && priority == (pass == 1)) {
              color = sample;
            }
          }
        }
      }
      if (blank || brightness == 0) {
        blue = 0;
        green = 0;
        red = 0;
      } else {
        smx_ppu_rgb(color, brightness, &blue, &green, &red);
      }
      ppu->pixels[(y * SMX_PPU_WIDTH + x) * 4u] = blue;
      ppu->pixels[(y * SMX_PPU_WIDTH + x) * 4u + 1u] = green;
      ppu->pixels[(y * SMX_PPU_WIDTH + x) * 4u + 2u] = red;
      ppu->pixels[(y * SMX_PPU_WIDTH + x) * 4u + 3u] = 255;
    }
  }
}

Uint32 smx_ppu_frame_hash(const SmxPpu *ppu) {
  Uint32 hash = 2166136261u;
  Uint32 i = 0;
  Uint32 count = (Uint32)SMX_PPU_WIDTH * SMX_PPU_HEIGHT * 4u;
  for (; i < count; i += 1) {
    hash ^= ppu->pixels[i];
    hash *= 16777619u;
  }
  return hash;
}
