#include "smx/ppu.h"

static void smx_ppu_on_write(void *user, Uint16 addr, Uint8 value);
static Uint8 smx_ppu_on_read(void *user, Uint16 addr, Uint8 open_bus);

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

/* VRAM is open to the CPU only in vblank or forced blank. */
static bool smx_ppu_vram_open(const SmxPpu *ppu) {
  return ppu->beam_line == NULL || *ppu->beam_line >= 225u ||
         (smx_bus_mmio(ppu->bus, 0x2100) & 0x80) != 0;
}

static void smx_ppu_store_vram(SmxPpu *ppu, bool high, Uint8 value) {
  Uint16 word = smx_ppu_translate(ppu, ppu->vram_addr);
  if (smx_ppu_vram_open(ppu)) {
    ppu->vram[(Uint32)word * 2u + (high ? 1u : 0u)] = value;
  }
  if (high == ppu->vram_inc_high) {
    smx_ppu_vram_step(ppu);
  }
}

static void smx_ppu_prefetch(SmxPpu *ppu) {
  Uint32 word = smx_ppu_translate(ppu, ppu->vram_addr);
  if (!smx_ppu_vram_open(ppu)) {
    ppu->vram_latch = 0;
    return;
  }
  ppu->vram_latch = (Uint16)(ppu->vram[word * 2u] | ((Uint16)ppu->vram[word * 2u + 1u] << 8));
}

/* Reads return the prefetched word and refill it before the address steps. */
static Uint8 smx_ppu_load_vram(SmxPpu *ppu, bool high) {
  Uint8 value = (Uint8)(high ? ppu->vram_latch >> 8 : ppu->vram_latch);
  if (high == ppu->vram_inc_high) {
    smx_ppu_prefetch(ppu);
    smx_ppu_vram_step(ppu);
  }
  return value;
}

void smx_ppu_init(SmxPpu *ppu, SmxBus *bus) {
  SDL_memset(ppu, 0, sizeof(*ppu));
  ppu->bus = bus;
  ppu->pixels = (Uint8 *)SDL_calloc((Uint32)SMX_PPU_WIDTH * SMX_PPU_HEIGHT, 4);
  ppu->picture_matches_oracle = false;
  if (bus != NULL) {
    bus->mmio_write = smx_ppu_on_write;
    bus->mmio_read = smx_ppu_on_read;
    bus->mmio_user = ppu;
    bus->mmio[0x2100] = 0x8F;
  }
}

void smx_ppu_quit(SmxPpu *ppu) {
  if (ppu->bus != NULL && ppu->bus->mmio_user == ppu) {
    ppu->bus->mmio_write = NULL;
    ppu->bus->mmio_read = NULL;
    ppu->bus->mmio_user = NULL;
  }
  SDL_free(ppu->pixels);
  SDL_memset(ppu, 0, sizeof(*ppu));
}

void smx_ppu_write_cgram(SmxPpu *ppu, Uint8 index, Uint16 bgr555) {
  ppu->cgram[index] = (Uint16)(bgr555 & 0x7FFF);
}

/* The PPU dot of a master clock in the line: dots 323 and 327 are 6 clocks long. */
static Uint16 smx_ppu_dot(Uint32 clock) {
  if (clock <= 1292u) {
    return (Uint16)(clock >> 2);
  }
  if (clock <= 1310u) {
    return (Uint16)((clock - 2u) >> 2);
  }
  return (Uint16)((clock - 4u) >> 2);
}

void smx_ppu_latch_counters(SmxPpu *ppu) {
  if (ppu->beam_line == NULL || ppu->beam_dot == NULL) {
    return;
  }
  ppu->h_latch = smx_ppu_dot(*ppu->beam_dot);
  ppu->v_latch = (Uint16)*ppu->beam_line;
  ppu->latched = true;
}

/* A 9-bit counter, low byte first; the high byte's other bits are PPU2 open bus. */
static Uint8 smx_ppu_counter_byte(SmxPpu *ppu, Uint16 counter, bool *toggle) {
  Uint8 value =
      *toggle ? (Uint8)(((counter >> 8) & 0x01) | (ppu->ppu2_open & 0xFE)) : (Uint8)counter;
  *toggle = !*toggle;
  return value;
}

static Uint8 smx_ppu_on_read(void *user, Uint16 addr, Uint8 open_bus) {
  SmxPpu *ppu = (SmxPpu *)user;
  Sint16 b_high = (Sint16)((Sint16)ppu->m7b >> 8);
  Sint32 product = (Sint32)(Sint16)ppu->m7a * (Sint32)b_high;
  Uint8 value = open_bus;

  switch (addr) {
  case 0x2134:
    ppu->ppu1_open = (Uint8)product;
    return ppu->ppu1_open;
  case 0x2135:
    ppu->ppu1_open = (Uint8)(product >> 8);
    return ppu->ppu1_open;
  case 0x2136:
    ppu->ppu1_open = (Uint8)(product >> 16);
    return ppu->ppu1_open;
  case 0x2137:
    if ((smx_bus_mmio(ppu->bus, 0x4201) & 0x80) != 0) {
      smx_ppu_latch_counters(ppu);
    }
    return open_bus;
  case 0x2138:
    value = ppu->oam_addr < sizeof(ppu->oam) ? ppu->oam[ppu->oam_addr] : 0;
    ppu->oam_addr = (Uint16)((ppu->oam_addr + 1) & 0x03FF);
    ppu->ppu1_open = value;
    return value;
  case 0x2139:
  case 0x213A:
    ppu->ppu1_open = smx_ppu_load_vram(ppu, addr == 0x213A);
    return ppu->ppu1_open;
  case 0x213B: {
    Uint16 color = ppu->cgram[ppu->cgram_addr];
    if (!ppu->cgram_high) {
      ppu->cgram_high = true;
      value = (Uint8)color;
    } else {
      ppu->cgram_high = false;
      ppu->cgram_addr = (Uint8)(ppu->cgram_addr + 1);
      value = (Uint8)(((color >> 8) & 0x7F) | (ppu->ppu2_open & 0x80));
    }
    ppu->ppu2_open = value;
    return value;
  }
  case 0x213C:
    ppu->ppu2_open = smx_ppu_counter_byte(ppu, ppu->h_latch, &ppu->h_toggle);
    return ppu->ppu2_open;
  case 0x213D:
    ppu->ppu2_open = smx_ppu_counter_byte(ppu, ppu->v_latch, &ppu->v_toggle);
    return ppu->ppu2_open;
  case 0x213E:
    ppu->ppu1_open = (Uint8)((ppu->time_over ? 0x80 : 0) | (ppu->range_over ? 0x40 : 0) |
                             (ppu->ppu1_open & 0x10) | 0x01);
    return ppu->ppu1_open;
  case 0x213F:
    /* NTSC, PPU2 version 3. Reading resets both counter toggles. */
    value = (Uint8)((ppu->beam_field != NULL && *ppu->beam_field ? 0x80 : 0) |
                    (ppu->latched ? 0x40 : 0) | (ppu->ppu2_open & 0x20) | 0x03);
    if ((smx_bus_mmio(ppu->bus, 0x4201) & 0x80) != 0) {
      ppu->latched = false;
    }
    ppu->h_toggle = false;
    ppu->v_toggle = false;
    ppu->ppu2_open = value;
    return value;
  default:
    return open_bus;
  }
}

