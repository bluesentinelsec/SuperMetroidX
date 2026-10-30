#include "smx/apu.h"
#include "smx/cart.h"
#include "smx/cheat.h"
#include "smx/clock.h"
#include "smx/game.h"
#include "smx/overlay.h"
#include "smx/ppu.h"
#include "smx/save.h"
#include "smx/translate.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("cheat: %s", name);
    g_failed = true;
  }
}

static Uint16 get16(const Uint8 *at, Uint32 offset) {
  return (Uint16)(at[offset] | ((Uint16)at[offset + 1u] << 8));
}

static void put16(Uint8 *at, Uint32 offset, Uint16 value) {
  at[offset] = (Uint8)value;
  at[offset + 1u] = (Uint8)(value >> 8);
}

/* WRAM in play, with no items, 99 energy, and the default controls. */
static Uint8 *fake_wram(void) {
  Uint8 *wram = (Uint8 *)SDL_calloc(1, 0x20000);

  put16(wram, 0x0998, 0x08);
  put16(wram, 0x09B4, 0x0080);
  put16(wram, 0x09C2, 99);
  put16(wram, 0x09C4, 99);
  return wram;
}

static void test_parse(void) {
  SmxCheats cheats;

  smx_cheat_init(&cheats, NULL, 0);
  expect("a list parses", smx_cheat_parse(&cheats, "life,fly"));
  expect("the listed cheats are on",
         cheats.on[SMX_CHEAT_LIFE] && cheats.on[SMX_CHEAT_FLY] && !cheats.on[SMX_CHEAT_AMMO]);
  expect("an unknown cheat is refused", !smx_cheat_parse(&cheats, "life,moon"));
  expect("an empty name is refused", !smx_cheat_parse(&cheats, "life,,fly"));
  smx_cheat_init(&cheats, NULL, 0);
  expect("all parses", smx_cheat_parse(&cheats, "all"));
  expect("all turns every cheat on", cheats.on[SMX_CHEAT_AMMO] && cheats.on[SMX_CHEAT_HYPER]);
  expect("all leaves turbo off", cheats.turbo == 0);
  expect("turbo speeds", smx_cheat_turbo_percent(0) == 100 && smx_cheat_turbo_percent(1) == 150 &&
                             smx_cheat_turbo_percent(4) == 400 &&
                             SDL_strcmp(smx_cheat_turbo_name(2), "2x") == 0);
  smx_cheat_turbo_next(&cheats);
  expect("turbo steps", cheats.turbo == 1);
  cheats.turbo = SMX_CHEAT_TURBO_SPEEDS - 1u;
  smx_cheat_turbo_next(&cheats);
  expect("and turns off after the fastest", cheats.turbo == 0);
  expect("names", SDL_strcmp(smx_cheat_name(SMX_CHEAT_FLY), "fly") == 0 &&
                      SDL_strcmp(smx_cheat_title(SMX_CHEAT_LIFE), "Infinite life") == 0);
}

