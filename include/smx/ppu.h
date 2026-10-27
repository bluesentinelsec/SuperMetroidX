#ifndef SMX_PPU_H_
#define SMX_PPU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

#define SMX_PPU_WIDTH 256
#define SMX_PPU_HEIGHT 224
/** Background tile columns fetched per line: 32 on screen plus one for the fine scroll. */
#define SMX_PPU_COLUMNS 33

/**
 * One background tile column as the PPU fetched it. The map entry and the
 * scroll it was fetched with, offset-per-tile included, come from the map fetch; `indices` are the
 * character row's pixels in the character's own left-to-right order (two characters in hi-res),
 * decoded from the character fetches.
 */
typedef struct SmxPpuColumn {
  Uint16 entry;
  Uint16 hscroll;
  Uint16 vscroll;
  Uint8 chr[16];
  Uint8 indices[16];
} SmxPpuColumn;

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
 * Mode 7 commits each matrix or scroll byte immediately. The previous
 * byte is the low half. `$2134`–`$2136` are M7A times the signed high
 * byte of M7B. `$211A` bits 6-7 select the area outside 1024x1024: 2 is
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
 * HDMA. The picture stores scanlines 1 through 224. Scanline 0 is hidden.
 * `$2139`/`$213A` read `vram_latch`, which an address write or the read
 * that steps the address refills. `$213B` reads CGRAM low then high, and
 * `$2138` reads OAM.
 * Character fetches wrap at the end of the 64 KiB VRAM.
 * Five-bit channels round to eight bits. picture_matches_oracle latches
 * once the picture matches the NTSC logo frame.
 * Backgrounds are fetched ahead of drawing, a tile column at a time on a
 * fixed schedule of dots per mode: a register write changes only the columns
 * fetched after it, while the fine scroll and the colors apply as each pixel
 * is drawn. In mode 0 each background has its own 32 colors.
 * Outside forced blank, lines 1-224 read CGRAM all through the drawing
 * period (master clocks 88 to 1095), and a CGRAM access then reaches the
 * entry the PPU is reading instead of the address.
 * `beam_line`, `beam_dot`, and `beam_field`, when a clock sets them, are
 * the scanline, master clock in the line, and field being drawn. VRAM can
 * then be written and read only in vblank or forced blank; at other times
 * a write is dropped, a read gets 0, and the address still steps.
 * `$2137` (with `$4201` bit 7 set) latches the H/V counters for `$213C` and
 * `$213D`, which return them low byte then high. `$213E` and `$213F` report
 * sprite time and range overflow this frame, the field, the latch, NTSC,
 * and the chip versions. Each PPU chip keeps its own open bus.
 */
typedef struct SmxPpu {
  SmxBus *bus;
  Uint8 vram[0x10000];
  Uint16 cgram[256];
  Uint8 oam[544];
  Uint16 vram_addr;
  Uint16 vram_latch;
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
  Uint16 obj_color[256];
  Uint8 obj_prio[256];
  /** Sprite pixels from palettes 4-7, the only ones color math applies to. */
  bool obj_math[256];
  /** The CGRAM entry of each sprite pixel. */
  Uint8 obj_index[256];
  /** Background columns fetched on the line `fetch_key` names, through dot `fetch_dot`. */
  SmxPpuColumn columns[4][SMX_PPU_COLUMNS];
  Uint32 fetch_key;
  Uint16 fetch_dot;
  /** The latest offset-per-tile words, for the next column's map fetch. */
  Uint16 opt_h;
  Uint16 opt_v;
  /** Finished pictures, which tells one frame's line from the next. */
  Uint32 pictures;
  /** The CGRAM entry the PPU read last, where writes land while it draws. */
  Uint8 cgram_reading;
  /** The picture being drawn, XRGB8888. */
  Uint8 *pixels;
  /** The last finished picture: `pixels` as vblank began. Present and hash this one. */
  Uint8 *picture;
  bool picture_matches_oracle;
  const Uint32 *beam_line;
  const Uint32 *beam_dot;
  const bool *beam_field;
  Uint8 ppu1_open;
  Uint8 ppu2_open;
  bool time_over;
  bool range_over;
  /** The line being evaluated has run out of sprite tiles. */
  bool time_over_line;
  bool latched;
  bool h_toggle;
  bool v_toggle;
  Uint16 h_latch;
  Uint16 v_latch;
  /** Counts CGRAM writes, so a copy of CGRAM can tell it is stale. */
  Uint32 cgram_serial;
  /**
   * Called before each visible scanline is drawn, with the registers it is
   * drawn with, and when a picture is finished. NULL for none. Saves states
   * keep the hooks of the machine they load into.
   */
  void (*line_hook)(void *user, const struct SmxPpu *ppu, Uint32 scanline);
  void (*picture_hook)(void *user, const struct SmxPpu *ppu);
  void *hook_user;
} SmxPpu;

void smx_ppu_init(SmxPpu *ppu, SmxBus *bus);
void smx_ppu_quit(SmxPpu *ppu);

/** Write one CGRAM word. `index` is the color number. */
void smx_ppu_write_cgram(SmxPpu *ppu, Uint8 index, Uint16 bgr555);

/** Draw scanline `y` into picture row `y`. */
void smx_ppu_render_line(SmxPpu *ppu, Uint32 y);

/**
 * Draw PPU scanline `scanline` into the visible picture.
 * Scanline 0 is hidden. Scanlines 1 through 224 fill rows 0 through 223.
 */
void smx_ppu_present_line(SmxPpu *ppu, Uint32 scanline);

/**
 * Fetch background tiles of `scanline` through PPU dot `dot` (master clock
 * divided by 4), as the PPU does ahead of drawing. The clock calls this
 * before every PPU register write; drawing fetches whatever is still due.
 */
void smx_ppu_fetch(SmxPpu *ppu, Uint32 scanline, Uint32 dot);

/** Draw pixels `x_from` up to but not including `x_to` of a visible scanline. */
void smx_ppu_present_span(SmxPpu *ppu, Uint32 scanline, Uint32 x_from, Uint32 x_to);

/** Fill every visible line from the current registers, then finish the picture. */
void smx_ppu_render(SmxPpu *ppu);

/**
 * Vblank has begun: copy the drawn lines to `picture`. The CPU can run a DMA
 * past the end of a frame, so the lines drawn after vblank may already belong
 * to the next picture by the time a frame is presented.
 */
void smx_ppu_end_picture(SmxPpu *ppu);

/** FNV-1a of the finished XRGB picture. */
Uint32 smx_ppu_frame_hash(const SmxPpu *ppu);

/** Latch the H/V counters at the beam, for `$213C` and `$213D`. */
void smx_ppu_latch_counters(SmxPpu *ppu);

/** Latch picture_matches_oracle when the picture is the NTSC logo frame. */
void smx_ppu_note_picture(SmxPpu *ppu);

#endif /* SMX_PPU_H_ */
