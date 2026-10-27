#include "smx/apu.h"
#include "smx/cart.h"
#include "smx/clock.h"
#include "smx/game.h"
#include "smx/ppu.h"
#include "smx/translate.h"

#include <SDL3/SDL.h>

#include "game_hashes.h"

/*
 * Runs the ROM from power-on and checks the picture and the audio against
 * hashes recorded from a run that matched Mesen 2 frame for frame. It needs
 * the ROM, so CMake registers it only when one is given:
 *
 *   smx_test_game_run idle ROM          the attract mode, translated
 *   smx_test_game_run new-game ROM      a new game into Ceres, translated
 *   smx_test_game_run interpreted ROM   the attract mode's start, interpreted
 *   smx_test_game_run --record ROM      print the arrays in tests/game_hashes.h
 *
 * The translated run covers the attract mode, and a new game through the
 * intro into Ceres. The interpreter must give the same hashes over the first
 * checkpoints. With SMX_EMBED_ROM, the built-in ROM must equal the file.
 */

typedef struct SmxGameRun {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxApu apu;
  SmxClock clock;
} SmxGameRun;

/* The new-game script smxrecomp traces: Start, the first slot, Start through the intro. */
static Uint16 smx_game_buttons(Uint32 frame) {
  static const Uint32 opening[][2] = {{1300, 0x1000}, {1500, 0x0080}, {1700, 0x0080},
                                      {1900, 0x0080}, {2100, 0x0080}, {2300, 0x1000}};
  Uint32 index = 0;

  for (; index < sizeof(opening) / sizeof(opening[0]); index += 1) {
    if (frame >= opening[index][0] && frame < opening[index][0] + 8u) {
      return (Uint16)opening[index][1];
    }
  }
  if (frame >= 4600u && (frame - 4600u) % 200u < 6u) {
    return 0x1000;
  }
  return 0;
}

static Uint32 smx_game_fnv(Uint32 hash, const Uint8 *bytes, Uint32 size) {
  Uint32 index = 0;

  for (; index < size; index += 1) {
    hash = (hash ^ bytes[index]) * 16777619u;
  }
  return hash;
}

static bool smx_game_start(SmxGameRun *run, const SmxCart *cart, bool translated) {
  SDL_memset(run, 0, sizeof(*run));
  smx_bus_init(&run->bus, cart->bytes, cart->size, smx_cart_sram_bytes(cart->sram_size_code));
  smx_cpu_init(&run->cpu, &run->bus);
  smx_dma_init(&run->dma, &run->bus);
  smx_ppu_init(&run->ppu, &run->bus);
  smx_apu_init(&run->apu, &run->bus);
  smx_clock_init(&run->clock, &run->cpu, &run->dma);
  run->clock.ppu = &run->ppu;
  smx_cpu_reset(&run->cpu);
  if (translated) {
    if (smx_game.code.count == 0 ||
        smx_game.rom_hash != smx_translate_rom_hash(cart->bytes, cart->size)) {
      SDL_Log("game: the translation is not of this ROM");
      return false;
    }
    run->cpu.code = &smx_game.code;
  }
  return true;
}

static void smx_game_stop(SmxGameRun *run) {
  smx_ppu_quit(&run->ppu);
  smx_bus_quit(&run->bus);
}

/*
 * Run `checkpoints` checkpoints of `script` (0 idle, 1 new game) and write a
 * picture hash and a running audio hash at each.
 */
static bool smx_game_run(const SmxCart *cart, bool translated, Uint32 script, Uint32 checkpoints,
                         Uint32 *pictures, Uint32 *sounds) {
  SmxGameRun *run = (SmxGameRun *)SDL_calloc(1, sizeof(SmxGameRun));
  Uint32 sound = 2166136261u;
  Uint32 frame = 1;
  bool ok = run != NULL && smx_game_start(run, cart, translated);

  for (; ok && frame <= checkpoints * SMX_GAME_CHECKPOINT_FRAMES; frame += 1) {
    Sint16 samples[1024 * 2];
    Uint32 count = 0;
    run->clock.joy = script == 1u ? smx_game_buttons(frame) : 0u;
    if (!smx_clock_frame(&run->clock)) {
      SDL_Log("game: the CPU stopped at frame %u", (unsigned)frame);
      ok = false;
      break;
    }
    count = smx_apu_drain(&run->apu, samples, 1024);
    sound = smx_game_fnv(sound, (const Uint8 *)samples, count * 4u);
    if (frame % SMX_GAME_CHECKPOINT_FRAMES == 0u) {
      Uint32 at = frame / SMX_GAME_CHECKPOINT_FRAMES - 1u;
      pictures[at] = smx_game_fnv(2166136261u, run->ppu.picture,
                                  (Uint32)(SMX_PPU_WIDTH * SMX_PPU_HEIGHT * 4));
      sounds[at] = sound;
    }
  }
  if (run != NULL) {
    smx_game_stop(run);
  }
  SDL_free(run);
  return ok;
}