static void test_ram(void) {
  SmxCheats cheats;
  Uint8 *wram = fake_wram();

  smx_cheat_init(&cheats, NULL, 0);
  smx_cheat_toggle(&cheats, SMX_CHEAT_LIFE);
  smx_cheat_toggle(&cheats, SMX_CHEAT_AMMO);
  put16(wram, 0x0998, 0x0C);
  put16(wram, 0x09C2, 10);
  smx_cheat_apply(&cheats, wram, 0);
  expect("outside play nothing changes", get16(wram, 0x09C2) == 10 && get16(wram, 0x09C6) == 0);

  put16(wram, 0x0998, 0x08);
  put16(wram, 0x0A50, 300);
  put16(wram, 0x09D4, 100);
  smx_cheat_apply(&cheats, wram, 0);
  expect("life fills energy", get16(wram, 0x09C2) == 99);
  expect("life fills the reserve", get16(wram, 0x09D6) == 100);
  expect("a hit bigger than full energy leaves one", get16(wram, 0x0A50) == 98);
  expect("ammo before any expansion gives 99",
         get16(wram, 0x09C6) == 99 && get16(wram, 0x09C8) == 99 && get16(wram, 0x09D0) == 99);
  put16(wram, 0x09C8, 15);
  put16(wram, 0x09C6, 3);
  smx_cheat_apply(&cheats, wram, 0);
  expect("ammo fills to the maximum", get16(wram, 0x09C6) == 15);

  smx_cheat_toggle(&cheats, SMX_CHEAT_AMMO);
  put16(wram, 0x09C6, 3);
  smx_cheat_apply(&cheats, wram, 0);
  expect("ammo off leaves the count", get16(wram, 0x09C6) == 3);

  smx_cheat_toggle(&cheats, SMX_CHEAT_FLY);
  smx_cheat_apply(&cheats, wram, 0x0040);
  expect("fly waits for the jump button", get16(wram, 0x0B36) == 0);
  smx_cheat_apply(&cheats, wram, 0x0080);
  expect("fly rises while jump is held", get16(wram, 0x0B36) == 1 && get16(wram, 0x0B2E) == 4);
  put16(wram, 0x09B4, 0x8000);
  put16(wram, 0x0B36, 2);
  smx_cheat_apply(&cheats, wram, 0x0080);
  expect("fly follows the jump button the game is set to", get16(wram, 0x0B36) == 2);
  SDL_free(wram);
}

