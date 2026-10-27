#include "smx/apu.h"
#include "smx/cart.h"
#include "smx/clock.h"
#include "smx/game.h"
#include "smx/motion.h"
#include "smx/ppu.h"
#include "smx/save.h"
#include "smx/translate.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("motion: %s", name);
    g_failed = true;
  }
}

/*
 * A mode 1 screen: BG1 of 8x8 tiles that differ by column, so a scroll shows,
 * and every sprite hidden. Sprites take their tiles from the same VRAM.
 */
static void setup(SmxBus *bus, SmxPpu *ppu) {
  Uint32 tile = 0;
  Uint32 row = 0;
  Uint32 index = 0;

  smx_bus_init(bus, NULL, 0, 0);
  smx_ppu_init(ppu, bus);
  smx_bus_write(bus, 0x2100, 0x0F);
  smx_bus_write(bus, 0x2105, 0x01);
  smx_bus_write(bus, 0x2107, 0x40);
  smx_bus_write(bus, 0x210B, 0x00);
  smx_bus_write(bus, 0x2101, 0x02);
  smx_bus_write(bus, 0x212C, 0x11);
  /* Tiles 1-32 of 4bpp at VRAM 0: tile t is color t & 15 in a stripe pattern. */
  for (tile = 1; tile <= 32u; tile += 1) {
    for (row = 0; row < 8u; row += 1) {
      Uint8 color = (Uint8)(tile & 15u);
      Uint32 base = tile * 32u + row * 2u;
      ppu->vram[base] = (Uint8)((color & 1u) != 0 ? 0xF0 : 0x00);
      ppu->vram[base + 1u] = (Uint8)((color & 2u) != 0 ? 0xF0 : 0x00);
      ppu->vram[base + 16u] = (Uint8)((color & 4u) != 0 ? 0xF0 : 0x00);
      ppu->vram[base + 17u] = (Uint8)((color & 8u) != 0 ? 0xF0 : 0x00);
    }
  }
  /* The map at word $4000: column c shows tile c % 32 + 1. */
  for (index = 0; index < 32u * 32u; index += 1) {
    Uint16 entry = (Uint16)((index % 32u) + 1u);
    ppu->vram[0x8000u + index * 2u] = (Uint8)entry;
    ppu->vram[0x8000u + index * 2u + 1u] = (Uint8)(entry >> 8);
  }
  for (index = 0; index < 256u; index += 1) {
    smx_ppu_write_cgram(ppu, (Uint8)index, (Uint16)(index * 0x0421u));
  }
  /* Every sprite hidden below the screen. */
  for (index = 0; index < 128u; index += 1) {
    ppu->oam[index * 4u + 1u] = 0xF0;
  }
}

static void set_scroll(SmxBus *bus, Uint16 h, Uint16 v) {
  smx_bus_write(bus, 0x210D, (Uint8)h);
  smx_bus_write(bus, 0x210D, (Uint8)(h >> 8));
  smx_bus_write(bus, 0x210E, (Uint8)v);
  smx_bus_write(bus, 0x210E, (Uint8)(v >> 8));
}

static void draw_picture(SmxPpu *ppu) {
  Uint32 line = 1;

  for (; line <= SMX_PPU_HEIGHT; line += 1) {
    smx_ppu_present_line(ppu, line);
  }
  smx_ppu_end_picture(ppu);
}

/* The picture `ppu` draws at scroll `h`, `v`, for comparison. */
static bool same_as_scroll(const Uint8 *pixels, Uint16 h, Uint16 v) {
  SmxBus bus;
  SmxPpu ppu;
  bool same = false;

  setup(&bus, &ppu);
  set_scroll(&bus, h, v);
  draw_picture(&ppu);
  same = SDL_memcmp(pixels, ppu.picture, (size_t)SMX_PPU_WIDTH * SMX_PPU_HEIGHT * 4u) == 0;
  smx_ppu_quit(&ppu);
  smx_bus_quit(&bus);
  return same;
}

