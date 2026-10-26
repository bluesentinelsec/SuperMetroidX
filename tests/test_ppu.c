#include "smx/bus.h"
#include "smx/ppu.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_u8(const char *name, Uint8 got, Uint8 want) {
  if (got != want) {
    SDL_Log("ppu: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("ppu: %s", name);
    g_failed = true;
  }
}

/* Sprites on line 0, one red pixel at each sprite's left edge. `large` uses 16x16. */
static void place_sprites(SmxPpu *ppu, Uint32 count, Uint32 spacing, bool large, Uint8 attr) {
  Uint32 i = 0;

  SDL_memset(ppu->oam, 0, sizeof(ppu->oam));
  for (; i < 128; i += 1) {
    ppu->oam[i * 4u + 1u] = 0xE0;
  }
  for (i = 0; i < count; i += 1) {
    ppu->oam[i * 4u] = (Uint8)(i * spacing);
    ppu->oam[i * 4u + 1u] = 255;
    ppu->oam[i * 4u + 2u] = 1;
    ppu->oam[i * 4u + 3u] = attr;
    if (large) {
      ppu->oam[512u + i / 4u] = (Uint8)(ppu->oam[512u + i / 4u] | (2u << ((i & 3u) * 2u)));
    }
  }
}

static void test_sprite_limits(void) {
  SmxBus bus;
  SmxPpu ppu;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_ppu_init(&ppu, &bus);
  smx_bus_write(&bus, 0x002100, 0x0F);
  smx_bus_write(&bus, 0x002101, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x10);
  ppu.vram[32] = 0x80;
  smx_ppu_write_cgram(&ppu, 129, 0x001F);

  /* 33 sprites on a line: only the first 32 in OAM order are kept. */
  place_sprites(&ppu, 33, 7, false, 0);
  smx_ppu_render(&ppu);
  expect_u8("32nd sprite shown", ppu.pixels[(31u * 7u) * 4u + 2u], 255);
  expect_u8("33rd sprite dropped", ppu.pixels[(32u * 7u) * 4u + 2u], 0);
  expect_u8("range over", (Uint8)(smx_bus_read(&bus, 0x00213E) & 0x40), 0x40);
  expect_u8("ntsc version", (Uint8)(smx_bus_read(&bus, 0x00213F) & 0x1F), 0x03);

  /* 18 16x16 sprites need 36 tiles. They are fetched last first, so sprite 0 misses out. */
  place_sprites(&ppu, 18, 8, true, 0);
  smx_ppu_render(&ppu);
  expect_u8("first sprite out of time", ppu.pixels[2], 0);
  expect_u8("second sprite shown", ppu.pixels[8u * 4u + 2u], 255);
  expect_u8("time over", (Uint8)(smx_bus_read(&bus, 0x00213E) & 0xC0), 0x80);

  /* Color math only adds to sprites in palettes 4-7. */
  place_sprites(&ppu, 1, 0, false, 0);
  ppu.oam[4] = 8;
  ppu.oam[5] = 255;
  ppu.oam[6] = 1;
  ppu.oam[7] = 0x08;
  smx_ppu_write_cgram(&ppu, 129, 0x0010);
  smx_ppu_write_cgram(&ppu, 193, 0x0010);
  smx_bus_write(&bus, 0x002130, 0x00);
  smx_bus_write(&bus, 0x002131, 0x10);
  smx_bus_write(&bus, 0x002132, 0x2F);
  smx_ppu_render(&ppu);
  expect_u8("palette 0 sprite skips math", ppu.pixels[2], 132);
  expect_u8("palette 4 sprite adds", ppu.pixels[8u * 4u + 2u], 255);
  smx_ppu_quit(&ppu);
  smx_bus_quit(&bus);
}