static void test_items_hyper(void) {
  SmxCheats cheats;
  Uint8 *wram = fake_wram();
  /* A LoROM image with the suit palettes, HUD icons, and rainbow where the game has them. */
  Uint8 *rom = (Uint8 *)SDL_calloc(1, 0x100000);
  Uint32 index = 0;

  put16(rom, 0x08D727u + 4u, 0x9800);
  for (; index < 16u; index += 1) {
    put16(rom, 0x0D9800u + index * 2u, (Uint16)(0x100u + index));
  }
  for (index = 0; index < 0x2Cu; index += 2) {
    put16(rom, 0x0019A3u + index, (Uint16)(0x3400u + index));
  }
  put16(rom, 0x06E1F2u, 0xD900);
  /* Beam tiles and colors for Charge+Wave+Plasma (index 9) and for none (index 0). */
  put16(rom, 0x0843B1u + 18u, 0xA000);
  put16(rom, 0x0843C9u + 18u, 0xC500);
  put16(rom, 0x0843B1u, 0xB000);
  put16(rom, 0x0843C9u, 0xC600);
  for (index = 0; index < 16u; index += 1) {
    put16(rom, 0x084500u + index * 2u, (Uint16)(0x900u + index));
    put16(rom, 0x084600u + index * 2u, (Uint16)(0x600u + index));
  }
  for (index = 0; index < 0x80u; index += 2) {
    put16(wram, 0xC608u + index, 0x2C0F);
  }

  smx_cheat_init(&cheats, rom, 0x100000);
  smx_cheat_toggle(&cheats, SMX_CHEAT_ITEMS);
  smx_cheat_toggle(&cheats, SMX_CHEAT_ITEMS);
  expect("items cannot be switched off before they are given", cheats.on[SMX_CHEAT_ITEMS]);
  expect("items report being given", smx_cheat_apply(&cheats, wram, 0));
  expect("items are given once", !cheats.on[SMX_CHEAT_ITEMS] && !smx_cheat_apply(&cheats, wram, 0));
  expect("every item", get16(wram, 0x09A2) == 0xF32F && get16(wram, 0x09A4) == 0xF32F);
  expect("every beam, with Plasma over Spazer",
         get16(wram, 0x09A6) == 0x100B && get16(wram, 0x09A8) == 0x100F);
  expect("every tank and expansion", get16(wram, 0x09C2) == 1499 && get16(wram, 0x09C4) == 1499 &&
                                         get16(wram, 0x09C6) == 230 && get16(wram, 0x09CC) == 50 &&
                                         get16(wram, 0x09D0) == 50 && get16(wram, 0x09D6) == 400);
  expect("reserve tanks refill on their own", get16(wram, 0x09C0) == 1);
  expect("the gravity suit's colors", get16(wram, 0xC180) == 0x100 && get16(wram, 0xC19E) == 0x10F);
  expect("the missile icon", get16(wram, 0xC61C) == 0x3400 && get16(wram, 0xC620) == 0x3404 &&
                                 get16(wram, 0xC65C) == 0x3406 && get16(wram, 0xC660) == 0x340A);
  expect("the x-ray icon", get16(wram, 0xC636) == 0x3424 && get16(wram, 0xC678) == 0x342A);

  put16(wram, 0x1E7D, 0x1234);
  put16(wram, 0x09A6, 0);
  put16(wram, 0x0330, 7);
  smx_cheat_toggle(&cheats, SMX_CHEAT_HYPER);
  smx_cheat_apply(&cheats, wram, 0);
  expect("hyper sets the game's flag", get16(wram, 0x0A76) == 0x8000);
  expect("hyper equips Charge, Wave, and Plasma", get16(wram, 0x09A6) == 0x1009);
  expect("hyper queues the beam's tiles after what the game queued",
         get16(wram, 0x00D7) == 0x0100 && get16(wram, 0x00D9) == 0xA000 && wram[0x00DB] == 0x9A &&
             get16(wram, 0x00DC) == 0x6300 && get16(wram, 0x0330) == 14);
  expect("hyper loads the beam's colors",
         get16(wram, 0xC1C0) == 0x900 && get16(wram, 0xC1DE) == 0x90F);
  expect("hyper starts the rainbow in a free slot", get16(wram, 0x1E7D + 14u) == 0xE1F0 &&
                                                        get16(wram, 0x1EBD + 14u) == 0xD900 &&
                                                        get16(wram, 0x1E7D) == 0x1234);
  put16(wram, 0xC1C2, 0x7FFF);
  put16(wram, 0x0330, 0);
  smx_cheat_toggle(&cheats, SMX_CHEAT_HYPER);
  smx_cheat_apply(&cheats, wram, 0);
  expect("hyper off clears the flag and the rainbow",
         get16(wram, 0x0A76) == 0 && get16(wram, 0x1E7D + 14u) == 0);
  expect("hyper off brings the beams back", get16(wram, 0x09A6) == 0);
  expect("hyper off loads their tiles and colors",
         get16(wram, 0x00D2) == 0xB000 && get16(wram, 0xC1C2) == 0x601);

  /* Items given while the hyper beam is on wait behind it. */
  smx_cheat_toggle(&cheats, SMX_CHEAT_HYPER);
  smx_cheat_apply(&cheats, wram, 0);
  smx_cheat_toggle(&cheats, SMX_CHEAT_ITEMS);
  smx_cheat_apply(&cheats, wram, 0);
  expect("items keep the hyper beam equipped", get16(wram, 0x09A6) == 0x1009);
  smx_cheat_toggle(&cheats, SMX_CHEAT_HYPER);
  smx_cheat_apply(&cheats, wram, 0);
  expect("hyper off then equips every beam but Spazer", get16(wram, 0x09A6) == 0x100B);
  SDL_free(rom);
  SDL_free(wram);
}

static void test_overlay(void) {
  SmxOverlay overlay;
  Uint8 *picture = (Uint8 *)SDL_calloc(1, 256u * 224u * 4u);
  const Uint8 *drawn = NULL;
  Uint32 inside = 0;
  Uint32 outside = 0;
  Uint32 index = 0;

  expect("the overlay starts", smx_overlay_init(&overlay, 256, 224));
  expect("without messages the picture shows as it is",
         smx_overlay_draw(&overlay, picture, 0) == picture);
  smx_overlay_show(&overlay, "Fly mode on", 1000);
  drawn = smx_overlay_draw(&overlay, picture, 1000);
  expect("a message draws on a copy", drawn != picture && drawn != NULL);
  for (index = 0; drawn != NULL && index < 256u * 224u; index += 1) {
    if (drawn[index * 4u] == 0xFF) {
      if ((index / 256u) > 200u && (index % 256u) < 100u) {
        inside += 1;
      } else {
        outside += 1;
      }
    }
  }
  expect("the message is in the bottom left", inside > 20u && outside == 0);
  expect("the picture is left alone", picture[(212u * 256u + 10u) * 4u] == 0);
  smx_overlay_show(&overlay, "1", 1000);
  smx_overlay_show(&overlay, "2", 1000);
  smx_overlay_show(&overlay, "3", 1000);
  smx_overlay_show(&overlay, "4", 1000);
  expect("the oldest message makes room",
         overlay.count == SMX_OVERLAY_LINES && SDL_strcmp(overlay.text[0], "1") == 0);
  expect("messages go", smx_overlay_draw(&overlay, picture, 1000 + SMX_OVERLAY_MS) == picture);
  smx_overlay_quit(&overlay);
  SDL_free(picture);
}