static void test_scroll(void) {
  SmxBus bus;
  SmxPpu ppu;
  SmxMotion *motion = (SmxMotion *)SDL_calloc(1, sizeof(SmxMotion));
  SmxMotionShift shift;
  const Uint8 *between = NULL;

  setup(&bus, &ppu);
  expect("motion starts", motion != NULL && smx_motion_init(motion, &ppu));
  set_scroll(&bus, 0, 0);
  draw_picture(&ppu);
  expect("one picture is not enough to move", smx_motion_draw(motion, 0.5f, &shift) == NULL);
  set_scroll(&bus, 4, 2);
  draw_picture(&ppu);

  expect("the last picture shows as it is at alpha 1", smx_motion_draw(motion, 1.0f, &shift) == NULL);
  between = smx_motion_draw(motion, 0.5f, &shift);
  expect("halfway is drawn at the halfway scroll",
         between != NULL && same_as_scroll(between, 2, 1));
  expect("halfway is neither picture",
         between != NULL && !same_as_scroll(between, 4, 2) && !same_as_scroll(between, 0, 0));
  expect("a whole-pixel scroll leaves no fraction",
         between != NULL && shift.x[100] == 0.0f && shift.y == 0.0f);
  expect("the whole screen moves", shift.top == 0 && shift.bottom == SMX_PPU_HEIGHT);

  /* 4 - 0.625 * 4 = 1.5: drawn at 1, with half a pixel left for the presenter. */
  between = smx_motion_draw(motion, 0.375f, &shift);
  expect("a fraction is drawn at the whole pixel below it",
         between != NULL && same_as_scroll(between, 1, 0));
  expect("the fraction is left over", shift.x[100] == 0.5f && shift.y == 0.75f);

  expect("the redraw is the picture",
         SDL_memcmp(smx_motion_redraw(motion), ppu.picture,
                    (size_t)SMX_PPU_WIDTH * SMX_PPU_HEIGHT * 4u) == 0);

  /* A jump across the map is a cut, not motion. */
  set_scroll(&bus, 300, 2);
  draw_picture(&ppu);
  expect("a cut does not move", smx_motion_draw(motion, 0.5f, &shift) == NULL);

  smx_motion_reset(motion);
  draw_picture(&ppu);
  expect("after a reset there is nothing to move from",
         smx_motion_draw(motion, 0.5f, &shift) == NULL);

  smx_motion_quit(motion);
  SDL_free(motion);
  expect("quitting removes the hooks", ppu.line_hook == NULL && ppu.picture_hook == NULL);
  smx_ppu_quit(&ppu);
  smx_bus_quit(&bus);
}

static void test_sprite(void) {
  SmxBus bus;
  SmxPpu ppu;
  SmxMotion *motion = (SmxMotion *)SDL_calloc(1, sizeof(SmxMotion));
  SmxMotionShift shift;
  const Uint8 *between = NULL;

  setup(&bus, &ppu);
  expect("motion starts", motion != NULL && smx_motion_init(motion, &ppu));
  /* Sprite 0: tile 1 (from the BG tiles, which share VRAM 0 here), palette 0, at 100, 80. */
  smx_bus_write(&bus, 0x2101, 0x00);
  ppu.oam[0] = 100;
  ppu.oam[1] = 80;
  ppu.oam[2] = 1;
  ppu.oam[3] = 0x30;
  draw_picture(&ppu);
  ppu.oam[0] = 108;
  ppu.oam[1] = 84;
  draw_picture(&ppu);
  between = smx_motion_draw(motion, 0.5f, &shift);
  expect("a moving sprite is drawn between", between != NULL);
  expect("halfway along", motion->ppu.oam[0] == 104 && motion->ppu.oam[1] == 82);
  expect("the source keeps its OAM", ppu.oam[0] == 108 && ppu.oam[1] == 84);
  smx_motion_quit(motion);
  SDL_free(motion);
  smx_ppu_quit(&ppu);
  smx_bus_quit(&bus);
}

/* Frames of play recorded per section, after the room has settled. */
#define ROM_FRAMES 240u

typedef struct Console {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxApu apu;
  SmxClock clock;
  SmxMotion motion;
} Console;