int main(void) {
  SmxBus bus;
  SmxPpu ppu;
  Uint32 blank_hash = 0;
  Uint32 color_hash = 0;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_ppu_init(&ppu, &bus);

  smx_bus_write(&bus, 0x002100, 0x8F);
  smx_ppu_write_cgram(&ppu, 0, 0x7FFF);
  smx_ppu_render(&ppu);
  expect_u8("blank b", ppu.pixels[0], 0);
  expect_u8("blank g", ppu.pixels[1], 0);
  expect_u8("blank r", ppu.pixels[2], 0);
  blank_hash = smx_ppu_frame_hash(&ppu);

  smx_bus_write(&bus, 0x002100, 0x0F);
  smx_ppu_render(&ppu);
  expect_u8("white b", ppu.pixels[0], 255);
  expect_u8("white g", ppu.pixels[1], 255);
  expect_u8("white r", ppu.pixels[2], 255);
  color_hash = smx_ppu_frame_hash(&ppu);
  expect_true("hash changes", blank_hash != color_hash);

  /* The top three bits repeat into the low bits, so 3 is 24. */
  smx_ppu_write_cgram(&ppu, 0, (Uint16)(3u | (28u << 5) | (7u << 10)));
  smx_ppu_render(&ppu);
  expect_u8("expand r", ppu.pixels[2], 24);
  expect_u8("expand g", ppu.pixels[1], 231);
  expect_u8("expand b", ppu.pixels[0], 57);
  /* Brightness 7 scales 31 to 14 before the expansion. */
  smx_ppu_write_cgram(&ppu, 0, 0x7FFF);
  smx_bus_write(&bus, 0x002100, 0x07);
  smx_ppu_render(&ppu);
  expect_u8("dim r", ppu.pixels[2], 115);
  smx_bus_write(&bus, 0x002100, 0x0F);
  expect_true("no oracle match", !ppu.picture_matches_oracle);

  /* Mode 1 BG1: one 4bpp pixel at (0, 0), map at word $0400, tiles at word 0. */
  smx_bus_write(&bus, 0x002105, 0x01);
  smx_bus_write(&bus, 0x002107, 0x04);
  smx_bus_write(&bus, 0x00210B, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x01);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x80);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x04);
  smx_bus_write(&bus, 0x002118, 0x00);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002118, 0x01);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002121, 0x00);
  smx_bus_write(&bus, 0x002122, 0x00);
  smx_bus_write(&bus, 0x002122, 0x00);
  smx_bus_write(&bus, 0x002122, 0x1F);
  smx_bus_write(&bus, 0x002122, 0x00);
  smx_ppu_render(&ppu);
  expect_u8("tile r", ppu.pixels[2], 255);
  expect_u8("tile g", ppu.pixels[1], 0);
  expect_u8("tile b", ppu.pixels[0], 0);
  expect_u8("gap r", ppu.pixels[4 + 2], 0);
  expect_u8("next tile r", ppu.pixels[8 * 4 + 2], 0);
  /* A vertical write between the horizontal bytes keeps the low 3 bits. */
  smx_bus_write(&bus, 0x00210D, 0x07);
  smx_bus_write(&bus, 0x00210E, 0x00);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_ppu_render(&ppu);
  expect_u8("fine scroll r", ppu.pixels[2], 0);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210E, 0x00);
  smx_bus_write(&bus, 0x00210E, 0x00);
  smx_bus_write(&bus, 0x00210E, 0x00);
  smx_ppu_render(&ppu);
  expect_u8("scroll restored r", ppu.pixels[2], 255);
  smx_bus_write(&bus, 0x00210D, 0x08);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_ppu_render(&ppu);
  expect_u8("scrolled r", ppu.pixels[2], 0);
  expect_true("tile hash", smx_ppu_frame_hash(&ppu) != color_hash);
  expect_true("still no oracle", !ppu.picture_matches_oracle);

  /* One 8x8 sprite. OAM Y is one line above the picture line. */
  ppu.oam[0] = 0;
  ppu.oam[1] = 255;
  ppu.oam[2] = 1;
  ppu.oam[3] = 0;
  ppu.vram[32] = 0x80;
  ppu.cgram[129] = 0x001F;
  smx_bus_write(&bus, 0x002101, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x10);
  smx_ppu_render(&ppu);
  expect_u8("sprite r", ppu.pixels[2], 255);
  expect_u8("sprite gap", ppu.pixels[4 + 2], 0);

  smx_bus_write(&bus, 0x002102, 0x00);
  smx_bus_write(&bus, 0x002103, 0x00);
  {
    Uint32 i = 0;
    for (; i < 544; i += 1) {
      smx_bus_write(&bus, 0x002104, (Uint8)(i == 512 ? 0xA5 : 0x00));
    }
  }
  /* Mode 1 BG3 is 2bpp and shares the main screen with the playing field. */
  smx_bus_write(&bus, 0x002105, 0x01);
  smx_bus_write(&bus, 0x002109, 0x04);
  smx_bus_write(&bus, 0x00210C, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x04);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x04);
  smx_bus_write(&bus, 0x002118, 0x00);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x80);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_ppu_write_cgram(&ppu, 1, 0x001F);
  smx_ppu_render(&ppu);
  expect_u8("bg3 plane", ppu.vram[0], 0x80);
  expect_u8("bg3 map", ppu.vram[0x800], 0x00);
  expect_u8("bg3 r", ppu.pixels[2], 255);
  expect_u8("bg3 gap", ppu.pixels[4 + 2], 0);

  /* Mode 7 identity: map (0, 0) names tile 1, whose first pixel is color 2. */
  smx_bus_write(&bus, 0x002105, 0x07);
  smx_bus_write(&bus, 0x00212C, 0x01);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x01);
  smx_bus_write(&bus, 0x002116, 64);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002119, 0x02);
  smx_bus_write(&bus, 0x00211B, 0x00);
  smx_bus_write(&bus, 0x00211B, 0x01);
  smx_bus_write(&bus, 0x00211C, 0x00);
  smx_bus_write(&bus, 0x00211C, 0x00);
  smx_bus_write(&bus, 0x00211D, 0x00);
  smx_bus_write(&bus, 0x00211D, 0x00);
  smx_bus_write(&bus, 0x00211E, 0x00);
  smx_bus_write(&bus, 0x00211E, 0x01);
  smx_bus_write(&bus, 0x00211F, 0x00);
  smx_bus_write(&bus, 0x00211F, 0x00);
  smx_bus_write(&bus, 0x002120, 0x00);
  smx_bus_write(&bus, 0x002120, 0x00);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210E, 0x00);
  smx_bus_write(&bus, 0x00210E, 0x00);
  smx_ppu_write_cgram(&ppu, 2, 0x001F);
  smx_ppu_render(&ppu);
  expect_u8("mode7 r", ppu.pixels[2], 255);
  expect_u8("mode7 g", ppu.pixels[1], 0);
  expect_u8("mode7 clear", ppu.pixels[8 * 4 + 2], 0);
  expect_true("mode7 no oracle", !ppu.picture_matches_oracle);
  /* $211A = $80 hides samples past 1024. A negative scroll lands there. */
  smx_bus_write(&bus, 0x00211A, 0x80);
  smx_bus_write(&bus, 0x002130, 0x00);
  smx_bus_write(&bus, 0x002131, 0x00);
  smx_bus_write(&bus, 0x00210D, 0xF0);
  smx_bus_write(&bus, 0x00210D, 0x1F);
  smx_ppu_write_cgram(&ppu, 0, 0x03E0);
  smx_ppu_render(&ppu);
  expect_u8("mode7 outside g", ppu.pixels[1], 255);
  expect_u8("mode7 outside r", ppu.pixels[2], 0);
  smx_bus_write(&bus, 0x00211C, 0x00);
  smx_bus_write(&bus, 0x00211C, 0x03);
  smx_bus_write(&bus, 0x00211B, 0x00);
  smx_bus_write(&bus, 0x00211B, 0x02);
  expect_u8("m7 product low", smx_bus_read(&bus, 0x002134), 0x00);
  expect_u8("m7 product mid", smx_bus_read(&bus, 0x002135), 0x06);
  expect_u8("m7 product high", smx_bus_read(&bus, 0x002136), 0x00);

  expect_u8("oam high", ppu.oam[512], 0xA5);
  expect_u8("oam low kept", ppu.oam[0], 0x00);

  /* Window 1 covers x = 0 only and hides BG1 there. */
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x002123, 0x02);
  smx_bus_write(&bus, 0x002126, 0x00);
  smx_bus_write(&bus, 0x002127, 0x00);
  smx_bus_write(&bus, 0x00212E, 0x01);
  smx_ppu_render(&ppu);
  expect_u8("window r", ppu.pixels[2], 0);
  smx_bus_write(&bus, 0x00212E, 0x00);
  smx_ppu_render(&ppu);
  expect_u8("window off r", ppu.pixels[2], 255);

  /* Backdrop red plus fixed green. */
  smx_bus_write(&bus, 0x00212C, 0x00);
  smx_bus_write(&bus, 0x002121, 0x00);
  smx_bus_write(&bus, 0x002122, 0x1F);
  smx_bus_write(&bus, 0x002122, 0x00);
  smx_bus_write(&bus, 0x002132, 0x5F);
  smx_bus_write(&bus, 0x002131, 0x20);
  smx_ppu_render(&ppu);
  expect_u8("math r", ppu.pixels[2], 255);
  expect_u8("math g", ppu.pixels[1], 255);
  expect_u8("math b", ppu.pixels[0], 0);

  /* Mode 1 BG3 priority sits above a sprite when $2105 bit 3 is set. */
  smx_bus_write(&bus, 0x002131, 0x00);
  smx_bus_write(&bus, 0x002105, 0x01);
  smx_bus_write(&bus, 0x002109, 0x04);
  smx_bus_write(&bus, 0x00210C, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x14);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x04);
  smx_bus_write(&bus, 0x002118, 0x00);
  smx_bus_write(&bus, 0x002119, 0x20);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x80);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_ppu_write_cgram(&ppu, 1, 0x001F);
  ppu.oam[0] = 0;
  ppu.oam[1] = 255;
  ppu.oam[2] = 1;
  ppu.oam[3] = 0x30;
  ppu.oam[512] = 0;
  ppu.vram[32] = 0x80;
  ppu.cgram[129] = 0x7C00;
  smx_ppu_render(&ppu);
  expect_u8("sprite over bg3 b", ppu.pixels[0], 255);
  smx_bus_write(&bus, 0x002105, 0x09);
  smx_ppu_render(&ppu);
  expect_u8("bg3 over sprite r", ppu.pixels[2], 255);
  expect_u8("bg3 over sprite b", ppu.pixels[0], 0);

  /* Mosaic size 2 repeats the BG1 pixel at x = 0 onto x = 1. */
  smx_ppu_write_cgram(&ppu, 0, 0x0000);
  smx_bus_write(&bus, 0x002105, 0x01);
  smx_bus_write(&bus, 0x002107, 0x04);
  smx_bus_write(&bus, 0x00210B, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x01);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  {
    Uint8 byte = 0;
    for (; byte < 16; byte += 1) {
      smx_bus_write(&bus, 0x002118, byte == 0 ? 0x80 : 0x00);
      smx_bus_write(&bus, 0x002119, 0x00);
    }
  }
  smx_bus_write(&bus, 0x002106, 0x11);
  smx_ppu_render(&ppu);
  expect_u8("mosaic src r", ppu.pixels[2], 255);
  expect_u8("mosaic r", ppu.pixels[4 + 2], 255);
  smx_bus_write(&bus, 0x002106, 0x00);
  smx_ppu_render(&ppu);
  expect_u8("mosaic off r", ppu.pixels[4 + 2], 0);

  /* Subscreen green added to the red backdrop. */
  smx_ppu_write_cgram(&ppu, 0, 0x001F);
  smx_ppu_write_cgram(&ppu, 1, 0x03E0);
  smx_bus_write(&bus, 0x00212C, 0x00);
  smx_bus_write(&bus, 0x00212D, 0x01);
  smx_bus_write(&bus, 0x002132, 0xE0);
  smx_bus_write(&bus, 0x002130, 0x02);
  smx_bus_write(&bus, 0x002131, 0x20);
  smx_ppu_render(&ppu);
  expect_u8("sub r", ppu.pixels[2], 255);
  expect_u8("sub g", ppu.pixels[1], 255);
  expect_u8("sub gap g", ppu.pixels[4 + 1], 0);

  /* Color window covers x = 0 and forces that main pixel black. */
  smx_bus_write(&bus, 0x00212C, 0x00);
  smx_bus_write(&bus, 0x00212D, 0x00);
  smx_bus_write(&bus, 0x002131, 0x00);
  smx_bus_write(&bus, 0x002125, 0x20);
  smx_bus_write(&bus, 0x002126, 0x00);
  smx_bus_write(&bus, 0x002127, 0x00);
  smx_ppu_write_cgram(&ppu, 0, 0x7FFF);
  smx_bus_write(&bus, 0x002130, 0x80);
  smx_ppu_render(&ppu);
  expect_u8("black window r", ppu.pixels[2], 0);
  expect_u8("outside window r", ppu.pixels[4 + 2], 255);

  /* A 16x16 BG1 tile reads its second 8x8 character at x = 8. */
  smx_bus_write(&bus, 0x002130, 0x00);
  smx_bus_write(&bus, 0x002105, 0x11);
  smx_bus_write(&bus, 0x002107, 0x04);
  smx_bus_write(&bus, 0x00210B, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x01);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x04);
  smx_bus_write(&bus, 0x002118, 0x00);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x00);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002116, 16);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x80);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_ppu_write_cgram(&ppu, 0, 0x0000);
  smx_ppu_write_cgram(&ppu, 1, 0x001F);
  smx_ppu_render(&ppu);
  expect_u8("large gap r", ppu.pixels[2], 0);
  expect_u8("large r", ppu.pixels[8 * 4 + 2], 255);

  /* $212F hides the subscreen pixel. $212E does not. */
  smx_bus_write(&bus, 0x002105, 0x01);
  smx_bus_write(&bus, 0x00212C, 0x00);
  smx_bus_write(&bus, 0x00212D, 0x01);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x80);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_ppu_write_cgram(&ppu, 0, 0x001F);
  smx_ppu_write_cgram(&ppu, 1, 0x03E0);
  smx_bus_write(&bus, 0x002132, 0xE0);
  smx_bus_write(&bus, 0x002130, 0x02);
  smx_bus_write(&bus, 0x002131, 0x20);
  smx_bus_write(&bus, 0x002123, 0x02);
  smx_bus_write(&bus, 0x002126, 0x00);
  smx_bus_write(&bus, 0x002127, 0x00);
  smx_bus_write(&bus, 0x00212E, 0x00);
  smx_bus_write(&bus, 0x00212F, 0x01);
  smx_ppu_render(&ppu);
  expect_u8("sub window g", ppu.pixels[1], 0);
  smx_bus_write(&bus, 0x00212F, 0x00);
  smx_ppu_render(&ppu);
  expect_u8("sub window off g", ppu.pixels[1], 255);

  /* Mode 3 BG1 is 8bpp. Color 1 is the low bit of the first tile. */
  smx_bus_write(&bus, 0x002105, 0x03);
  smx_bus_write(&bus, 0x002107, 0x04);
  smx_bus_write(&bus, 0x00210B, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x01);
  smx_bus_write(&bus, 0x00212D, 0x00);
  smx_bus_write(&bus, 0x002130, 0x00);
  smx_bus_write(&bus, 0x002131, 0x00);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  {
    Uint32 word = 0;
    for (; word < 32; word += 1) {
      smx_bus_write(&bus, 0x002118, 0x00);
      smx_bus_write(&bus, 0x002119, 0x00);
    }
  }
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x80);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x04);
  smx_bus_write(&bus, 0x002118, 0x00);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_ppu_write_cgram(&ppu, 0, 0x0000);
  smx_ppu_write_cgram(&ppu, 1, 0x001F);
  smx_ppu_render(&ppu);
  expect_u8("mode3 r", ppu.pixels[2], 255);
  expect_u8("mode3 g", ppu.pixels[1], 0);
  expect_u8("mode3 neighbor r", ppu.pixels[4 + 2], 0);

  /* Direct color: pixel 1, palette 0 is red 4, not the CGRAM word. */
  smx_ppu_write_cgram(&ppu, 1, 0x7FFF);
  smx_bus_write(&bus, 0x002130, 0x01);
  smx_ppu_render(&ppu);
  expect_u8("direct r", ppu.pixels[2], (Uint8)((4u * 255u + 15u) / 31u));
  expect_u8("direct g", ppu.pixels[1], 0);
  expect_u8("direct b", ppu.pixels[0], 0);

  /* Mode 4 BG1 is the same 8bpp tile. BG2 is 2bpp and stays off here. */
  smx_bus_write(&bus, 0x002105, 0x04);
  smx_bus_write(&bus, 0x002130, 0x00);
  smx_ppu_write_cgram(&ppu, 1, 0x001F);
  smx_ppu_render(&ppu);
  expect_u8("mode4 r", ppu.pixels[2], 255);
  expect_u8("mode4 g", ppu.pixels[1], 0);

  /* Mode 2 offset-per-tile. Column 0 keeps BG1. Column 1 uses BG3 entry 0. */
  smx_bus_write(&bus, 0x002105, 0x02);
  smx_bus_write(&bus, 0x002107, 0x04);
  smx_bus_write(&bus, 0x002109, 0x0C);
  smx_bus_write(&bus, 0x00212C, 0x01);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210D, 0x00);
  smx_bus_write(&bus, 0x00210E, 0x00);
  smx_bus_write(&bus, 0x00210E, 0x00);
  smx_bus_write(&bus, 0x002111, 0x00);
  smx_bus_write(&bus, 0x002111, 0x00);
  smx_bus_write(&bus, 0x002112, 0x00);
  smx_bus_write(&bus, 0x002112, 0x00);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x01);
  smx_bus_write(&bus, 0x002117, 0x04);
  smx_bus_write(&bus, 0x002118, 0x01);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002116, 0x02);
  smx_bus_write(&bus, 0x002117, 0x04);
  smx_bus_write(&bus, 0x002118, 0x00);
  smx_bus_write(&bus, 0x002119, 0x04);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x0C);
  smx_bus_write(&bus, 0x002118, 0x08);
  smx_bus_write(&bus, 0x002119, 0x20);
  smx_ppu_write_cgram(&ppu, 0, 0x0000);
  smx_ppu_write_cgram(&ppu, 1, 0x001F);
  smx_ppu_write_cgram(&ppu, 17, 0x03E0);
  smx_ppu_render(&ppu);
  expect_u8("opt first r", ppu.pixels[2], 255);
  expect_u8("opt column g", ppu.pixels[8 * 4 + 1], 255);
  expect_u8("opt column r", ppu.pixels[8 * 4 + 2], 0);

  /* Mode 5 main screen keeps even hi-res pixels. 0xC0 is source pixels 0 and 1. */
  smx_bus_write(&bus, 0x002105, 0x05);
  smx_bus_write(&bus, 0x002107, 0x04);
  smx_bus_write(&bus, 0x00212C, 0x01);
  smx_bus_write(&bus, 0x00212D, 0x00);
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0xC0);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x04);
  smx_bus_write(&bus, 0x002118, 0x00);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_ppu_write_cgram(&ppu, 0, 0x0000);
  smx_ppu_write_cgram(&ppu, 1, 0x001F);
  smx_ppu_render(&ppu);
  expect_u8("hires even r", ppu.pixels[2], 255);
  expect_u8("hires odd skipped r", ppu.pixels[4 + 2], 0);

  /* Sprite 0 is red and sprite 1 is green. $2103 bit 7 puts sprite 1 in front. */
  smx_bus_write(&bus, 0x002100, 0x0F);
  smx_bus_write(&bus, 0x002105, 0x00);
  smx_bus_write(&bus, 0x00212C, 0x10);
  smx_bus_write(&bus, 0x00212E, 0x00);
  smx_bus_write(&bus, 0x002131, 0x00);
  smx_bus_write(&bus, 0x002101, 0x00);
  smx_ppu_write_cgram(&ppu, 0, 0x0000);
  smx_ppu_write_cgram(&ppu, 129, 0x001F);
  smx_ppu_write_cgram(&ppu, 145, 0x03E0);
  SDL_memset(ppu.oam, 0, sizeof(ppu.oam));
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x00);
  smx_bus_write(&bus, 0x002118, 0x80);
  smx_bus_write(&bus, 0x002119, 0x00);
  smx_bus_write(&bus, 0x002102, 0x00);
  smx_bus_write(&bus, 0x002103, 0x00);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0xFF);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0x30);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0xFF);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0x32);
  smx_ppu_render(&ppu);
  expect_u8("sprite order r", ppu.pixels[2], 255);
  smx_bus_write(&bus, 0x002102, 0x02);
  smx_bus_write(&bus, 0x002103, 0x80);
  smx_ppu_render(&ppu);
  expect_u8("sprite rotate g", ppu.pixels[1], 255);
  expect_u8("sprite rotate r", ppu.pixels[2], 0);
  /* OAM order picks the sprite pixel. Priority 3 on sprite 1 does not lift it over sprite 0. */
  smx_bus_write(&bus, 0x002102, 0x00);
  smx_bus_write(&bus, 0x002103, 0x00);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0xFF);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0xFF);
  smx_bus_write(&bus, 0x002104, 0x00);
  smx_bus_write(&bus, 0x002104, 0x32);
  smx_ppu_render(&ppu);
  expect_u8("sprite index beats priority r", ppu.pixels[2], 255);
  expect_u8("sprite index beats priority g", ppu.pixels[1], 0);

  /* VRAM reads return the prefetched word. The stepping read refills it from
   * the address before stepping, so the first word comes back twice. */
  smx_bus_write(&bus, 0x002115, 0x80);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x01);
  smx_bus_write(&bus, 0x002118, 0x34);
  smx_bus_write(&bus, 0x002119, 0x12);
  smx_bus_write(&bus, 0x002118, 0x78);
  smx_bus_write(&bus, 0x002119, 0x56);
  smx_bus_write(&bus, 0x002116, 0x00);
  smx_bus_write(&bus, 0x002117, 0x01);
  expect_u8("vram read lo", smx_bus_read(&bus, 0x002139), 0x34);
  expect_u8("vram read hi", smx_bus_read(&bus, 0x00213A), 0x12);
  expect_u8("vram again lo", smx_bus_read(&bus, 0x002139), 0x34);
  expect_u8("vram again hi", smx_bus_read(&bus, 0x00213A), 0x12);
  expect_u8("vram next lo", smx_bus_read(&bus, 0x002139), 0x78);
  expect_u8("vram next hi", smx_bus_read(&bus, 0x00213A), 0x56);
  smx_ppu_write_cgram(&ppu, 5, 0x7ABC);
  smx_bus_write(&bus, 0x002121, 0x05);
  expect_u8("cgram read lo", smx_bus_read(&bus, 0x00213B), 0xBC);
  expect_u8("cgram read hi", (Uint8)(smx_bus_read(&bus, 0x00213B) & 0x7F), 0x7A);

  smx_ppu_quit(&ppu);
  smx_bus_quit(&bus);
  test_sprite_limits();
  return g_failed ? 1 : 0;
}