typedef struct Console {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxApu apu;
  SmxClock clock;
  SmxCheats cheats;
} Console;

static bool frame(Console *console, Uint16 joy) {
  Sint16 samples[1024 * 2];
  bool ok = false;

  smx_cheat_apply(&console->cheats, console->bus.wram, joy);
  console->clock.joy = joy;
  ok = smx_clock_frame(&console->clock);
  smx_apu_drain(&console->apu, samples, 1024);
  return ok;
}

static void frames(Console *console, Uint32 count, Uint16 joy) {
  Uint32 index = 0;

  for (; index < count; index += 1) {
    frame(console, joy);
  }
}

/* A console in play at the landing site with `loadout`, Samus out of the ship and facing right. */
static Console *boot(const SmxCart *cart, const SmxSaveLoadout *loadout) {
  Console *console = (Console *)SDL_calloc(1, sizeof(Console));
  Uint32 index = 0;
  Uint32 quiet = 0;

  smx_bus_init(&console->bus, cart->bytes, cart->size, smx_cart_sram_bytes(cart->sram_size_code));
  smx_cpu_init(&console->cpu, &console->bus);
  smx_dma_init(&console->dma, &console->bus);
  smx_ppu_init(&console->ppu, &console->bus);
  smx_apu_init(&console->apu, &console->bus);
  smx_clock_init(&console->clock, &console->cpu, &console->dma);
  console->clock.ppu = &console->ppu;
  smx_cheat_init(&console->cheats, cart->bytes, cart->size);
  smx_cpu_reset(&console->cpu);
  if (smx_game.rom_hash == smx_translate_rom_hash(cart->bytes, cart->size)) {
    console->cpu.code = &smx_game.code;
  }
  for (index = 0; index < 2000u && quiet < 30u; index += 1) {
    frame(console, 0);
    quiet = smx_save_formatted(console->bus.sram) ? quiet + 1u : 0u;
  }
  smx_save_write(console->bus.sram, 0, loadout);
  for (index = 0; index < 6000u && !smx_cheat_playing(console->bus.wram); index += 1) {
    frame(console, (index % 32u) < 4u ? 0x1080 : 0);
  }
  /* The ship lands and opens before Samus takes input. */
  frames(console, 600, 0);
  frames(console, 10, 0x0100);
  frames(console, 30, 0);
  return console;
}

static void quit(Console *console) {
  smx_ppu_quit(&console->ppu);
  smx_bus_quit(&console->bus);
  SDL_free(console);
}

static void put_ram(Console *console, Uint32 at, Uint16 value) {
  put16(console->bus.wram, at, value);
}

static Uint16 ram(const Console *console, Uint32 at) {
  return get16(console->bus.wram, at);
}

