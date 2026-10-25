#ifndef SMX_PPU_H_
#define SMX_PPU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

#define SMX_PPU_WIDTH 256
#define SMX_PPU_HEIGHT 224

/**
 * NTSC picture RAM.
 * The framebuffer is XRGB8888, 256 by 224. Forced blank is black.
 * Mode 0, mode 1, mode 2, mode 3, mode 4, and mode 7 backgrounds are sampled,
 * then sprites on the main screen, including mode 1 BG3. Mode 2 uses 4bpp
 * BG1 and BG2. Modes 2 and 4 take per-column scroll from the BG3 map. The
 * first screen column keeps the layer scroll. Mode 3 and mode 4 BG1 are 8bpp.
 * Mode 4 BG2 is 2bpp. Modes 5 and 6 keep a 256-wide picture and sample every
 * other hi-res pixel: the main screen takes the even pixels and the subscreen
 * takes the odd pixels. $2130 bit 0 selects
 * direct color. `$2103` bit 7 draws sprites starting at the OAM word address.
 * Mode 7 uses the latched
 * matrix. `$211A` bits 6-7 select the area outside 1024x1024: 2 is
 * transparent and 3 repeats character 0. Modes 0 and 1 scroll. A vertical write between the two
 * horizontal bytes keeps the fine horizontal offset.
 * HDMA can rewrite those registers per line. $2123-$212B and $212E clip
 * main-screen layers. $212F clips the subscreen the same way.
 * $2131 adds or subtracts the fixed color from $2132.
 * Mode 0 and mode 1 use the hardware layer order. $2105 bits 4-7 select
 * 16x16 characters. Mode 1 $2105 bit 3
 * draws priority BG3 above sprites. $2106 mosaics
 * the background layers it enables. `$2130` bit 1 blends the main
 * pixel with the subscreen from `$212D` instead of the fixed color.
 * Bits 4-7 of $2130 black out the main pixel or skip color math in the
 * color window. A finished visible line is drawn before the next line's
 * HDMA. picture_matches_oracle stays false.
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
  Uint16 oam_base;
  bool oam_rotate;
  Uint8 m7_low;
  bool m7_have_low;
  Uint8 bg_ofs;
  Uint8 bg_hofs;
  Uint16 m7a;
  Uint16 m7b;
  Uint16 m7c;
  Uint16 m7d;
  Uint16 m7x;
  Uint16 m7y;
  Uint16 m7h;
  Uint16 m7v;
  Uint16 bg_h[4];
  Uint16 bg_v[4];
  Uint8 fixed_r;
  Uint8 fixed_g;
  Uint8 fixed_b;
  Uint8 *pixels;
  bool picture_matches_oracle;
} SmxPpu;

void smx_ppu_init(SmxPpu *ppu, SmxBus *bus);
void smx_ppu_quit(SmxPpu *ppu);

/** Write one CGRAM word. `index` is the color number. */
void smx_ppu_write_cgram(SmxPpu *ppu, Uint8 index, Uint16 bgr555);

/** Draw scanline `y` from the registers as they are now. */
void smx_ppu_render_line(SmxPpu *ppu, Uint32 y);

/** Fill every visible line from the current registers. */
void smx_ppu_render(SmxPpu *ppu);

/** FNV-1a of the XRGB framebuffer. */
Uint32 smx_ppu_frame_hash(const SmxPpu *ppu);

#endif /* SMX_PPU_H_ */
