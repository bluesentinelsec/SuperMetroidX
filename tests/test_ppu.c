#include "smx/bus.h"
#include "smx/ppu.h"
#include "smx/translate.h"

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
  expect_true("no oracle match", !ppu.picture_matches_oracle);
  expect_true("translator blocked", !smx_translate_ready(&ppu));

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

  expect_u8("oam high", ppu.oam[512], 0xA5);
  expect_u8("oam low kept", ppu.oam[0], 0x00);

  smx_ppu_quit(&ppu);
  smx_bus_quit(&bus);
  return g_failed ? 1 : 0;
}