static void smx_ppu_on_write(void *user, Uint16 addr, Uint8 value) {
  SmxPpu *ppu = (SmxPpu *)user;

  if (addr == 0x2115) {
    ppu->vram_inc = (Uint8)(value & 0x03);
    ppu->vram_map = (Uint8)((value >> 2) & 0x03);
    ppu->vram_inc_high = (value & 0x80) != 0;
  } else if (addr == 0x2116) {
    ppu->vram_addr = (Uint16)((ppu->vram_addr & 0xFF00) | value);
    smx_ppu_prefetch(ppu);
  } else if (addr == 0x2117) {
    ppu->vram_addr = (Uint16)((ppu->vram_addr & 0x00FF) | ((Uint16)value << 8));
    smx_ppu_prefetch(ppu);
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
  } else if (addr >= 0x210D && addr <= 0x2114) {
    Uint8 index = (Uint8)((addr - 0x210D) >> 1);
    if ((addr & 1u) == 1) {
      ppu->bg_h[index] =
          (Uint16)(((Uint16)value << 8) | (ppu->bg_ofs & 0xF8u) | (ppu->bg_hofs & 0x07u));
      ppu->bg_ofs = value;
      ppu->bg_hofs = value;
    } else {
      ppu->bg_v[index] = (Uint16)(((Uint16)value << 8) | ppu->bg_ofs);
      ppu->bg_ofs = value;
    }
  }
  if (addr == 0x210D || addr == 0x210E || (addr >= 0x211B && addr <= 0x2120)) {
    /* Every write commits. The previous byte is the low half. */
    Uint16 word = (Uint16)(((Uint16)value << 8) | ppu->m7_low);
    ppu->m7_low = value;
    ppu->m7_have_low = true;
    if (addr == 0x210D || addr == 0x210E) {
      word = (Uint16)(word & 0x1FFFu);
    }
    if (addr == 0x211B) {
      ppu->m7a = word;
    } else if (addr == 0x211C) {
      ppu->m7b = word;
    } else if (addr == 0x211D) {
      ppu->m7c = word;
    } else if (addr == 0x211E) {
      ppu->m7d = word;
    } else if (addr == 0x211F) {
      ppu->m7x = word;
    } else if (addr == 0x2120) {
      ppu->m7y = word;
    } else if (addr == 0x210D) {
      ppu->m7h = word;
    } else if (addr == 0x210E) {
      ppu->m7v = word;
    }
  } else if (addr == 0x2102) {
    ppu->oam_addr = (Uint16)((ppu->oam_addr & 0xFF00) | value);
    ppu->oam_base = (Uint16)((ppu->oam_base & 0x0100) | value);
  } else if (addr == 0x2103) {
    ppu->oam_addr = (Uint16)((ppu->oam_addr & 0x00FF) | ((Uint16)(value & 0x01) << 8));
    ppu->oam_base = (Uint16)((ppu->oam_base & 0x00FF) | ((Uint16)(value & 0x01) << 8));
    ppu->oam_rotate = (value & 0x80) != 0;
  } else if (addr == 0x2132) {
    if ((value & 0x20) != 0) {
      ppu->fixed_r = (Uint8)(value & 0x1F);
    }
    if ((value & 0x40) != 0) {
      ppu->fixed_g = (Uint8)(value & 0x1F);
    }
    if ((value & 0x80) != 0) {
      ppu->fixed_b = (Uint8)(value & 0x1F);
    }
  } else if (addr == 0x2104) {
    if (ppu->oam_addr < sizeof(ppu->oam)) {
      ppu->oam[ppu->oam_addr] = value;
    }
    /* 544 bytes: 512 sprite words, then the 32-byte high table. */
    ppu->oam_addr = (Uint16)((ppu->oam_addr + 1) & 0x03FF);
  }
}

/* Main-screen window: true when this layer is hidden at x. */
/*
 * Fill `inside` for pixels `x_from` to `x_to` from one layer's window settings: `sel` is its
 * nibble of $2123-$2125 (invert 1, enable 1, invert 2, enable 2) and `logic` its two bits of
 * $212A/$212B, which combine the windows when both are enabled.
 */
static void smx_ppu_window_span(const SmxPpu *ppu, Uint8 sel, Uint8 logic, Uint32 x_from,
                                Uint32 x_to, bool *inside) {
  Uint8 left1 = smx_bus_mmio(ppu->bus, 0x2126);
  Uint8 right1 = smx_bus_mmio(ppu->bus, 0x2127);
  Uint8 left2 = smx_bus_mmio(ppu->bus, 0x2128);
  Uint8 right2 = smx_bus_mmio(ppu->bus, 0x2129);
  bool ena1 = (sel & 0x02) != 0;
  bool ena2 = (sel & 0x08) != 0;
  bool inv1 = (sel & 0x01) != 0;
  bool inv2 = (sel & 0x04) != 0;
  Uint32 x = x_from;

  if (!ena1 && !ena2) {
    SDL_memset(&inside[x_from], 0, x_to - x_from);
    return;
  }
  logic = (Uint8)(logic & 0x03);
  for (; x < x_to; x += 1) {
    bool in1 = (x >= left1 && x <= right1) != inv1;
    bool in2 = (x >= left2 && x <= right2) != inv2;
    bool in = false;
    if (ena1 && ena2) {
      if (logic == 0) {
        in = in1 || in2;
      } else if (logic == 1) {
        in = in1 && in2;
      } else if (logic == 2) {
        in = in1 != in2;
      } else {
        in = in1 == in2;
      }
    } else if (ena1) {
      in = in1;
    } else if (ena2) {
      in = in2;
    }
    inside[x] = in;
  }
}