static bool smx_game_check(const char *name, Uint32 checkpoints, const Uint32 *pictures,
                           const Uint32 *sounds, const Uint32 want[][2]) {
  Uint32 index = 0;

  for (; index < checkpoints; index += 1) {
    if (pictures[index] != want[index][0] || sounds[index] != want[index][1]) {
      SDL_Log("game: %s frame %u: picture %08x sound %08x, want %08x %08x", name,
              (unsigned)((index + 1u) * SMX_GAME_CHECKPOINT_FRAMES), (unsigned)pictures[index],
              (unsigned)sounds[index], (unsigned)want[index][0], (unsigned)want[index][1]);
      return false;
    }
  }
  return true;
}

static void smx_game_print(const char *name, Uint32 checkpoints, const Uint32 *pictures,
                           const Uint32 *sounds) {
  Uint32 index = 0;

  SDL_Log("static const Uint32 %s[%u][2] = {", name, (unsigned)checkpoints);
  for (; index < checkpoints; index += 1) {
    SDL_Log("    {0x%08xu, 0x%08xu},", (unsigned)pictures[index], (unsigned)sounds[index]);
  }
  SDL_Log("};");
}

int main(int argc, char **argv) {
  static Uint32 pictures[SMX_GAME_CHECKPOINTS];
  static Uint32 sounds[SMX_GAME_CHECKPOINTS];
  const char *what = argc == 3 ? argv[1] : "";
  bool record = SDL_strcmp(what, "--record") == 0;
  SmxCart cart;
  bool ok = true;

  if (argc != 3 || (!record && SDL_strcmp(what, "idle") != 0 &&
                    SDL_strcmp(what, "new-game") != 0 && SDL_strcmp(what, "interpreted") != 0)) {
    SDL_Log("usage: smx_test_game_run idle|new-game|interpreted|--record ROM");
    return 1;
  }
  smx_cart_init(&cart);
  if (!smx_cart_load_path(&cart, argv[2])) {
    SDL_Log("game: cannot load %s", argv[2]);
    return 1;
  }
  if (smx_game_rom != NULL) {
    SmxCart built_in;
    smx_cart_init(&built_in);
    if (!smx_cart_load_bytes(&built_in, smx_game_rom, smx_game_rom_size) ||
        built_in.size != cart.size || SDL_memcmp(built_in.bytes, cart.bytes, cart.size) != 0) {
      SDL_Log("game: the built-in ROM differs from %s", argv[2]);
      ok = false;
    }
    smx_cart_free(&built_in);
  }

  if (record) {
    ok = smx_game_run(&cart, true, 0, SMX_GAME_CHECKPOINTS, pictures, sounds) && ok;
    smx_game_print("smx_game_idle", SMX_GAME_CHECKPOINTS, pictures, sounds);
    ok = smx_game_run(&cart, true, 1, SMX_GAME_CHECKPOINTS, pictures, sounds) && ok;
    smx_game_print("smx_game_new_game", SMX_GAME_CHECKPOINTS, pictures, sounds);
  } else if (SDL_strcmp(what, "idle") == 0) {
    ok = smx_game_run(&cart, true, 0, SMX_GAME_CHECKPOINTS, pictures, sounds) &&
         smx_game_check("idle", SMX_GAME_CHECKPOINTS, pictures, sounds, smx_game_idle) && ok;
  } else if (SDL_strcmp(what, "new-game") == 0) {
    ok = smx_game_run(&cart, true, 1, SMX_GAME_CHECKPOINTS, pictures, sounds) &&
         smx_game_check("new game", SMX_GAME_CHECKPOINTS, pictures, sounds, smx_game_new_game) &&
         ok;
  } else {
    ok = smx_game_run(&cart, false, 0, SMX_GAME_INTERPRETED, pictures, sounds) &&
         smx_game_check("interpreted", SMX_GAME_INTERPRETED, pictures, sounds, smx_game_idle) &&
         ok;
  }
  smx_cart_free(&cart);
  return ok ? 0 : 1;
}