/* Items given in play look as they do when a save with them loads: HUD and suit. */
static void test_rom_items(const SmxCart *cart) {
  SmxSaveLoadout full = smx_save_section("landing")->loadout;
  Console *saved = NULL;
  Console *given = NULL;
  Uint32 index = 0;
  bool hud = true;
  bool suit = true;
  bool beam = true;

  full.items = 0xF32F;
  full.beams = 0x100F;
  full.energy = 1499;
  full.reserve = 400;
  full.missiles = 230;
  full.supers = 50;
  full.power_bombs = 50;
  saved = boot(cart, &full);
  given = boot(cart, &smx_save_section("landing")->loadout);
  smx_cheat_toggle(&given->cheats, SMX_CHEAT_ITEMS);
  frames(saved, 60, 0);
  frames(given, 60, 0);
  for (index = 0x09A2u; index < 0x09D8u; index += 2) {
    if (index != 0x09D2u && ram(saved, index) != ram(given, index)) {
      SDL_Log("cheat: items: $%04x is %04x, a save has %04x", (unsigned)index,
              (unsigned)ram(given, index), (unsigned)ram(saved, index));
      g_failed = true;
    }
  }
  for (index = 0xC608u; index < 0xC6C8u; index += 2) {
    hud = hud && ram(saved, index) == ram(given, index);
  }
  for (index = 0xC0u; index < 0xD0u; index += 1) {
    suit = suit && saved->ppu.cgram[index] == given->ppu.cgram[index];
    beam = beam && saved->ppu.cgram[index + 0x20u] == given->ppu.cgram[index + 0x20u];
  }
  beam = beam &&
         SDL_memcmp(saved->ppu.vram + 0x6300u * 2u, given->ppu.vram + 0x6300u * 2u, 0x200u) == 0;
  expect("items: the HUD matches a save with every item", hud);
  expect("items: the suit's colors match a save with every item", suit);
  expect("items: the beam's tiles and colors match a save with every item", beam);
  quit(saved);
  quit(given);
}

/* Infinite life survives a hit bigger than full energy; without it, Samus dies. */
static void test_rom_life(const SmxCart *cart) {
  Console *console = boot(cart, &smx_save_section("landing")->loadout);
  Uint32 index = 0;
  bool died = false;

  console->cheats.on[SMX_CHEAT_LIFE] = true;
  put_ram(console, 0x0A50, 500);
  for (index = 0; index < 30u; index += 1) {
    frame(console, 0);
    died = died || ram(console, 0x0998) != 0x08;
  }
  expect("life: a 500 hit on 99 energy is survived", !died && ram(console, 0x09C2) > 0);
  console->cheats.on[SMX_CHEAT_LIFE] = false;
  put_ram(console, 0x0A50, 500);
  for (index = 0; index < 30u; index += 1) {
    frame(console, 0);
    died = died || ram(console, 0x0998) != 0x08;
  }
  expect("life: without it the same hit kills", died);
  quit(console);
}

/* Missiles count down when fired, unless ammo is infinite. */
static void test_rom_ammo(const SmxCart *cart) {
  SmxSaveLoadout loadout = smx_save_section("landing")->loadout;
  Console *console = NULL;

  loadout.missiles = 10;
  console = boot(cart, &loadout);
  /* Select picks missiles; X fires. */
  frames(console, 2, 0x2000);
  frames(console, 4, 0);
  frames(console, 2, 0x0040);
  frames(console, 30, 0);
  expect("ammo: a missile fired counts down", ram(console, 0x09C6) == 9);
  console->cheats.on[SMX_CHEAT_AMMO] = true;
  frames(console, 2, 0x0040);
  frames(console, 30, 0);
  expect("ammo: with it the count stays full", ram(console, 0x09C6) == 10);
  console->cheats.on[SMX_CHEAT_AMMO] = false;
  frames(console, 2, 0x0040);
  frames(console, 30, 0);
  expect("ammo: off again it counts down", ram(console, 0x09C6) == 9);
  quit(console);
}

/*
 * The hyper beam loads Charge+Wave+Plasma's tiles and colors as a save with
 * them does, fires the hyper shot with its rainbow, and puts back the beam
 * that was equipped when switched off.
 */