static bool smx_ppu_region(Uint8 code, bool inside) {
  if (code == 0) {
    return false;
  }
  if (code == 3) {
    return true;
  }
  if (code == 2) {
    return inside;
  }
  return !inside;
}

static Uint16 smx_ppu_math(const SmxPpu *ppu, Uint16 color, Uint8 source, Uint16 sub,
                           bool use_sub) {
  Uint8 addsub = smx_bus_mmio(ppu->bus, 0x2131);
  /* Source 6 is a sprite in palettes 0-3, which color math never touches. */
  Uint8 mask = source == 0   ? 0x20
               : source < 5  ? (Uint8)(1u << (source - 1u))
               : source == 5 ? 0x10
                             : 0;
  Sint16 channels[3];
  Sint16 fixed[3];
  Uint8 i = 0;

  if ((addsub & mask) == 0) {
    return color;
  }
  channels[0] = (Sint16)(color & 0x1F);
  channels[1] = (Sint16)((color >> 5) & 0x1F);
  channels[2] = (Sint16)((color >> 10) & 0x1F);
  if (use_sub && (smx_bus_mmio(ppu->bus, 0x2130) & 0x02) != 0) {
    fixed[0] = (Sint16)(sub & 0x1F);
    fixed[1] = (Sint16)((sub >> 5) & 0x1F);
    fixed[2] = (Sint16)((sub >> 10) & 0x1F);
  } else {
    fixed[0] = ppu->fixed_r;
    fixed[1] = ppu->fixed_g;
    fixed[2] = ppu->fixed_b;
  }
  for (; i < 3; i += 1) {
    if ((addsub & 0x80) != 0) {
      channels[i] = (Sint16)(channels[i] - fixed[i]);
    } else {
      channels[i] = (Sint16)(channels[i] + fixed[i]);
    }
    if ((addsub & 0x40) != 0) {
      channels[i] = (Sint16)(channels[i] / 2);
    }
    if (channels[i] < 0) {
      channels[i] = 0;
    }
    if (channels[i] > 31) {
      channels[i] = 31;
    }
  }
  return (Uint16)(channels[0] | (channels[1] << 5) | (channels[2] << 10));
}

/* Brightness scales the 5-bit channel, then the top bits repeat into the low bits. */
static Uint8 smx_ppu_expand(Uint8 channel, Uint8 brightness) {
  Uint8 scaled = (Uint8)((channel * brightness) / 15u);
  return (Uint8)((scaled << 3) | (scaled >> 2));
}

static void smx_ppu_rgb(Uint16 bgr, Uint8 brightness, Uint8 *blue, Uint8 *green, Uint8 *red) {
  *blue = smx_ppu_expand((Uint8)((bgr & 0x7C00) >> 10), brightness);
  *green = smx_ppu_expand((Uint8)((bgr & 0x03E0) >> 5), brightness);
  *red = smx_ppu_expand((Uint8)(bgr & 0x001F), brightness);
}

static Uint16 smx_ppu_word(const SmxPpu *ppu, Uint16 addr) {
  Uint32 byte = (Uint32)(addr & 0x7FFF) * 2u;
  return (Uint16)(ppu->vram[byte] | ((Uint16)ppu->vram[byte + 1] << 8));
}

/*
 * Decode one 8-pixel row of a character into palette indices, left pixel first. VRAM wraps at
 * 64 KiB.
 */
static void smx_ppu_decode_row(const SmxPpu *ppu, Uint16 base_word, Uint16 tile, Uint8 row,
                               Uint8 depth, Uint8 *indices) {
  Uint32 bytes = depth == 8 ? 64u : (depth == 4 ? 32u : 16u);
  Uint32 base = ((Uint32)(base_word & 0x7FFF) * 2u) + (Uint32)tile * bytes + (Uint32)row * 2u;
  Uint8 planes[8];
  Uint8 plane = 0;
  Uint8 col = 0;

  for (; plane < depth; plane = (Uint8)(plane + 2)) {
    Uint32 at = base + (Uint32)(plane / 2u) * 16u;
    planes[plane] = ppu->vram[at & 0xFFFFu];
    planes[plane + 1] = ppu->vram[(at + 1u) & 0xFFFFu];
  }
  for (; col < 8; col += 1) {
    Uint8 bit = (Uint8)(7u - col);
    Uint8 index = 0;
    for (plane = 0; plane < depth; plane += 1) {
      index = (Uint8)(index | (((planes[plane] >> bit) & 1u) << plane));
    }
    indices[col] = index;
  }
}

/* A background layer over one span: color, and priority 0 or 1, or 0xFF where it is clear. */
typedef struct SmxPpuLine {
  Uint16 color[SMX_PPU_WIDTH];
  Uint8 prio[SMX_PPU_WIDTH];
} SmxPpuLine;

/*
 * Evaluate the sprites on scanline `y` into `obj_color`/`obj_prio`, as the
 * PPU does. The first 32 sprites on the line in OAM order are kept (a sprite
 * at X = -256 counts but never shows). Their tiles on screen are then fetched
 * last sprite first, left to right, and only 34 fit in the line. A later
 * fetch overwrites, so the first opaque sprite in OAM order wins, whatever
 * its priority; the priority only places it among the backgrounds. 0xFF
 * marks no sprite.
 */
#define SMX_PPU_LINE_SPRITES 32u
#define SMX_PPU_LINE_TILES 34u

