#ifndef SMX_PPU_H_
#define SMX_PPU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

#define SMX_PPU_WIDTH 256
#define SMX_PPU_HEIGHT 224

/**
 * NTSC picture RAM.
 * The framebuffer is XRGB8888, 256 by 224. Forced blank is black.
 * This is not a mode 0-7 renderer. A frame hash can be compared with an
 * external NTSC log later; nothing here claims that match yet.
 */
typedef struct SmxPpu {
  SmxBus *bus;
  Uint16 cgram[256];
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