/* One frame, dropping the audio. */
static bool frame(Console *console) {
  Sint16 samples[1024 * 2];
  bool ok = smx_clock_frame(&console->clock);

  smx_apu_drain(&console->apu, samples, 1024);
  return ok;
}

/*
 * From `section`'s save station in play: the redraw matches every picture,
 * and in-between pictures draw.
 */
static void test_rom_section(const SmxCart *cart, const char *name) {
  Console *console = (Console *)SDL_calloc(1, sizeof(Console));
  Uint32 index = 0;
  Uint32 quiet = 0;
  Uint32 mismatched = 0;
  Uint32 moved = 0;

  if (console == NULL) {
    expect("console", false);
    return;
  }
  smx_bus_init(&console->bus, cart->bytes, cart->size, smx_cart_sram_bytes(cart->sram_size_code));
  smx_cpu_init(&console->cpu, &console->bus);
  smx_dma_init(&console->dma, &console->bus);
  smx_ppu_init(&console->ppu, &console->bus);
  smx_apu_init(&console->apu, &console->bus);
  smx_clock_init(&console->clock, &console->cpu, &console->dma);
  console->clock.ppu = &console->ppu;
  smx_cpu_reset(&console->cpu);
  if (smx_game.rom_hash == smx_translate_rom_hash(cart->bytes, cart->size)) {
    console->cpu.code = &smx_game.code;
  }
  for (index = 0; index < 2000u && quiet < 30u; index += 1) {
    frame(console);
    quiet = smx_save_formatted(console->bus.sram) ? quiet + 1u : 0u;
  }
  smx_save_write(console->bus.sram, 0, &smx_save_section(name)->loadout);
  for (index = 0; index < 6000u && console->bus.wram[0x0998] != 0x08; index += 1) {
    console->clock.joy = (index % 32u) < 4u ? 0x1080 : 0;
    frame(console);
  }
  console->clock.joy = 0;
  for (index = 0; index < 120u; index += 1) {
    frame(console);
  }
  expect("motion starts", smx_motion_init(&console->motion, &console->ppu));
  for (index = 0; index < ROM_FRAMES; index += 1) {
    SmxMotionShift shift;
    const Uint8 *redrawn = NULL;
    /* Right with run, jumping now and then. */
    console->clock.joy = (Uint16)(0x0100 | 0x8000 | ((index % 50u) < 25u ? 0x0080 : 0));
    expect("the game runs", frame(console));
    smx_motion_objects(&console->motion, console->bus.wram);
    if (smx_motion_draw(&console->motion, 0.5f, &shift) != NULL) {
      moved += 1;
    }
    redrawn = smx_motion_redraw(&console->motion);
    if (index >= 2u && (redrawn == NULL ||
                        SDL_memcmp(redrawn, console->ppu.picture,
                                   (size_t)SMX_PPU_WIDTH * SMX_PPU_HEIGHT * 4u) != 0)) {
      mismatched += 1;
    }
  }
  if (mismatched != 0 || moved == 0) {
    SDL_Log("motion: %s: %u of %u redraws differ from the picture, %u frames moved", name,
            (unsigned)mismatched, (unsigned)ROM_FRAMES, (unsigned)moved);
    g_failed = true;
  }
  smx_motion_quit(&console->motion);
  smx_ppu_quit(&console->ppu);
  smx_bus_quit(&console->bus);
  SDL_free(console);
}

/* With a ROM: `smx_test_motion ROM` checks the redraw against the game's pictures. */
int main(int argc, char **argv) {
  test_scroll();
  test_sprite();
  if (argc > 1) {
    static const char *const sections[] = {"explore", "ceres", "norfair", "maridia"};
    SmxCart cart;
    Uint32 index = 0;
    smx_cart_init(&cart);
    expect("the ROM loads", smx_cart_load_path(&cart, argv[1]));
    for (; cart.bytes != NULL && index < sizeof(sections) / sizeof(sections[0]); index += 1) {
      test_rom_section(&cart, sections[index]);
    }
    smx_cart_free(&cart);
  }
  return g_failed ? 1 : 0;
}