static void smx_ppu_sprite_line(SmxPpu *ppu, Uint32 y) {
  Uint8 obsel = smx_bus_mmio(ppu->bus, 0x2101);
  Uint8 size = (Uint8)((obsel >> 5) & 0x07);
  Uint16 name_base = (Uint16)((obsel & 0x07) << 13);
  Uint16 name_gap = (Uint16)((1u + ((obsel >> 3) & 0x03)) << 12);
  static const Uint8 small_w[8] = {8, 8, 8, 16, 16, 32, 16, 16};
  static const Uint8 small_h[8] = {8, 8, 8, 16, 16, 32, 32, 32};
  static const Uint8 large_w[8] = {16, 32, 64, 32, 64, 64, 32, 32};
  static const Uint8 large_h[8] = {16, 32, 64, 32, 64, 64, 64, 32};
  Uint8 kept[SMX_PPU_LINE_SPRITES];
  Uint32 count = 0;
  Uint32 tiles = 0;
  Uint16 n = 0;
  Uint16 first = ppu->oam_rotate ? (Uint16)((ppu->oam_base >> 1) & 0x7F) : 0;

  SDL_memset(ppu->obj_prio, 0xFF, sizeof(ppu->obj_prio));
  ppu->time_over_line = false;
  /* The overflow flags start clear each frame. */
  if (y == 1u) {
    ppu->time_over = false;
    ppu->range_over = false;
  }
  for (; n < 128; n += 1) {
    Uint8 sprite = (Uint8)((first + n) & 0x7F);
    Uint8 pair = (Uint8)((ppu->oam[512u + (sprite >> 2)] >> ((sprite & 3u) * 2u)) & 0x03);
    Sint16 sx = (Sint16)(ppu->oam[sprite * 4u] - ((pair & 0x01) != 0 ? 256 : 0));
    Uint8 width = (pair & 0x02) != 0 ? large_w[size] : small_w[size];
    Uint8 height = (pair & 0x02) != 0 ? large_h[size] : small_h[size];
    Uint16 top = (Uint16)((ppu->oam[sprite * 4u + 1u] + 1u) & 0xFF);
    if (sx != -256 && (sx + width <= 0 || sx > 255)) {
      continue;
    }
    if ((Uint16)((y - top) & 0xFF) < height) {
      if (count == SMX_PPU_LINE_SPRITES) {
        ppu->range_over = true;
        break;
      }
      kept[count] = sprite;
      count += 1;
    }
  }
  while (count != 0 && !ppu->time_over_line) {
    Uint8 sprite = kept[count - 1u];
    Uint8 attr = ppu->oam[sprite * 4u + 3u];
    Uint8 pair = (Uint8)((ppu->oam[512u + (sprite >> 2)] >> ((sprite & 3u) * 2u)) & 0x03);
    Sint16 sx = (Sint16)(ppu->oam[sprite * 4u] - ((pair & 0x01) != 0 ? 256 : 0));
    Uint8 width = (pair & 0x02) != 0 ? large_w[size] : small_w[size];
    Uint16 top = (Uint16)((ppu->oam[sprite * 4u + 1u] + 1u) & 0xFF);
    Uint16 dy = (Uint16)((y - top) & 0xFF);
    Uint8 columns = (Uint8)(width / 8u);
    Uint8 column = 0;
    Uint16 pos = dy;

    count -= 1;
    /* Vertical flip mirrors rectangular sprites a square at a time. */
    if ((attr & 0x80) != 0) {
      pos = dy < width ? (Uint16)(width - 1u - dy) : (Uint16)(width * 3u - 1u - dy);
    }
    /* Tiles wholly left of the screen are skipped; X = -256 is fetched as if at 0. */
    if (sx <= -8 && sx != -256) {
      column = (Uint8)(-sx / 8);
    }
    for (; column < columns; column += 1) {
      Sint16 tile_x = (Sint16)((sx == -256 ? 0 : sx) + column * 8);
      Sint16 draw_x = (Sint16)(sx + column * 8);
      Uint8 tile_col = (attr & 0x40) != 0 ? (Uint8)(columns - 1u - column) : column;
      Uint16 tile = (Uint16)(ppu->oam[sprite * 4u + 2u] | ((Uint16)(attr & 0x01) << 8));
      Uint16 base = name_base;
      Uint8 indices[8];
      Uint8 px = 0;

      if (tiles == SMX_PPU_LINE_TILES) {
        ppu->time_over = true;
        ppu->time_over_line = true;
        break;
      }
      tiles += 1;
      /* Larger sprites step through a 16x16 character grid that wraps. */
      tile =
          (Uint16)((tile & 0x0100) | ((Uint16)((((tile >> 4) & 0x0F) + (pos >> 3)) & 0x0F) << 4) |
                   (((tile & 0x0F) + tile_col) & 0x0F));
      if ((tile & 0x0100) != 0) {
        base = (Uint16)(base + name_gap);
        tile = (Uint16)(tile & 0x00FF);
      }
      smx_ppu_decode_row(ppu, base, tile, (Uint8)(pos & 7u), 4, indices);
      for (px = 0; px < 8u; px += 1) {
        Sint16 x = (Sint16)(draw_x + px);
        Uint8 index = indices[(attr & 0x40) != 0 ? 7u - px : px];
        if (x < 0 || x >= (Sint16)SMX_PPU_WIDTH) {
          continue;
        }
        if (index != 0) {
          ppu->obj_prio[x] = (Uint8)((attr >> 4) & 0x03);
          ppu->obj_math[x] = (attr & 0x08) != 0;
          ppu->obj_color[x] = ppu->cgram[128u + ((Uint32)((attr >> 1) & 0x07) * 16u) + index];
        }
      }
      /* The rest of the sprite is right of the screen. */
      if (tile_x + 8 >= (Sint16)SMX_PPU_WIDTH) {
        break;
      }
    }
  }
}

static Sint32 smx_ppu_sex13(Uint16 value) {
  value = (Uint16)(value & 0x1FFF);
  if ((value & 0x1000) != 0) {
    return (Sint32)value - 0x2000;
  }
  return (Sint32)value;
}

static Sint32 smx_ppu_m7_clip(Sint32 value) {
  if ((value & 0x2000) != 0) {
    return value | ~(Sint32)0x3FF;
  }
  return value & 0x3FF;
}