static void test_rom_hyper(const SmxCart *cart) {
  SmxSaveLoadout beams = smx_save_section("landing")->loadout;
  Console *saved = NULL;
  Console *console = NULL;
  Uint8 tiles[0x200];
  Uint16 colors[16];
  Uint32 index = 0;
  bool loaded = true;
  bool same = true;
  bool rainbow = false;

  beams.beams = 0x1009;
  saved = boot(cart, &beams);
  console = boot(cart, &smx_save_section("landing")->loadout);
  SDL_memcpy(tiles, console->ppu.vram + 0x6300u * 2u, sizeof(tiles));
  for (; index < 16u; index += 1) {
    colors[index] = ram(console, 0xC1C0u + index * 2u);
  }
  console->cheats.on[SMX_CHEAT_HYPER] = true;
  frames(console, 2, 0);
  expect("hyper: Charge, Wave, and Plasma are equipped", ram(console, 0x09A6) == 0x1009);
  loaded = SDL_memcmp(console->ppu.vram + 0x6300u * 2u, saved->ppu.vram + 0x6300u * 2u,
                      sizeof(tiles)) == 0;
  for (index = 9; index < 16u; index += 1) {
    loaded = loaded && ram(console, 0xC1C0u + index * 2u) == ram(saved, 0xC1C0u + index * 2u);
  }
  expect("hyper: the beam's tiles and colors are a save's with those beams", loaded);
  frames(console, 2, 0x0040);
  expect("hyper: the shot is the hyper beam's", (ram(console, 0x0C18) & 0x0010) != 0);
  for (index = 0; index < 20u; index += 1) {
    frame(console, 0);
    rainbow = rainbow || ram(console, 0xC1C4) != ram(saved, 0xC1C4);
  }
  expect("hyper: the beam's colors cycle", rainbow);
  console->cheats.on[SMX_CHEAT_HYPER] = false;
  frames(console, 30, 0);
  same = SDL_memcmp(console->ppu.vram + 0x6300u * 2u, tiles, sizeof(tiles)) == 0;
  for (index = 0; index < 16u; index += 1) {
    same = same && ram(console, 0xC1C0u + index * 2u) == colors[index];
  }
  expect("hyper: off brings back the beam, its tiles, and its colors",
         ram(console, 0x0A76) == 0 && ram(console, 0x09A6) == 0 && same);
  frames(console, 2, 0x0040);
  expect("hyper: off fires the plain beam", (ram(console, 0x0C18) & 0x0010) == 0);
  quit(saved);
  quit(console);
}

/* Fly mode rises while jump is held, falls when it is not, and stops at a ceiling. */
static void test_rom_fly(const SmxCart *cart) {
  Console *console = boot(cart, &smx_save_section("landing")->loadout);
  Uint16 start = ram(console, 0x0AFA);
  Uint16 low = 0;

  console->cheats.on[SMX_CHEAT_FLY] = true;
  frames(console, 60, 0x0080);
  expect("fly: rises past a jump", start - ram(console, 0x0AFA) > 100);
  frames(console, 120, 0);
  expect("fly: lands when jump is let go", ram(console, 0x0AFA) == start);
  /* From mid-fall, holding jump again rises. */
  frames(console, 60, 0x0080);
  frames(console, 15, 0);
  low = ram(console, 0x0AFA);
  frames(console, 20, 0x0080);
  expect("fly: rises again from a fall", ram(console, 0x0AFA) < low);
  frames(console, 1200, 0x0080);
  low = ram(console, 0x0AFA);
  frames(console, 60, 0x0080);
  SDL_Log("fly: top %u", (unsigned)low);
  expect("fly: stops at the top of the room", ram(console, 0x0AFA) == low && low < start);
  quit(console);
}

/* With a ROM: `smx_test_cheat ROM` plays each cheat in the game. */
int main(int argc, char **argv) {
  test_parse();
  test_ram();
  test_items_hyper();
  test_overlay();
  if (argc > 1) {
    SmxCart cart;
    smx_cart_init(&cart);
    expect("the ROM loads", smx_cart_load_path(&cart, argv[1]));
    if (cart.bytes != NULL) {
      test_rom_items(&cart);
      test_rom_life(&cart);
      test_rom_ammo(&cart);
      test_rom_hyper(&cart);
      test_rom_fly(&cart);
    }
    smx_cart_free(&cart);
  }
  return g_failed ? 1 : 0;
}
