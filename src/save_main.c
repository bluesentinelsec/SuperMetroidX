#include "smx/cart.h"
#include "smx/save.h"
#include "smx/sram.h"
#include "smx/state.h"
#include "smx/translate.h"

#include <SDL3/SDL.h>

/*
 * smxsave writes a play-test save: SRAM with slot A at a chosen save station
 * and loadout, and a quick-save state of the first frames of play from it.
 * The game formats SRAM itself, then the tool presses A and Start through the
 * title, the file select, and the area map until the game is in play.
 */

/* Frames to wait for the game to format SRAM, and to reach play from the title. */
#define SMX_SAVE_FORMAT_FRAMES 2000u
#define SMX_SAVE_PLAY_FRAMES 6000u
/* Frames to let the room fade in once play starts. */
#define SMX_SAVE_SETTLE_FRAMES 90u
/* `$7E:0998`, the game's main state; 8 is play. */
#define SMX_SAVE_GAME_STATE 0x0998u
#define SMX_SAVE_STATE_PLAY 0x08u
#define SMX_SAVE_BUTTONS_A_START 0x1080u

typedef struct SmxSaveConsole {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxApu apu;
  SmxClock clock;
  SmxMachine machine;
} SmxSaveConsole;

static void smx_usage(void) {
  Uint32 count = 0;
  const SmxSaveSection *sections = smx_save_sections(&count);
  Uint32 index = 0;

  SDL_Log("usage: smxsave [options] ROM DATADIR");
  SDL_Log("  Writes DATADIR/<rom>.srm with slot A at a save station, and");
  SDL_Log("  DATADIR/<rom>.state, a quick-save of the first frames of play from it.");
  SDL_Log("  Run SuperMetroidX with SMX_DATA_DIR=DATADIR, then press F9 or pass --load-state.");
  SDL_Log("  --section NAME      start from a section (default landing):");
  for (; index < count; index += 1) {
    SDL_Log("      %-14s %s", sections[index].name, sections[index].description);
  }
  SDL_Log("  --area N            area: 0 Crateria, 1 Brinstar, 2 Norfair, 3 Wrecked Ship,");
  SDL_Log("                      4 Maridia, 5 Tourian, 6 Ceres");
  SDL_Log("  --station N         load station in the area: save stations from 0, others from 16");
  SDL_Log("  --items A,B,...     add items and beams: varia spring morph screw gravity hijump");
  SDL_Log("                      space bombs speed grapple xray wave ice spazer plasma charge all");
  SDL_Log("  --energy N  --reserve N  --missiles N  --supers N  --power-bombs N");
  SDL_Log("  --no-state          write only the SRAM");
}

static bool smx_parse_u16(const char *text, Uint16 *out) {
  char *end = NULL;
  long value = SDL_strtol(text, &end, 0);

  if (end == text || *end != '\0' || value < 0 || value > 0xFFFF) {
    return false;
  }
  *out = (Uint16)value;
  return true;
}

static bool smx_parse_items(const char *list, SmxSaveLoadout *loadout) {
  char name[32];
  const char *at = list;

  while (*at != '\0') {
    Uint32 length = 0;
    while (at[length] != '\0' && at[length] != ',') {
      length += 1;
    }
    if (length == 0 || length >= sizeof(name)) {
      return false;
    }
    SDL_memcpy(name, at, length);
    name[length] = '\0';
    if (!smx_save_add_item(name, &loadout->items, &loadout->beams)) {
      SDL_Log("smxsave: unknown item %s", name);
      return false;
    }
    at += length;
    if (*at == ',') {
      at += 1;
    }
  }
  return true;
}

static void smx_console_init(SmxSaveConsole *console, const SmxCart *cart) {
  smx_bus_init(&console->bus, cart->bytes, cart->size, smx_cart_sram_bytes(cart->sram_size_code));
  smx_cpu_init(&console->cpu, &console->bus);
  smx_dma_init(&console->dma, &console->bus);
  smx_ppu_init(&console->ppu, &console->bus);
  smx_apu_init(&console->apu, &console->bus);
  smx_clock_init(&console->clock, &console->cpu, &console->dma);
  console->clock.ppu = &console->ppu;
  smx_cpu_reset(&console->cpu);
  console->machine.bus = &console->bus;
  console->machine.cpu = &console->cpu;
  console->machine.dma = &console->dma;
  console->machine.ppu = &console->ppu;
  console->machine.apu = &console->apu;
  console->machine.clock = &console->clock;
}

/* Run until the game has formatted SRAM and finished writing it. */
static bool smx_format(SmxSaveConsole *console) {
  Uint32 frame = 0;
  Uint32 quiet = 0;

  for (; frame < SMX_SAVE_FORMAT_FRAMES && quiet < 30u; frame += 1) {
    if (!smx_clock_frame(&console->clock)) {
      return false;
    }
    quiet = smx_save_formatted(console->bus.sram) ? quiet + 1u : 0u;
  }
  return quiet == 30u;
}

/* Press A and Start in short pulses until the game is in play, then let the room settle. */
static bool smx_reach_play(SmxSaveConsole *console) {
  Uint32 frame = 0;

  for (; frame < SMX_SAVE_PLAY_FRAMES; frame += 1) {
    bool playing = console->bus.wram[SMX_SAVE_GAME_STATE] == SMX_SAVE_STATE_PLAY;
    if (playing) {
      break;
    }
    console->clock.joy = (frame % 32u) < 4u ? SMX_SAVE_BUTTONS_A_START : 0u;
    if (!smx_clock_frame(&console->clock)) {
      return false;
    }
  }
  if (frame == SMX_SAVE_PLAY_FRAMES) {
    return false;
  }
  console->clock.joy = 0;
  for (frame = 0; frame < SMX_SAVE_SETTLE_FRAMES; frame += 1) {
    if (!smx_clock_frame(&console->clock)) {
      return false;
    }
  }
  return true;
}