static bool smx_ppu_sample_mode7(const SmxPpu *ppu, Uint32 x, Uint32 y, Uint16 *color) {
  Uint8 select = smx_bus_mmio(ppu->bus, 0x211A);
  Sint32 a = (Sint16)ppu->m7a;
  Sint32 b = (Sint16)ppu->m7b;
  Sint32 c = (Sint16)ppu->m7c;
  Sint32 d = (Sint16)ppu->m7d;
  Sint32 cx = smx_ppu_sex13(ppu->m7x);
  Sint32 cy = smx_ppu_sex13(ppu->m7y);
  Sint32 h = smx_ppu_sex13(ppu->m7h);
  Sint32 v = smx_ppu_sex13(ppu->m7v);
  Sint32 real_y = (select & 0x02) != 0 ? (Sint32)(255u - y) : (Sint32)y;
  Sint32 x_value = ((a * smx_ppu_m7_clip(h - cx)) & ~(Sint32)63) + ((b * real_y) & ~(Sint32)63) +
                   ((b * smx_ppu_m7_clip(v - cy)) & ~(Sint32)63) + cx * 256;
  Sint32 y_value = ((c * smx_ppu_m7_clip(h - cx)) & ~(Sint32)63) + ((d * real_y) & ~(Sint32)63) +
                   ((d * smx_ppu_m7_clip(v - cy)) & ~(Sint32)63) + cy * 256;
  Sint32 x_step = a;
  Sint32 y_step = c;
  Sint32 px = 0;
  Sint32 py = 0;

  if ((select & 0x01) != 0) {
    x_value += x_step * 255;
    y_value += y_step * 255;
    x_step = -x_step;
    y_step = -y_step;
  }
  x_value += x_step * (Sint32)x;
  y_value += y_step * (Sint32)x;
  px = x_value >> 8;
  py = y_value >> 8;
  bool outside = px < 0 || py < 0 || px > 1023 || py > 1023;
  Uint16 tile = 0;
  Uint16 word = 0;
  Uint8 index = 0;

  if (outside && (select & 0xC0) == 0x80) {
    return false;
  }
  if (outside && (select & 0xC0) == 0xC0) {
    tile = 0;
  } else {
    px &= 1023;
    py &= 1023;
    word = (Uint16)(((Uint32)(py >> 3) << 7) | (Uint32)(px >> 3));
    tile = (Uint16)(ppu->vram[(Uint32)word * 2u]);
  }
  word = (Uint16)(((Uint32)tile << 6) | ((Uint32)(py & 7) << 3) | (Uint32)(px & 7));
  index = ppu->vram[(Uint32)word * 2u + 1u];
  if (index == 0) {
    return false;
  }
  *color = ppu->cgram[index];
  return true;
}

/** Replace BG1 or BG2 scroll from the BG3 map in modes 2 and 4. The first column keeps the layer
 * scroll. */
static void smx_ppu_opt(const SmxPpu *ppu, Uint8 mode, Uint8 bg, Uint32 x, Uint16 *hscroll,
                        Uint16 *vscroll) {
  Uint16 h = *hscroll;
  Uint16 bg3h = 0;
  Uint16 bg3v = 0;
  Uint32 col = 0;
  Uint32 ox = 0;
  Uint32 oy = 0;
  Uint16 map = 0;
  Uint16 hentry = 0;
  Uint16 mask = 0;

  if ((mode != 2 && mode != 4 && mode != 6) || bg > 1) {
    return;
  }
  if (x < (Uint32)(8u - (h & 7u))) {
    return;
  }
  col = (x - (Uint32)(8u - (h & 7u))) >> 3;
  bg3h = ppu->bg_h[2];
  bg3v = ppu->bg_v[2];
  map = (Uint16)((smx_bus_mmio(ppu->bus, 0x2109) & 0xFC) << 8);
  ox = ((bg3h & 0x3F8u) >> 3) + col;
  oy = (bg3v & 0x3F8u) >> 3;
  hentry = smx_ppu_word(ppu, (Uint16)(map + (oy & 31u) * 32u + (ox & 31u)));
  mask = bg == 0 ? 0x2000u : 0x4000u;
  if (mode == 4) {
    if ((hentry & mask) == 0) {
      return;
    }
    if ((hentry & 0x8000) != 0) {
      *vscroll = (Uint16)(hentry & 0x03FF);
    } else {
      *hscroll = (Uint16)((((x & ~7u) + (hentry & 0x03F8u) + ((x + h) & 7u)) - x) & 0x03FF);
    }
    return;
  }
  if ((hentry & mask) != 0) {
    *hscroll = (Uint16)((((x & ~7u) + (hentry & 0x03F8u) + ((x + h) & 7u)) - x) & 0x03FF);
  }
  {
    Uint16 ventry = smx_ppu_word(ppu, (Uint16)(map + ((oy + 1u) & 31u) * 32u + (ox & 31u)));
    if ((ventry & mask) != 0) {
      *vscroll = (Uint16)(ventry & 0x03FF);
    }
  }
}

/* Mosaic snaps a coordinate to the top-left of its block for the layers $2106 enables. */
static Uint32 smx_ppu_mosaic(Uint8 reg, Uint8 bg, Uint32 value) {
  Uint8 size = (Uint8)((reg & 0x0F) + 1u);
  if (size == 1 || (reg & (Uint8)(0x10u << bg)) == 0) {
    return value;
  }
  return value - (value % size);
}

/* Bits per pixel of a background in a mode, or 0 when the mode has no such layer. */
static Uint8 smx_ppu_depth(Uint8 mode, Uint8 bg) {
  static const Uint8 depths[8][4] = {{2, 2, 2, 2}, {4, 4, 2, 0}, {4, 4, 0, 0}, {8, 4, 0, 0},
                                     {8, 2, 0, 0}, {4, 2, 0, 0}, {4, 0, 0, 0}, {0, 0, 0, 0}};
  return depths[mode][bg];
}

/*
 * Draw background `bg` of scanline `y` over pixels `x_from` to `x_to`. In modes 5 and 6 a pixel
 * is two hi-res pixels: `odd` picks the right one, which the subscreen shows.
 */
