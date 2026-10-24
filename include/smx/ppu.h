#ifndef SMX_PPU_H_
#define SMX_PPU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

#define SMX_PPU_WIDTH 256
#define SMX_PPU_HEIGHT 224

/**
 * NTSC picture RAM.
 * The framebuffer is XRGB8888, 256 by 224. Forced blank is black.
 * Mode 0 and mode 1 backgrounds are sampled, then sprites on the main
 * screen. Scroll and modes 2-7 are not drawn. picture_matches_oracle stays false.
 */
typedef struct SmxPpu {
  SmxBus *bus;
  Uint8 vram[0x10000];
  Uint16 cgram[256];
  Uint8 oam[544];
  Uint16 vram_addr;
  Uint8 vram_inc;
  Uint8 vram_map;
  bool vram_inc_high;
  Uint8 cgram_addr;
  Uint8 cgram_latch;
  bool cgram_high;
  Uint16 oam_addr;
  Uint8 *pixels;
  bool picture_matches_oracle;
} SmxPpu;

void smx_ppu_init(SmxPpu *ppu, SmxBus *bus);
void smx_ppu_quit(SmxPpu *ppu);

/** Write one CGRAM word. `index` is the color number. */
void smx_ppu_write_cgram(SmxPpu *ppu, Uint8 index, Uint16 bgr555);

/** Fill the framebuffer from INIDISP and backdrop color 0. */
void smx_ppu_render(SmxPpu *ppu);

/** FNV-1a of the XRGB framebuffer. */
Uint32 smx_ppu_frame_hash(const SmxPpu *ppu);

#endif /* SMX_PPU_H_ */
