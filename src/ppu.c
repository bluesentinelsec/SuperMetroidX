#include "smx/ppu.h"

void smx_ppu_init(SmxPpu *ppu, SmxBus *bus) {
  SDL_memset(ppu, 0, sizeof(*ppu));
  ppu->bus = bus;
  ppu->pixels = (Uint8 *)SDL_calloc((Uint32)SMX_PPU_WIDTH * SMX_PPU_HEIGHT, 4);
  ppu->picture_matches_oracle = false;
}

void smx_ppu_quit(SmxPpu *ppu) {
  SDL_free(ppu->pixels);
  SDL_memset(ppu, 0, sizeof(*ppu));
}

void smx_ppu_write_cgram(SmxPpu *ppu, Uint8 index, Uint16 bgr555) {
  ppu->cgram[index] = (Uint16)(bgr555 & 0x7FFF);
}

void smx_ppu_render(SmxPpu *ppu) {
  Uint8 inidisp = smx_bus_mmio(ppu->bus, 0x2100);
  bool blank = (inidisp & 0x80) != 0;
  Uint8 brightness = (Uint8)(inidisp & 0x0F);
  Uint16 color = ppu->cgram[0];
  Uint8 blue = (Uint8)(((color & 0x7C00) >> 10) * 255 / 31);
  Uint8 green = (Uint8)(((color & 0x03E0) >> 5) * 255 / 31);
  Uint8 red = (Uint8)((color & 0x001F) * 255 / 31);
  Uint32 index = 0;
  Uint32 pixels = (Uint32)SMX_PPU_WIDTH * SMX_PPU_HEIGHT;

  if (blank || brightness == 0) {
    red = 0;
    green = 0;
    blue = 0;
  } else if (brightness < 15) {
    red = (Uint8)((red * brightness) / 15);
    green = (Uint8)((green * brightness) / 15);
    blue = (Uint8)((blue * brightness) / 15);
  }

  for (; index < pixels; index += 1) {
    ppu->pixels[index * 4] = blue;
    ppu->pixels[index * 4 + 1] = green;
    ppu->pixels[index * 4 + 2] = red;
    ppu->pixels[index * 4 + 3] = 255;
  }
}

Uint32 smx_ppu_frame_hash(const SmxPpu *ppu) {
  Uint32 hash = 2166136261u;
  Uint32 size = (Uint32)SMX_PPU_WIDTH * SMX_PPU_HEIGHT * 4;
  Uint32 index = 0;

  for (; index < size; index += 1) {
    hash ^= ppu->pixels[index];
    hash *= 16777619u;
  }
  return hash;
}