static void smx_ppu_bg_line(const SmxPpu *ppu, Uint8 mode, Uint8 bg, bool odd, Uint32 y,
                            Uint32 x_from, Uint32 x_to, SmxPpuLine *line) {
  static const Uint16 map_regs[4] = {0x2107, 0x2108, 0x2109, 0x210A};
  Uint8 depth = smx_ppu_depth(mode, bg);
  Uint8 map_reg = smx_bus_mmio(ppu->bus, map_regs[bg]);
  Uint8 char_reg = smx_bus_mmio(ppu->bus, bg < 2 ? 0x210B : 0x210C);
  Uint16 chars = (Uint16)(((bg & 1) == 0 ? (char_reg & 0x0F) : (char_reg >> 4)) << 12);
  Uint8 mosaic = smx_bus_mmio(ppu->bus, 0x2106);
  bool large = (smx_bus_mmio(ppu->bus, 0x2105) & (Uint8)(0x10u << bg)) != 0;
  bool hires = mode == 5 || mode == 6;
  bool offset_per_tile = (mode == 2 || mode == 4 || mode == 6) && bg < 2;
  bool direct =
      depth == 8 && (smx_bus_mmio(ppu->bus, 0x2130) & 0x01) != 0 && (mode == 3 || mode == 4);
  bool wide = (map_reg & 0x01) != 0;
  bool tall = (map_reg & 0x02) != 0;
  Uint32 shift_x = (large || hires) ? 4u : 3u;
  Uint32 shift_y = large ? 4u : 3u;
  Uint8 span_x = (large || hires) ? 15 : 7;
  Uint8 span_y = large ? 15 : 7;
  Uint32 my = smx_ppu_mosaic(mosaic, bg, y);
  Uint32 cached = 0xFFFFFFFFu;
  Uint32 cell = 0xFFFFFFFFu;
  Uint16 entry = 0;
  Uint8 indices[8];
  Uint32 x = x_from;

  for (; x < x_to; x += 1) {
    Uint32 mx = smx_ppu_mosaic(mosaic, bg, x);
    Uint16 hscroll = ppu->bg_h[bg];
    Uint16 vscroll = ppu->bg_v[bg];
    Uint32 px = 0;
    Uint32 py = 0;
    Uint32 ex = 0;
    Uint32 ey = 0;
    Uint16 tile = 0;
    Uint8 palette = 0;
    Uint8 local_x = 0;
    Uint8 local_y = 0;
    Uint8 index = 0;
    Uint32 key = 0;

    if (offset_per_tile) {
      smx_ppu_opt(ppu, mode, bg, mx, &hscroll, &vscroll);
    }
    px = (mx + (hscroll & 0x03FFu)) & 0x03FFu;
    py = (my + (vscroll & 0x03FFu)) & 0x03FFu;
    if (hires) {
      px = px * 2u + (odd ? 1u : 0u);
    }
    ex = px >> shift_x;
    ey = py >> shift_y;
    if (((ey << 8) | ex) != cell) {
      Uint16 map = (Uint16)((map_reg & 0xFC) << 8);
      if (wide && (ex & 32u) != 0) {
        map = (Uint16)(map + 0x400u);
      }
      if (tall && (ey & 32u) != 0) {
        map = (Uint16)(map + (wide ? 0x800u : 0x400u));
      }
      entry = smx_ppu_word(ppu, (Uint16)(map + (ey & 31u) * 32u + (ex & 31u)));
      cell = (ey << 8) | ex;
    }
    tile = (Uint16)(entry & 0x03FF);
    palette = (Uint8)((entry >> 10) & 0x07);
    local_x = (Uint8)(px & span_x);
    local_y = (Uint8)(py & span_y);
    if ((entry & 0x8000) != 0) {
      local_y = (Uint8)(span_y - local_y);
    }
    if ((entry & 0x4000) != 0) {
      local_x = (Uint8)(span_x - local_x);
    }
    if (large || hires) {
      tile = (Uint16)(tile + (local_x >> 3) + (large ? (Uint16)(local_y >> 3) * 16u : 0u));
    }
    /* Neighbouring pixels usually share a character row; decode each row once. */
    key = ((Uint32)tile << 3) | (local_y & 7u);
    if (key != cached) {
      smx_ppu_decode_row(ppu, chars, tile, (Uint8)(local_y & 7u), depth, indices);
      cached = key;
    }
    index = indices[local_x & 7u];
    if (index == 0) {
      line->prio[x] = 0xFF;
      continue;
    }
    line->prio[x] = (entry & 0x2000) != 0 ? 1 : 0;
    if (direct) {
      Uint8 red = (Uint8)(index & 0x07);
      Uint8 green = (Uint8)((index >> 3) & 0x07);
      Uint8 blue = (Uint8)((index >> 6) & 0x03);
      Uint8 pr = (Uint8)(palette & 0x01);
      Uint8 pg = (Uint8)((palette >> 1) & 0x01);
      Uint8 pb = (Uint8)((palette >> 2) & 0x01);
      line->color[x] =
          (Uint16)(((Uint16)((red << 2) | (pr << 1))) | ((Uint16)((green << 2) | (pg << 1)) << 5) |
                   ((Uint16)((blue << 3) | (pb << 2)) << 10));
    } else if (depth == 8) {
      line->color[x] = ppu->cgram[index];
    } else if (depth == 4) {
      line->color[x] = ppu->cgram[(Uint32)palette * 16u + index];
    } else {
      line->color[x] = ppu->cgram[(Uint32)palette * 4u + index];
    }
  }
}

/* Mode 7 BG1 of scanline `y` over pixels `x_from` to `x_to`. */
static void smx_ppu_mode7_line(const SmxPpu *ppu, Uint32 y, Uint32 x_from, Uint32 x_to,
                               SmxPpuLine *line) {
  Uint8 mosaic = smx_bus_mmio(ppu->bus, 0x2106);
  Uint32 my = smx_ppu_mosaic(mosaic, 0, y);
  Uint32 x = x_from;

  for (; x < x_to; x += 1) {
    Uint16 color = 0;
    if (smx_ppu_sample_mode7(ppu, smx_ppu_mosaic(mosaic, 0, x), my, &color)) {
      line->color[x] = color;
      line->prio[x] = 0;
    } else {
      line->prio[x] = 0xFF;
    }
  }
}

/*
 * Layer order from back to front, per mode. Each entry is a layer (0-3 a background, 4 the
 * sprites) in the high nibble and the priority it shows at in the low one. Mode 7 BG1 has a
 * single priority, 0.
 */
