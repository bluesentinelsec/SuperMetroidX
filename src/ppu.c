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

static Uint8 smx_ppu_on_read(void *user, Uint16 addr, Uint8 open_bus) {
  SmxPpu *ppu = (SmxPpu *)user;
  Sint16 b_high = (Sint16)((Sint16)ppu->m7b >> 8);
  Sint32 product = (Sint32)(Sint16)ppu->m7a * (Sint32)b_high;

  if (addr == 0x2134) {
    return (Uint8)product;
  }
  if (addr == 0x2135) {
    return (Uint8)(product >> 8);
  }
  if (addr == 0x2136) {
    return (Uint8)(product >> 16);
  }
  return open_bus;
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
static bool smx_ppu_window_masks(const SmxPpu *ppu, Uint8 layer, Uint32 x, Uint16 enable_reg) {
  Uint8 enable = smx_bus_mmio(ppu->bus, enable_reg);
  Uint8 sel = 0;
  Uint8 logic = 0;
  Uint8 shift = 0;
  bool ena1 = false;
  bool ena2 = false;
  bool in1 = false;
  bool in2 = false;
  bool inside = false;
  Uint8 left1 = smx_bus_mmio(ppu->bus, 0x2126);
  Uint8 right1 = smx_bus_mmio(ppu->bus, 0x2127);
  Uint8 left2 = smx_bus_mmio(ppu->bus, 0x2128);
  Uint8 right2 = smx_bus_mmio(ppu->bus, 0x2129);

  if ((enable & (Uint8)(1u << layer)) == 0) {
    return false;
  }
  if (layer < 2) {
    sel = smx_bus_mmio(ppu->bus, 0x2123);
    logic = (Uint8)(smx_bus_mmio(ppu->bus, 0x212A) >> (layer * 2u));
    shift = (Uint8)(layer * 4u);
  } else if (layer < 4) {
    sel = smx_bus_mmio(ppu->bus, 0x2124);
    logic = (Uint8)(smx_bus_mmio(ppu->bus, 0x212A) >> (layer * 2u));
    shift = (Uint8)((layer - 2u) * 4u);
  } else {
    sel = smx_bus_mmio(ppu->bus, 0x2125);
    logic = smx_bus_mmio(ppu->bus, 0x212B);
    shift = 0;
  }
  ena1 = (sel & (Uint8)(0x02u << shift)) != 0;
  ena2 = (sel & (Uint8)(0x08u << shift)) != 0;
  in1 = ((x >= left1 && x <= right1) != ((sel & (Uint8)(0x01u << shift)) != 0));
  in2 = ((x >= left2 && x <= right2) != ((sel & (Uint8)(0x04u << shift)) != 0));
  if (ena1 && ena2) {
    logic = (Uint8)(logic & 0x03);
    if (logic == 0) {
      inside = in1 || in2;
    } else if (logic == 1) {
      inside = in1 && in2;
    } else if (logic == 2) {
      inside = in1 != in2;
    } else {
      inside = in1 == in2;
    }
  } else if (ena1) {
    inside = in1;
  } else if (ena2) {
    inside = in2;
  }
  return inside;
}

static bool smx_ppu_color_window(const SmxPpu *ppu, Uint32 x) {
  Uint8 sel = smx_bus_mmio(ppu->bus, 0x2125);
  Uint8 logic = (Uint8)((smx_bus_mmio(ppu->bus, 0x212B) >> 2) & 0x03);
  bool ena1 = (sel & 0x20) != 0;
  bool ena2 = (sel & 0x80) != 0;
  bool in1 = ((x >= smx_bus_mmio(ppu->bus, 0x2126) && x <= smx_bus_mmio(ppu->bus, 0x2127)) !=
              ((sel & 0x10) != 0));
  bool in2 = ((x >= smx_bus_mmio(ppu->bus, 0x2128) && x <= smx_bus_mmio(ppu->bus, 0x2129)) !=
              ((sel & 0x40) != 0));
  bool inside = false;

  if (ena1 && ena2) {
    if (logic == 0) {
      inside = in1 || in2;
    } else if (logic == 1) {
      inside = in1 && in2;
    } else if (logic == 2) {
      inside = in1 != in2;
    } else {
      inside = in1 == in2;
    }
  } else if (ena1) {
    inside = in1;
  } else if (ena2) {
    inside = in2;
  }
  return inside;
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
  Uint8 mask = source == 0 ? 0x20 : source < 5 ? (Uint8)(1u << (source - 1u)) : 0x10;
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

/** Round a 5-bit channel to 8-bit. Mesen expands the same way. */
static Uint8 smx_ppu_expand(Uint8 channel) {
  return (Uint8)((channel * 255u + 15u) / 31u);
}

static void smx_ppu_rgb(Uint16 bgr, Uint8 brightness, Uint8 *blue, Uint8 *green, Uint8 *red) {
  Uint8 b = smx_ppu_expand((Uint8)((bgr & 0x7C00) >> 10));
  Uint8 g = smx_ppu_expand((Uint8)((bgr & 0x03E0) >> 5));
  Uint8 r = smx_ppu_expand((Uint8)(bgr & 0x001F));
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
                           Uint8 depth) {
  Uint32 bytes = depth == 8 ? 64u : (depth == 4 ? 32u : 16u);
  Uint32 base = ((Uint32)(base_word & 0x7FFF) * 2u) + (Uint32)tile * bytes;
  Uint8 bit = (Uint8)(7u - (col & 7u));
  Uint8 p0 = ppu->vram[base + (Uint32)row * 2u];
  Uint8 p1 = ppu->vram[base + (Uint32)row * 2u + 1u];
  Uint8 index = (Uint8)(((p0 >> bit) & 1u) | (((p1 >> bit) & 1u) << 1));
  Uint8 plane = 2;

  for (; depth >= 4 && plane < depth; plane = (Uint8)(plane + 2)) {
    Uint32 at = base + (Uint32)(plane / 2u) * 16u + (Uint32)row * 2u;
    Uint8 even = ppu->vram[at];
    Uint8 odd = ppu->vram[at + 1u];
    index = (Uint8)(index | (((even >> bit) & 1u) << plane) | (((odd >> bit) & 1u) << (plane + 1)));
  }
  return index;
}

static Uint32 smx_ppu_mosaic(const SmxPpu *ppu, Uint8 bg, Uint32 value) {
  Uint8 reg = smx_bus_mmio(ppu->bus, 0x2106);
  Uint8 size = (Uint8)((reg & 0x0F) + 1u);
  if (size == 1 || (reg & (Uint8)(0x10u << bg)) == 0) {
    return value;
  }
  return value - (value % size);
}

static void smx_ppu_sample_bg(const SmxPpu *ppu, Uint8 map_reg, Uint8 char_nibble, Uint8 depth,
                              bool large, bool hires, bool odd, Uint32 x, Uint32 y, Uint16 hscroll,
                              Uint16 vscroll, bool *opaque, bool *priority, Uint16 *color) {
  Uint16 map = (Uint16)((map_reg & 0xFC) << 8);
  Uint16 chars = (Uint16)((char_nibble & 0x0F) << 12);
  Uint32 px = (x + (hscroll & 0x03FFu)) & 0x03FFu;
  Uint32 py = (y + (vscroll & 0x03FFu)) & 0x03FFu;
  Uint32 shift_x = large ? 4u : 3u;
  Uint32 shift_y = large ? 4u : 3u;
  Uint32 ex = 0;

  if (hires) {
    px = px * 2u + (odd ? 1u : 0u);
    shift_x = 4u;
  }
  ex = px >> shift_x;
  Uint32 ey = py >> shift_y;
  Uint32 tx = ex & 31u;
  Uint32 ty = ey & 31u;
  Uint16 entry = 0;
  bool wide = (map_reg & 0x01) != 0;
  bool tall = (map_reg & 0x02) != 0;

  if (wide && (ex & 32u) != 0) {
    map = (Uint16)(map + 0x400u);
  }
  if (tall && (ey & 32u) != 0) {
    map = (Uint16)(map + (wide ? 0x800u : 0x400u));
  }
  entry = smx_ppu_word(ppu, (Uint16)(map + ty * 32u + tx));
  Uint16 tile = (Uint16)(entry & 0x03FF);
  Uint8 palette = (Uint8)((entry >> 10) & 0x07);
  Uint8 span_x = (large || hires) ? 15 : 7;
  Uint8 span_y = large ? 15 : 7;
  Uint8 local_x = (Uint8)(px & span_x);
  Uint8 local_y = (Uint8)(py & span_y);
  Uint8 row = 0;
  Uint8 col = 0;
  Uint8 index = 0;

  if ((entry & 0x8000) != 0) {
    local_y = (Uint8)(span_y - local_y);
  }
  if ((entry & 0x4000) != 0) {
    local_x = (Uint8)(span_x - local_x);
  }
  row = (Uint8)(local_y & 7u);
  col = (Uint8)(local_x & 7u);
  if (large || hires) {
    tile = (Uint16)(tile + (local_x >> 3) + (large ? (Uint16)(local_y >> 3) * 16u : 0u));
  }
  index = smx_ppu_plane(ppu, chars, tile, row, col, depth);
  *priority = (entry & 0x2000) != 0;
  *opaque = index != 0;
  if (depth == 8 && (smx_bus_mmio(ppu->bus, 0x2130) & 0x01) != 0 &&
      ((smx_bus_mmio(ppu->bus, 0x2105) & 0x07) == 3 ||
       (smx_bus_mmio(ppu->bus, 0x2105) & 0x07) == 4)) {
    Uint8 red = (Uint8)(index & 0x07);
    Uint8 green = (Uint8)((index >> 3) & 0x07);
    Uint8 blue = (Uint8)((index >> 6) & 0x03);
    Uint8 pr = (Uint8)(palette & 0x01);
    Uint8 pg = (Uint8)((palette >> 1) & 0x01);
    Uint8 pb = (Uint8)((palette >> 2) & 0x01);
    *color =
        (Uint16)(((Uint16)((red << 2) | (pr << 1))) | ((Uint16)((green << 2) | (pg << 1)) << 5) |
                 ((Uint16)((blue << 3) | (pb << 2)) << 10));
  } else if (depth == 8) {
    *color = ppu->cgram[index];
  } else if (depth == 4) {
    *color = ppu->cgram[(Uint32)palette * 16u + index];
  } else {
    *color = ppu->cgram[(Uint32)palette * 4u + index];
  }
}

static bool smx_ppu_sample_sprite(const SmxPpu *ppu, Uint32 x, Uint32 y, Uint8 priority,
                                  Uint16 *color) {
  Uint8 obsel = smx_bus_mmio(ppu->bus, 0x2101);
  Uint8 size = (Uint8)((obsel >> 5) & 0x07);
  Uint16 name_base = (Uint16)((obsel & 0x07) << 13);
  Uint16 name_gap = (Uint16)((1u + ((obsel >> 3) & 0x03)) << 12);
  static const Uint8 small_w[8] = {8, 8, 8, 16, 16, 32, 16, 16};
  static const Uint8 small_h[8] = {8, 8, 8, 16, 16, 32, 32, 32};
  static const Uint8 large_w[8] = {16, 32, 64, 32, 64, 64, 32, 32};
  static const Uint8 large_h[8] = {16, 32, 64, 32, 64, 64, 64, 32};
  Uint16 n = 0;
  Uint16 first = ppu->oam_rotate ? (Uint16)((ppu->oam_base >> 1) & 0x7F) : 0;

  for (; n < 128; n += 1) {
    Uint16 sprite = (Uint16)((first + n) & 0x7F);
    Uint8 attr = ppu->oam[sprite * 4u + 3u];
    Uint8 pair = 0;
    Sint16 sx = 0;
    Uint8 width = 0;
    Uint8 height = 0;
    Uint16 top = 0;
    Uint16 dy = 0;
    Sint16 dx = 0;
    Uint16 tile = 0;
    Uint16 base = 0;
    Uint8 index = 0;
    if (((attr >> 4) & 0x03) != (Uint8)priority) {
      continue;
    }
    pair = (Uint8)((ppu->oam[512u + (sprite >> 2)] >> ((sprite & 3u) * 2u)) & 0x03);
    sx = ppu->oam[sprite * 4u];
    if ((pair & 0x01) != 0) {
      sx = (Sint16)(sx - 256);
    }
    width = (pair & 0x02) != 0 ? large_w[size] : small_w[size];
    height = (pair & 0x02) != 0 ? large_h[size] : small_h[size];
    top = (Uint16)((ppu->oam[sprite * 4u + 1u] + 1u) & 0xFF);
    dy = (Uint16)((y - top) & 0xFF);
    if (dy >= height) {
      continue;
    }
    dx = (Sint16)((Sint16)x - sx);
    if (dx < 0 || dx >= width) {
      continue;
    }
    if ((attr & 0x40) != 0) {
      dx = (Sint16)(width - 1 - dx);
    }
    if ((attr & 0x80) != 0) {
      dy = (Uint16)(height - 1u - dy);
    }
    tile = (Uint16)((ppu->oam[sprite * 4u + 2u] | ((Uint16)(attr & 0x01) << 8)) + (Uint16)(dx / 8) +
                    (Uint16)(dy / 8) * 16u);
    tile = (Uint16)(tile & 0x01FF);
    base = name_base;
    if ((tile & 0x0100) != 0) {
      base = (Uint16)(base + name_gap);
      tile = (Uint16)(tile & 0x00FF);
    }
    index = smx_ppu_plane(ppu, base, tile, (Uint8)(dy & 7u), (Uint8)(dx & 7u), 4);
    if (index == 0) {
      continue;
    }
    *color = ppu->cgram[128u + ((Uint32)((attr >> 1) & 0x07) * 16u) + index];
    return true;
  }
  return false;
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
                   ((b * smx_ppu_m7_clip(v - cy)) & ~(Sint32)63) + (cx << 8);
  Sint32 y_value = ((c * smx_ppu_m7_clip(h - cx)) & ~(Sint32)63) + ((d * real_y) & ~(Sint32)63) +
                   ((d * smx_ppu_m7_clip(v - cy)) & ~(Sint32)63) + (cy << 8);
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

static void smx_ppu_paint_bg(const SmxPpu *ppu, Uint32 x, Uint32 y, Uint8 screen, Uint8 bg,
                             bool high, bool sub, Uint16 *color, Uint8 *source, bool *hit) {
  Uint8 mode = (Uint8)(smx_bus_mmio(ppu->bus, 0x2105) & 0x07);
  Uint8 depth = 2;
  bool large = (smx_bus_mmio(ppu->bus, 0x2105) & (Uint8)(0x10u << bg)) != 0;
  bool opaque = false;
  bool priority = false;
  Uint16 sample = 0;
  Uint8 char_reg = 0;
  Uint8 nibble = 0;
  static const Uint16 map_regs[4] = {0x2107, 0x2108, 0x2109, 0x210A};

  if ((mode == 1 && bg == 3) || (mode == 2 && bg > 1) || ((mode == 3 || mode == 4) && bg > 1) ||
      (mode == 6 && bg > 0)) {
    return;
  }
  if ((mode == 1 && bg < 2) || mode == 2 || mode == 6 || (mode == 5 && bg == 0) ||
      (mode == 3 && bg == 1)) {
    depth = 4;
  } else if ((mode == 3 || mode == 4) && bg == 0) {
    depth = 8;
  }
  if ((screen & (Uint8)(1u << bg)) == 0) {
    return;
  }
  char_reg = smx_bus_mmio(ppu->bus, bg < 2 ? 0x210B : 0x210C);
  nibble = (bg & 1) == 0 ? (Uint8)(char_reg & 0x0F) : (Uint8)(char_reg >> 4);
  {
    Uint16 hscroll = ppu->bg_h[bg];
    Uint16 vscroll = ppu->bg_v[bg];
    smx_ppu_opt(ppu, mode, bg, smx_ppu_mosaic(ppu, bg, x), &hscroll, &vscroll);
    smx_ppu_sample_bg(ppu, smx_bus_mmio(ppu->bus, map_regs[bg]), nibble, depth, large,
                      mode == 5 || mode == 6, sub, smx_ppu_mosaic(ppu, bg, x),
                      smx_ppu_mosaic(ppu, bg, y), hscroll, vscroll, &opaque, &priority, &sample);
  }
  if (opaque && priority == high && !smx_ppu_window_masks(ppu, bg, x, sub ? 0x212F : 0x212E)) {
    *color = sample;
    *source = (Uint8)(bg + 1);
    *hit = true;
  }
}

static void smx_ppu_paint_sprite(const SmxPpu *ppu, Uint32 x, Uint32 y, Uint8 screen,
                                 Uint8 priority, bool sub, Uint16 *color, Uint8 *source,
                                 bool *hit) {
  Uint16 sprite_color = 0;
  if ((screen & 0x10) == 0) {
    return;
  }
  if (smx_ppu_sample_sprite(ppu, x, y, priority, &sprite_color) &&
      !smx_ppu_window_masks(ppu, 4, x, sub ? 0x212F : 0x212E)) {
    *color = sprite_color;
    *source = 5;
    *hit = true;
  }
}

static bool smx_ppu_screen(const SmxPpu *ppu, Uint32 x, Uint32 y, Uint8 screen, bool sub,
                           Uint16 *color, Uint8 *source) {
  Uint8 mode = (Uint8)(smx_bus_mmio(ppu->bus, 0x2105) & 0x07);
  bool bg3_high = (smx_bus_mmio(ppu->bus, 0x2105) & 0x08) != 0;
  bool hit = false;

  *color = ppu->cgram[0];
  *source = 0;
  if (mode == 0) {
    smx_ppu_paint_bg(ppu, x, y, screen, 3, false, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 2, false, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 0, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 3, true, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 2, true, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 1, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 1, false, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 0, false, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 2, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 1, true, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 0, true, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 3, sub, color, source, &hit);
  } else if (mode == 1) {
    smx_ppu_paint_bg(ppu, x, y, screen, 2, false, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 0, sub, color, source, &hit);
    if (!bg3_high) {
      smx_ppu_paint_bg(ppu, x, y, screen, 2, true, sub, color, source, &hit);
    }
    smx_ppu_paint_sprite(ppu, x, y, screen, 1, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 1, false, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 0, false, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 2, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 1, true, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 0, true, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 3, sub, color, source, &hit);
    if (bg3_high) {
      smx_ppu_paint_bg(ppu, x, y, screen, 2, true, sub, color, source, &hit);
    }
  } else if (mode == 2 || mode == 3 || mode == 4 || mode == 5 || mode == 6) {
    smx_ppu_paint_bg(ppu, x, y, screen, 1, false, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 0, false, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 0, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 1, true, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 1, sub, color, source, &hit);
    smx_ppu_paint_bg(ppu, x, y, screen, 0, true, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 2, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 3, sub, color, source, &hit);
  } else if (mode == 7) {
    Uint16 mode7 = 0;
    smx_ppu_paint_sprite(ppu, x, y, screen, 0, sub, color, source, &hit);
    if ((screen & 0x01) != 0 &&
        smx_ppu_sample_mode7(ppu, smx_ppu_mosaic(ppu, 0, x), smx_ppu_mosaic(ppu, 0, y), &mode7) &&
        !smx_ppu_window_masks(ppu, 0, x, sub ? 0x212F : 0x212E)) {
      *color = mode7;
      *source = 1;
      hit = true;
    }
    smx_ppu_paint_sprite(ppu, x, y, screen, 1, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 2, sub, color, source, &hit);
    smx_ppu_paint_sprite(ppu, x, y, screen, 3, sub, color, source, &hit);
  }
  return hit;
}

static void smx_ppu_render_at(SmxPpu *ppu, Uint32 sample, Uint32 row) {
  Uint8 inidisp = smx_bus_mmio(ppu->bus, 0x2100);
  bool blank = (inidisp & 0x80) != 0;
  Uint8 brightness = (Uint8)(inidisp & 0x0F);
  Uint32 x = 0;

  if (sample > SMX_PPU_HEIGHT || row >= SMX_PPU_HEIGHT || ppu->pixels == NULL) {
    return;
  }
  for (; x < SMX_PPU_WIDTH; x += 1) {
    Uint16 color = ppu->cgram[0];
    Uint16 sub = 0;
    Uint8 source = 0;
    Uint8 unused_source = 0;
    bool sub_hit = false;
    bool clip_math = false;
    Uint8 blue = 0;
    Uint8 green = 0;
    Uint8 red = 0;

    if (!blank && brightness != 0) {
      Uint8 select = smx_bus_mmio(ppu->bus, 0x2130);
      bool in_window = smx_ppu_color_window(ppu, x);
      smx_ppu_screen(ppu, x, sample, smx_bus_mmio(ppu->bus, 0x212C), false, &color, &source);
      sub_hit = smx_ppu_screen(ppu, x, sample, smx_bus_mmio(ppu->bus, 0x212D), true, &sub,
                               &unused_source);
      if (smx_ppu_region((Uint8)(select >> 6), in_window)) {
        color = 0;
      }
      if (smx_ppu_region((Uint8)((select >> 4) & 0x03), in_window)) {
        clip_math = true;
      }
    }
    if (blank || brightness == 0) {
      blue = 0;
      green = 0;
      red = 0;
    } else if (clip_math) {
      smx_ppu_rgb(color, brightness, &blue, &green, &red);
    } else {
      smx_ppu_rgb(smx_ppu_math(ppu, color, source, sub, sub_hit), brightness, &blue, &green, &red);
    }
    ppu->pixels[(row * SMX_PPU_WIDTH + x) * 4u] = blue;
    ppu->pixels[(row * SMX_PPU_WIDTH + x) * 4u + 1u] = green;
    ppu->pixels[(row * SMX_PPU_WIDTH + x) * 4u + 2u] = red;
    ppu->pixels[(row * SMX_PPU_WIDTH + x) * 4u + 3u] = 255;
  }
}

void smx_ppu_render_line(SmxPpu *ppu, Uint32 y) {
  smx_ppu_render_at(ppu, y, y);
}

void smx_ppu_present_line(SmxPpu *ppu, Uint32 scanline) {
  if (scanline < 1u) {
    return;
  }
  smx_ppu_render_at(ppu, scanline, scanline - 1u);
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
