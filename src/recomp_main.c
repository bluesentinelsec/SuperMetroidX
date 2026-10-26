#include "smx/cart.h"
#include "smx/translate.h"

#include <SDL3/SDL.h>

/*
 * The trace runs the ROM twice. Left alone, the title screen plays its
 * attract demos, which visit most areas. Then a new game starts: wait out
 * the intro, pick the first save slot, and press Start every 200 frames
 * through the opening.
 */
#define SMX_TRACE_IDLE_FRAMES 24000u
#define SMX_TRACE_PLAY_FRAMES 12000u
/* Bump when the trace plan changes, so cached traces are redone. */
#define SMX_TRACE_PLAN 1u

static SmxTranslatePress smx_play_presses[64];

static Uint32 smx_play_script(void) {
  static const SmxTranslatePress opening[] = {
      {1300, 8, 0x1000}, {1500, 8, 0x0080}, {1700, 8, 0x0080},
      {1900, 8, 0x0080}, {2100, 8, 0x0080}, {2300, 8, 0x1000},
  };
  Uint32 count = 0;
  Uint32 at = 4600;

  for (; count < sizeof(opening) / sizeof(opening[0]); count += 1) {
    smx_play_presses[count] = opening[count];
  }
  for (; at <= SMX_TRACE_PLAY_FRAMES && count < 64u; at += 200u, count += 1) {
    smx_play_presses[count].at = at;
    smx_play_presses[count].frames = 6;
    smx_play_presses[count].buttons = 0x1000;
  }
  return count;
}

static void smx_usage(void) {
  SDL_Log("usage: smxrecomp [--no-trace] [--seeds FILE] ROM OUTDIR");
  SDL_Log("  Translates ROM to C files in OUTDIR.");
  SDL_Log("  --no-trace    skip running the ROM in the interpreter to find entry points");
  SDL_Log("  --seeds FILE  add entry keys, one hexadecimal SMX_CPU_KEY per line");
  SDL_Log("  The trace is cached in OUTDIR/trace.txt for the same ROM.");
}

/* Trace both runs, or reuse the trace cached for this ROM and plan. */
static bool smx_trace(SmxTranslator *translator, const SmxCart *cart, const char *out_dir) {
  char path[1024];
  char header[96];
  Uint32 presses = smx_play_script();

  SDL_snprintf(path, sizeof(path), "%s/trace.txt", out_dir);
  SDL_snprintf(header, sizeof(header), "# smxrecomp trace rom %08x plan %u\n",
               (unsigned)smx_translate_rom_hash(cart->bytes, cart->size), (unsigned)SMX_TRACE_PLAN);
  if (smx_translate_load_keys(translator, path, header, NULL)) {
    SDL_Log("smxrecomp: reusing %s", path);
    return true;
  }
  SDL_Log("smxrecomp: tracing %u idle frames", (unsigned)SMX_TRACE_IDLE_FRAMES);
  if (!smx_translate_trace(translator, cart, SMX_TRACE_IDLE_FRAMES, NULL, 0)) {
    return false;
  }
  SDL_Log("smxrecomp: tracing %u frames of a new game", (unsigned)SMX_TRACE_PLAY_FRAMES);
  if (!smx_translate_trace(translator, cart, SMX_TRACE_PLAY_FRAMES, smx_play_presses, presses)) {
    return false;
  }
  SDL_CreateDirectory(out_dir);
  return smx_translate_save_keys(translator, path, header);
}

int main(int argc, char **argv) {
  const char *rom_path = NULL;
  const char *out_dir = NULL;
  const char *seeds = NULL;
  bool trace = true;
  Sint32 index = 1;
  SmxCart cart;
  SmxTranslator translator;
  bool ok = true;

  for (; index < argc; index += 1) {
    if (SDL_strcmp(argv[index], "--no-trace") == 0) {
      trace = false;
    } else if (SDL_strcmp(argv[index], "--seeds") == 0 && index + 1 < argc) {
      index += 1;
      seeds = argv[index];
    } else if (rom_path == NULL) {
      rom_path = argv[index];
    } else if (out_dir == NULL) {
      out_dir = argv[index];
    } else {
      smx_usage();
      return 2;
    }
  }
  if (rom_path == NULL || out_dir == NULL) {
    smx_usage();
    return 2;
  }

  smx_cart_init(&cart);
  if (!smx_cart_load_path(&cart, rom_path)) {
    SDL_Log("smxrecomp: cannot load %s", rom_path);
    return 1;
  }
  smx_translate_init(&translator, cart.bytes, cart.size);
  smx_translate_add_vectors(&translator);
  if (seeds != NULL) {
    Uint32 added = 0;
    ok = smx_translate_load_keys(&translator, seeds, NULL, &added);
    if (ok) {
      SDL_Log("smxrecomp: %u new entries from %s", (unsigned)added, seeds);
    } else {
      SDL_Log("smxrecomp: cannot read %s", seeds);
    }
  }
  if (ok && trace) {
    ok = smx_trace(&translator, &cart, out_dir);
  }
  if (ok) {
    smx_translate_explore(&translator);
    SDL_Log("smxrecomp: %u entry points, %u instructions translated",
            (unsigned)translator.entries.count, (unsigned)translator.visited.count);
    ok = smx_translate_write(&translator, out_dir);
  }
  smx_translate_free(&translator);
  smx_cart_free(&cart);
  return ok ? 0 : 1;
}