#define SMX_PPU_OBJ 4u
#define SMX_PPU_SLOT(layer, prio) (Uint8)(((layer) << 4) | (prio))

static const Uint8 smx_ppu_order_mode0[] = {
    SMX_PPU_SLOT(3, 0), SMX_PPU_SLOT(2, 0), SMX_PPU_SLOT(4, 0), SMX_PPU_SLOT(3, 1),
    SMX_PPU_SLOT(2, 1), SMX_PPU_SLOT(4, 1), SMX_PPU_SLOT(1, 0), SMX_PPU_SLOT(0, 0),
    SMX_PPU_SLOT(4, 2), SMX_PPU_SLOT(1, 1), SMX_PPU_SLOT(0, 1), SMX_PPU_SLOT(4, 3)};
static const Uint8 smx_ppu_order_mode1[] = {
    SMX_PPU_SLOT(2, 0), SMX_PPU_SLOT(4, 0), SMX_PPU_SLOT(2, 1), SMX_PPU_SLOT(4, 1),
    SMX_PPU_SLOT(1, 0), SMX_PPU_SLOT(0, 0), SMX_PPU_SLOT(4, 2), SMX_PPU_SLOT(1, 1),
    SMX_PPU_SLOT(0, 1), SMX_PPU_SLOT(4, 3)};
/* Mode 1 with $2105 bit 3: priority BG3 moves in front of everything. */
static const Uint8 smx_ppu_order_mode1_bg3[] = {
    SMX_PPU_SLOT(2, 0), SMX_PPU_SLOT(4, 0), SMX_PPU_SLOT(4, 1), SMX_PPU_SLOT(1, 0),
    SMX_PPU_SLOT(0, 0), SMX_PPU_SLOT(4, 2), SMX_PPU_SLOT(1, 1), SMX_PPU_SLOT(0, 1),
    SMX_PPU_SLOT(4, 3), SMX_PPU_SLOT(2, 1)};
static const Uint8 smx_ppu_order_mode2[] = {
    SMX_PPU_SLOT(1, 0), SMX_PPU_SLOT(0, 0), SMX_PPU_SLOT(4, 0), SMX_PPU_SLOT(1, 1),
    SMX_PPU_SLOT(4, 1), SMX_PPU_SLOT(0, 1), SMX_PPU_SLOT(4, 2), SMX_PPU_SLOT(4, 3)};
static const Uint8 smx_ppu_order_mode7[] = {SMX_PPU_SLOT(4, 0), SMX_PPU_SLOT(0, 0),
                                            SMX_PPU_SLOT(4, 1), SMX_PPU_SLOT(4, 2),
                                            SMX_PPU_SLOT(4, 3)};

/* Everything one span of one scanline needs, decoded once. */
typedef struct SmxPpuSpan {
  const Uint8 *order;
  Uint8 order_size;
  /** Layers drawn on the main screen ($212C) and the subscreen ($212D). */
  Uint8 screen[2];
  /** Layers the windows clip on each screen ($212E and $212F). */
  Uint8 clip[2];
  /** The backgrounds for each screen. They differ only in hi-res modes. */
  const SmxPpuLine *bg[2][4];
  /** Where each layer's window and the color window apply. */
  bool inside[5][SMX_PPU_WIDTH];
  bool color_inside[SMX_PPU_WIDTH];
} SmxPpuSpan;

/* The front-most visible pixel of one screen at `x`, or the backdrop when there is none. */
static bool smx_ppu_compose(const SmxPpu *ppu, const SmxPpuSpan *span, Uint8 which, Uint32 x,
                            Uint16 *color, Uint8 *source) {
  Uint8 screen = span->screen[which];
  Uint8 clip = span->clip[which];
  Uint8 slot = span->order_size;

  while (slot != 0) {
    Uint8 entry = span->order[slot - 1u];
    Uint8 layer = (Uint8)(entry >> 4);
    Uint8 prio = (Uint8)(entry & 0x0F);
    slot -= 1;
    if ((screen & (Uint8)(1u << layer)) == 0 ||
        ((clip & (Uint8)(1u << layer)) != 0 && span->inside[layer][x])) {
      continue;
    }
    if (layer == SMX_PPU_OBJ) {
      if (ppu->obj_prio[x] == prio) {
        *color = ppu->obj_color[x];
        *source = ppu->obj_math[x] ? 5 : 6;
        return true;
      }
    } else if (span->bg[which][layer]->prio[x] == prio) {
      *color = span->bg[which][layer]->color[x];
      *source = (Uint8)(layer + 1u);
      return true;
    }
  }
  *color = ppu->cgram[0];
  *source = 0;
  return false;
}

