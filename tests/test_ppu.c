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

  smx_ppu_quit(&ppu);
  smx_bus_quit(&bus);
  return g_failed ? 1 : 0;
}
