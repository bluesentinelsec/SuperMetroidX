#include "smx/save.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("save: %s", name);
    g_failed = true;
  }
}

static Uint16 get16(const Uint8 *at) {
  return (Uint16)(at[0] | ((Uint16)at[1] << 8));
}

int main(void) {
  static Uint8 sram[SMX_SAVE_SRAM_SIZE];
  const Uint8 *slot = sram + 0x10u + SMX_SAVE_SLOT_SIZE;
  SmxSaveLoadout loadout;
  Uint16 items = 0;
  Uint16 beams = 0;
  Uint32 count = 0;
  Uint32 index = 0;
  const SmxSaveSection *sections = smx_save_sections(&count);

  expect_true("blank not formatted", !smx_save_formatted(sram));
  sram[0x1FE3] = 0x61;
  expect_true("signature formats", smx_save_formatted(sram));
  expect_true("blank slot empty", !smx_save_valid(sram, 1));

  SDL_memset(&loadout, 0, sizeof(loadout));
  loadout.area = SMX_SAVE_NORFAIR;
  loadout.station = 2;
  loadout.items = SMX_SAVE_MORPH | SMX_SAVE_VARIA;
  loadout.beams = SMX_SAVE_SPAZER | SMX_SAVE_PLASMA;
  loadout.energy = 299;
  loadout.missiles = 25;
  smx_save_write(sram, 1, &loadout);
  expect_true("written slot valid", smx_save_valid(sram, 1));
  expect_true("other slots empty", !smx_save_valid(sram, 0) && !smx_save_valid(sram, 2));
  expect_true("items equipped and collected",
              get16(slot) == loadout.items && get16(slot + 2) == loadout.items);
  expect_true("spazer unequipped beside plasma",
              get16(slot + 4) == SMX_SAVE_PLASMA && get16(slot + 6) == loadout.beams);
  expect_true("energy and maximum", get16(slot + 0x20) == 299 && get16(slot + 0x22) == 299);
  expect_true("missiles and maximum", get16(slot + 0x24) == 25 && get16(slot + 0x26) == 25);
  expect_true("jump on A", get16(slot + 0x08 + 5u * 2u) == 0x0080);
  expect_true("station load mode", get16(slot + 0x154) == 0x05);
  expect_true("station", get16(slot + 0x156) == 2 && get16(slot + 0x158) == SMX_SAVE_NORFAIR);
  expect_true("sums mirrored", get16(sram + 2) == get16(sram + 0x1FF2) &&
                                   get16(sram + 0x0A) == (Uint16)~get16(sram + 2));

  /* Either copy of the sums is enough, as in the game. */
  sram[2] ^= 0xFF;
  expect_true("mirror still valid", smx_save_valid(sram, 1));
  sram[0x1FF2] ^= 0xFF;
  expect_true("both copies bad", !smx_save_valid(sram, 1));

  expect_true("item names", smx_save_add_item("morph", &items, &beams) &&
                                smx_save_add_item("charge", &items, &beams) &&
                                items == SMX_SAVE_MORPH && beams == SMX_SAVE_CHARGE);
  expect_true("unknown item", !smx_save_add_item("boots", &items, &beams));
  expect_true("sections", count >= 6 && smx_save_section("norfair") != NULL &&
                              smx_save_section("nowhere") == NULL);
  for (; index < count; index += 1) {
    expect_true("section has energy", sections[index].loadout.energy != 0);
  }
  return g_failed ? 1 : 0;
}