static void smx_ppu_render_at(SmxPpu *ppu, Uint32 sample, Uint32 row, Uint32 x_from, Uint32 x_to) {
  Uint8 inidisp = smx_bus_mmio(ppu->bus, 0x2100);
  bool blank = (inidisp & 0x80) != 0;
  Uint8 brightness = (Uint8)(inidisp & 0x0F);
  Uint8 bgmode = smx_bus_mmio(ppu->bus, 0x2105);
  Uint8 mode = (Uint8)(bgmode & 0x07);
  Uint8 select = smx_bus_mmio(ppu->bus, 0x2130);
  bool use_sub = (select & 0x02) != 0;
  SmxPpuLine lines[2][4];
  SmxPpuSpan span;
  Uint8 layer = 0;
  Uint32 x = x_from;

  if (sample > SMX_PPU_HEIGHT || row >= SMX_PPU_HEIGHT || ppu->pixels == NULL) {
    return;
  }
  smx_ppu_sprite_line(ppu, sample);
  x_to = x_to < SMX_PPU_WIDTH ? x_to : SMX_PPU_WIDTH;
  if (x_from >= x_to) {
    return;
  }
  if (blank || brightness == 0) {
    for (; x < x_to; x += 1) {
      Uint8 *out = &ppu->pixels[(row * SMX_PPU_WIDTH + x) * 4u];
      out[0] = 0;
      out[1] = 0;
      out[2] = 0;
      out[3] = 255;
    }
    return;
  }

  span.screen[0] = smx_bus_mmio(ppu->bus, 0x212C);
  span.screen[1] = smx_bus_mmio(ppu->bus, 0x212D);
  span.clip[0] = smx_bus_mmio(ppu->bus, 0x212E);
  span.clip[1] = smx_bus_mmio(ppu->bus, 0x212F);
  if (mode == 0) {
    span.order = smx_ppu_order_mode0;
    span.order_size = (Uint8)sizeof(smx_ppu_order_mode0);
  } else if (mode == 1 && (bgmode & 0x08) != 0) {
    span.order = smx_ppu_order_mode1_bg3;
    span.order_size = (Uint8)sizeof(smx_ppu_order_mode1_bg3);
  } else if (mode == 1) {
    span.order = smx_ppu_order_mode1;
    span.order_size = (Uint8)sizeof(smx_ppu_order_mode1);
  } else if (mode == 7) {
    span.order = smx_ppu_order_mode7;
    span.order_size = (Uint8)sizeof(smx_ppu_order_mode7);
  } else {
    span.order = smx_ppu_order_mode2;
    span.order_size = (Uint8)sizeof(smx_ppu_order_mode2);
  }

  /* Draw each background a screen shows once, and again for the subscreen in hi-res. */
  for (layer = 0; layer < 4; layer += 1) {
    Uint8 bit = (Uint8)(1u << layer);
    bool shown = ((span.screen[0] | (use_sub ? span.screen[1] : 0)) & bit) != 0;
    bool hires = mode == 5 || mode == 6;
    span.bg[0][layer] = &lines[0][layer];
    span.bg[1][layer] = hires ? &lines[1][layer] : &lines[0][layer];
    if (mode == 7 && layer == 0 && shown) {
      smx_ppu_mode7_line(ppu, sample, x_from, x_to, &lines[0][0]);
    } else if (mode == 7 || !shown || smx_ppu_depth(mode, layer) == 0) {
      SDL_memset(&lines[0][layer].prio[x_from], 0xFF, x_to - x_from);
      SDL_memset(&lines[1][layer].prio[x_from], 0xFF, x_to - x_from);
    } else {
      smx_ppu_bg_line(ppu, mode, layer, false, sample, x_from, x_to, &lines[0][layer]);
      if (hires) {
        smx_ppu_bg_line(ppu, mode, layer, true, sample, x_from, x_to, &lines[1][layer]);
      }
    }
  }

  /* Only the layers a screen clips, and the color window when $2130 uses it, need a window. */
  {
    Uint8 sel[6];
    Uint8 logic[6];
    bool *inside[6];
    Uint8 clipped = (Uint8)(span.clip[0] | (use_sub ? span.clip[1] : 0));
    Uint8 window = 0;
    sel[0] = smx_bus_mmio(ppu->bus, 0x2123);
    sel[2] = smx_bus_mmio(ppu->bus, 0x2124);
    sel[4] = smx_bus_mmio(ppu->bus, 0x2125);
    sel[1] = (Uint8)(sel[0] >> 4);
    sel[3] = (Uint8)(sel[2] >> 4);
    sel[5] = (Uint8)(sel[4] >> 4);
    logic[0] = smx_bus_mmio(ppu->bus, 0x212A);
    logic[1] = (Uint8)(logic[0] >> 2);
    logic[2] = (Uint8)(logic[0] >> 4);
    logic[3] = (Uint8)(logic[0] >> 6);
    logic[4] = smx_bus_mmio(ppu->bus, 0x212B);
    logic[5] = (Uint8)(logic[4] >> 2);
    for (; window < 6; window += 1) {
      bool needed = window == 5 ? (select & 0xF0) != 0 : (clipped & (Uint8)(1u << window)) != 0;
      inside[window] = window == 5 ? span.color_inside : span.inside[window];
      if (needed) {
        smx_ppu_window_span(ppu, (Uint8)(sel[window] & 0x0F), logic[window], x_from, x_to,
                            inside[window]);
      }
    }
  }

  for (; x < x_to; x += 1) {
    Uint16 color = 0;
    Uint16 sub = 0;
    Uint8 source = 0;
    Uint8 sub_source = 0;
    bool sub_hit = false;
    bool in_window = (select & 0xF0) != 0 && span.color_inside[x];
    Uint8 *out = &ppu->pixels[(row * SMX_PPU_WIDTH + x) * 4u];

    smx_ppu_compose(ppu, &span, 0, x, &color, &source);
    if (use_sub) {
      sub_hit = smx_ppu_compose(ppu, &span, 1, x, &sub, &sub_source);
    }
    if (smx_ppu_region((Uint8)(select >> 6), in_window)) {
      color = 0;
    }
    if (!smx_ppu_region((Uint8)((select >> 4) & 0x03), in_window)) {
      color = smx_ppu_math(ppu, color, source, sub, sub_hit);
    }
    smx_ppu_rgb(color, brightness, &out[0], &out[1], &out[2]);
    out[3] = 255;
  }
}

void smx_ppu_render_line(SmxPpu *ppu, Uint32 y) {
  smx_ppu_render_at(ppu, y, y, 0, SMX_PPU_WIDTH);
}

void smx_ppu_present_span(SmxPpu *ppu, Uint32 scanline, Uint32 x_from, Uint32 x_to) {
  if (scanline < 1u) {
    return;
  }
  smx_ppu_render_at(ppu, scanline, scanline - 1u, x_from, x_to);
}

void smx_ppu_present_line(SmxPpu *ppu, Uint32 scanline) {
  smx_ppu_present_span(ppu, scanline, 0, SMX_PPU_WIDTH);
}

void smx_ppu_render(SmxPpu *ppu) {
  Uint32 y = 0;

  for (; y < SMX_PPU_HEIGHT; y += 1) {
    smx_ppu_render_line(ppu, y);
  }
}

/** Mesen 2.1.1 NTSC frame 41, the Nintendo logo. */
#define SMX_PPU_NTSC_LOGO_HASH 0xd155b93bu

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

void smx_ppu_note_picture(SmxPpu *ppu) {
  if (ppu == NULL || ppu->picture_matches_oracle || ppu->pixels == NULL) {
    return;
  }
  if (smx_ppu_frame_hash(ppu) == SMX_PPU_NTSC_LOGO_HASH) {
    ppu->picture_matches_oracle = true;
  }
}