int main(int argc, char **argv) {
  const char *rom_path = NULL;
  const char *data_dir = NULL;
  const SmxSaveSection *section = smx_save_section("landing");
  SmxSaveLoadout loadout = section->loadout;
  SmxSaveLoadout overrides;
  bool have[9];
  bool want_state = true;
  Sint32 index = 1;
  SmxCart cart;
  SmxSaveConsole *console = NULL;
  char name[256];
  char sram_path[1024];
  char state_path[1024];
  Uint32 field = 0;
  int status = 1;

  SDL_memset(&overrides, 0, sizeof(overrides));
  SDL_memset(have, 0, sizeof(have));
  for (; index < argc; index += 1) {
    static const char *const fields[9] = {"--area",   "--station",  "--items",
                                          "--energy", "--reserve",  "--missiles",
                                          "--supers", "--power-bombs", NULL};
    Uint16 *targets[9];
    const char *arg = argv[index];
    bool matched = false;

    targets[0] = &overrides.area;
    targets[1] = &overrides.station;
    targets[2] = NULL;
    targets[3] = &overrides.energy;
    targets[4] = &overrides.reserve;
    targets[5] = &overrides.missiles;
    targets[6] = &overrides.supers;
    targets[7] = &overrides.power_bombs;
    if (SDL_strcmp(arg, "--list") == 0 || SDL_strcmp(arg, "--help") == 0) {
      smx_usage();
      return 0;
    }
    if (SDL_strcmp(arg, "--no-state") == 0) {
      want_state = false;
      continue;
    }
    if (SDL_strcmp(arg, "--section") == 0 && index + 1 < argc) {
      section = smx_save_section(argv[index + 1]);
      if (section == NULL) {
        SDL_Log("smxsave: unknown section %s", argv[index + 1]);
        return 1;
      }
      loadout = section->loadout;
      index += 1;
      continue;
    }
    for (field = 0; fields[field] != NULL; field += 1) {
      if (SDL_strcmp(arg, fields[field]) == 0 && index + 1 < argc) {
        matched = true;
        index += 1;
        if (field == 2) {
          if (!smx_parse_items(argv[index], &overrides)) {
            return 1;
          }
        } else if (!smx_parse_u16(argv[index], targets[field])) {
          SDL_Log("smxsave: %s needs a number", arg);
          return 1;
        }
        have[field] = true;
        break;
      }
    }
    if (matched) {
      continue;
    }
    if (arg[0] == '-') {
      smx_usage();
      return 1;
    }
    if (rom_path == NULL) {
      rom_path = arg;
    } else if (data_dir == NULL) {
      data_dir = arg;
    } else {
      smx_usage();
      return 1;
    }
  }
  if (rom_path == NULL || data_dir == NULL) {
    smx_usage();
    return 1;
  }
  /* Options apply over the section, whatever their order on the command line. */
  if (have[0]) {
    loadout.area = overrides.area;
  }
  if (have[1]) {
    loadout.station = overrides.station;
  }
  loadout.items = (Uint16)(loadout.items | overrides.items);
  loadout.beams = (Uint16)(loadout.beams | overrides.beams);
  if (have[3]) {
    loadout.energy = overrides.energy;
  }
  if (have[4]) {
    loadout.reserve = overrides.reserve;
  }
  if (have[5]) {
    loadout.missiles = overrides.missiles;
  }
  if (have[6]) {
    loadout.supers = overrides.supers;
  }
  if (have[7]) {
    loadout.power_bombs = overrides.power_bombs;
  }

  smx_cart_init(&cart);
  if (!smx_cart_load_path(&cart, rom_path)) {
    SDL_Log("smxsave: cannot load %s", rom_path);
    return 1;
  }
  console = (SmxSaveConsole *)SDL_calloc(1, sizeof(SmxSaveConsole));
  if (console == NULL) {
    smx_cart_free(&cart);
    return 1;
  }
  smx_console_init(console, &cart);
  smx_sram_file_name(name, sizeof(name), rom_path);
  SDL_CreateDirectory(data_dir);
  SDL_snprintf(sram_path, sizeof(sram_path), "%s/%s", data_dir, name);
  smx_state_path(state_path, sizeof(state_path), sram_path);

  if (console->bus.sram_size < SMX_SAVE_SRAM_SIZE || !smx_format(console)) {
    SDL_Log("smxsave: the game did not format SRAM; is %s Super Metroid?", rom_path);
  } else {
    smx_save_write(console->bus.sram, 0, &loadout);
    if (!SDL_SaveFile(sram_path, console->bus.sram, SMX_SAVE_SRAM_SIZE)) {
      SDL_Log("smxsave: cannot write %s: %s", sram_path, SDL_GetError());
    } else if (!want_state) {
      SDL_Log("smxsave: wrote %s", sram_path);
      status = 0;
    } else if (!smx_reach_play(console)) {
      SDL_Log("smxsave: the game did not reach play from area %u station %u",
              (unsigned)loadout.area, (unsigned)loadout.station);
    } else if (smx_state_save(&console->machine,
                              smx_translate_rom_hash(cart.bytes, cart.size), state_path)) {
      SDL_Log("smxsave: wrote %s and %s", sram_path, state_path);
      status = 0;
    }
  }
  smx_ppu_quit(&console->ppu);
  smx_bus_quit(&console->bus);
  SDL_free(console);
  smx_cart_free(&cart);
  return status;
}
