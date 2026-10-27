#include "smx/apu.h"
#include "smx/cart.h"
#include "smx/clock.h"
#include "smx/game.h"
#include "smx/motion.h"
#include "smx/pace.h"
#include "smx/ppu.h"
#include "smx/present.h"
#include "smx/sram.h"
#include "smx/state.h"
#include "smx/translate.h"

#include <SDL3/SDL.h>

/** Process entry. `int` is required by the C standard. */
/** Audio queued ahead, in NTSC frames, before the shell waits. */
#define SMX_SHELL_QUEUE_FRAMES 4u
/** Most frames emulated per presented frame while the audio queue refills. */
#define SMX_SHELL_CATCH_UP_FRAMES 4u
/** Bytes of queued 32040 Hz stereo S16 audio in `frames` NTSC frames. */
#define SMX_SHELL_QUEUE_BYTES(frames) ((int)((frames) * 534u * 4u))
/** One average NTSC frame: 357366 master clocks at 21477270 Hz. */
#define SMX_SHELL_FRAME_NS 16639266u
/*
 * With display sync, sample frames of 32040 Hz audio: one game frame's worth,
 * the queue the pace aims for, below which the shell runs an extra game frame
 * rather than let it run dry, and above which it skips one.
 */
#define SMX_SHELL_FRAME_SAMPLES 534u
#define SMX_SHELL_TARGET_SAMPLES (3u * SMX_SHELL_FRAME_SAMPLES)
#define SMX_SHELL_LOW_SAMPLES (2u * SMX_SHELL_FRAME_SAMPLES)
#define SMX_SHELL_HIGH_SAMPLES (8u * SMX_SHELL_FRAME_SAMPLES)

/*
 * Keyboard to joypad, in the bit order of a 16-bit read of `$4218`:
 * B Y Select Start Up Down Left Right in the high byte, A X L R in the low.
 */
static const struct {
  SDL_Scancode scancode;
  Uint16 button;
} smx_keymap[] = {
    {SDL_SCANCODE_Z, 0x8000},      {SDL_SCANCODE_X, 0x4000},     {SDL_SCANCODE_RSHIFT, 0x2000},
    {SDL_SCANCODE_RETURN, 0x1000}, {SDL_SCANCODE_UP, 0x0800},    {SDL_SCANCODE_DOWN, 0x0400},
    {SDL_SCANCODE_LEFT, 0x0200},   {SDL_SCANCODE_RIGHT, 0x0100}, {SDL_SCANCODE_A, 0x0080},
    {SDL_SCANCODE_S, 0x0040},      {SDL_SCANCODE_Q, 0x0020},     {SDL_SCANCODE_W, 0x0010},
};

/* Xbox-style gamepad to joypad, by button position: south is SNES B, east is A. */
static const struct {
  SDL_GamepadButton button;
  Uint16 joy;
} smx_padmap[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, 0x8000},         {SDL_GAMEPAD_BUTTON_WEST, 0x4000},
    {SDL_GAMEPAD_BUTTON_BACK, 0x2000},          {SDL_GAMEPAD_BUTTON_START, 0x1000},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, 0x0800},       {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x0400},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x0200},     {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x0100},
    {SDL_GAMEPAD_BUTTON_EAST, 0x0080},          {SDL_GAMEPAD_BUTTON_NORTH, 0x0040},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x0020}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x0010},
};

/* Half of the stick or trigger range counts as pressed. */
#define SMX_SHELL_AXIS_PRESS 16384

/* Untranslated ROM code the shell ran, kept for the next translation. */
static void smx_shell_miss(void *user, Uint32 key) {
  smx_translate_add((SmxTranslator *)user, key);
}

/* Merge this session's misses into `<user data>/<rom name>.coverage`. */
static void smx_shell_save_coverage(SmxTranslator *misses, const char *rom_path,
                                    const SmxCart *cart) {
  char path[1024];
  char header[64];
  Uint32 found = misses->entries.count;
  size_t length = 0;

  if (found == 0 || !smx_sram_default_path(path, sizeof(path), rom_path)) {
    return;
  }
  length = SDL_strlen(path);
  if (length > 4) {
    path[length - 4] = '\0';
  }
  SDL_strlcat(path, ".coverage", sizeof(path));
  SDL_snprintf(header, sizeof(header), "# SuperMetroidX coverage rom %08x\n",
               (unsigned)smx_translate_rom_hash(cart->bytes, cart->size));
  smx_translate_load_keys(misses, path, header, NULL);
  if (smx_translate_save_keys(misses, path, header)) {
    SDL_Log("%u untranslated entry points ran; %u are recorded in %s. Rebuild with "
            "-DSMX_RECOMP_SEEDS=<that file> to translate them.",
            (unsigned)found, (unsigned)misses->entries.count, path);
  }
}

/* `<user data>/<rom name>.state`, the quick-save slot. */
static bool smx_shell_state_path(char *path, Uint32 size, const char *rom_path) {
  char sram_path[1024];

  if (!smx_sram_default_path(sram_path, sizeof(sram_path), rom_path)) {
    return false;
  }
  smx_state_path(path, size, sram_path);
  return true;
}

/* Frames between checks for SRAM the game has changed. */
#define SMX_SHELL_SRAM_FRAMES 60u

static Uint16 smx_gamepad_joy(SDL_Gamepad *pad) {
  Uint16 joy = 0;
  Uint32 index = 0;
  Sint16 x = 0;
  Sint16 y = 0;

  if (pad == NULL) {
    return 0;
  }
  for (; index < sizeof(smx_padmap) / sizeof(smx_padmap[0]); index += 1) {
    if (SDL_GetGamepadButton(pad, smx_padmap[index].button)) {
      joy = (Uint16)(joy | smx_padmap[index].joy);
    }
  }
  x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
  y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
  if (y < -SMX_SHELL_AXIS_PRESS) {
    joy = (Uint16)(joy | 0x0800);
  } else if (y > SMX_SHELL_AXIS_PRESS) {
    joy = (Uint16)(joy | 0x0400);
  }
  if (x < -SMX_SHELL_AXIS_PRESS) {
    joy = (Uint16)(joy | 0x0200);
  } else if (x > SMX_SHELL_AXIS_PRESS) {
    joy = (Uint16)(joy | 0x0100);
  }
  if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > SMX_SHELL_AXIS_PRESS) {
    joy = (Uint16)(joy | 0x0020);
  }
  if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > SMX_SHELL_AXIS_PRESS) {
    joy = (Uint16)(joy | 0x0010);
  }
  /* The SNES pad cannot press opposite directions at once. */
  if ((joy & 0x0C00) == 0x0C00) {
    joy = (Uint16)(joy & ~0x0C00);
  }
  if ((joy & 0x0300) == 0x0300) {
    joy = (Uint16)(joy & ~0x0300);
  }
  return joy;
}

/*
 * SuperMetroid.sfc in the working directory, else in the save folder, so the
 * game also starts from a file manager, whose working directory is elsewhere.
 */
static const char *smx_shell_find_rom(char *path, Uint32 size) {
  char *data = NULL;

  if (SDL_GetPathInfo("SuperMetroid.sfc", NULL)) {
    return "SuperMetroid.sfc";
  }
  data = SDL_GetPrefPath("SuperMetroidX", "SuperMetroidX");
  if (data == NULL) {
    return "SuperMetroid.sfc";
  }
  SDL_snprintf(path, size, "%sSuperMetroid.sfc", data);
  SDL_free(data);
  return SDL_GetPathInfo(path, NULL) ? path : "SuperMetroid.sfc";
}

/*
 * Escape quits at once in debug builds. Release builds ask first, since the
 * key is easy to hit by accident in play.
 */
static bool smx_shell_confirm_quit(SDL_Window *window) {
#ifdef NDEBUG
  static const SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Quit"},
  };
  SDL_MessageBoxData box;
  int chosen = 0;

  SDL_memset(&box, 0, sizeof(box));
  box.flags = SDL_MESSAGEBOX_WARNING;
  box.window = window;
  box.title = "SuperMetroidX";
  box.message = "Are you sure you want to quit? Progress since your last save will be lost.";
  box.numbuttons = (int)(sizeof(buttons) / sizeof(buttons[0]));
  box.buttons = buttons;
  if (!SDL_ShowMessageBox(&box, &chosen)) {
    SDL_Log("cannot ask before quitting: %s", SDL_GetError());
    return true;
  }
  return chosen == 1;
#else
  (void)window;
  return true;
#endif
}

static void smx_shell_usage(void) {
  SDL_Log("usage: SuperMetroidX [options] [ROM]");
  SDL_Log("  ROM defaults to $SMX_ROM, then the ROM built into the game, then");
  SDL_Log("  SuperMetroid.sfc in the working directory or the save folder.");
  SDL_Log("  --load-state        start from the quick-save (F9)");
  SDL_Log("  --sync display|audio  show each game frame for whole display refreshes,");
  SDL_Log("                      resampling audio to match, or pace by the audio queue");
  SDL_Log("                      at the console's exact rate; default display");
  SDL_Log("  --interpolate on|off  on displays faster than 60 Hz, draw in-between pictures");
  SDL_Log("                      that move scrolling and sprites smoothly; default on");
  smx_present_usage();
  SDL_Log("  --smoke             present a few frames and exit, for CI");
}

int main(int argc, char **argv) {
  bool smoke = false;
  bool load_state = false;
  bool display_sync = true;
  bool interpolate = true;
  const char *rom_path = NULL;
  SmxPresentOptions display;
  Sint32 index = 1;

  smx_present_defaults(&display);
  for (; index < argc; index += 1) {
    Sint32 used = smx_present_option(&display, (Sint32)argc - index, argv + index);
    if (used < 0) {
      SDL_Log("%s needs one of the values below", argv[index]);
      smx_shell_usage();
      return 1;
    } else if (used > 0) {
      index += used - 1;
    } else if (SDL_strcmp(argv[index], "--help") == 0 || SDL_strcmp(argv[index], "-h") == 0) {
      smx_shell_usage();
      return 0;
    } else if (SDL_strcmp(argv[index], "--smoke") == 0) {
      smoke = true;
    } else if (SDL_strcmp(argv[index], "--load-state") == 0) {
      load_state = true;
    } else if (SDL_strcmp(argv[index], "--sync") == 0) {
      if (index + 1 < argc && SDL_strcmp(argv[index + 1], "display") == 0) {
        display_sync = true;
      } else if (index + 1 < argc && SDL_strcmp(argv[index + 1], "audio") == 0) {
        display_sync = false;
      } else {
        SDL_Log("--sync needs display or audio");
        smx_shell_usage();
        return 1;
      }
      index += 1;
    } else if (SDL_strcmp(argv[index], "--interpolate") == 0) {
      if (index + 1 < argc && SDL_strcmp(argv[index + 1], "on") == 0) {
        interpolate = true;
      } else if (index + 1 < argc && SDL_strcmp(argv[index + 1], "off") == 0) {
        interpolate = false;
      } else {
        SDL_Log("--interpolate needs on or off");
        smx_shell_usage();
        return 1;
      }
      index += 1;
    } else if (argv[index][0] == '-') {
      SDL_Log("unknown option %s", argv[index]);
      smx_shell_usage();
      return 1;
    } else if (rom_path == NULL) {
      rom_path = argv[index];
    }
  }
  if (rom_path == NULL) {
    rom_path = SDL_getenv("SMX_ROM");
  }
  char rom_default[1024];
  bool embedded = false;
  if ((rom_path == NULL || rom_path[0] == '\0') && smx_game_rom != NULL) {
    /* Saves are still named after SuperMetroid.sfc. */
    embedded = true;
    rom_path = "SuperMetroid.sfc";
  } else if (rom_path == NULL || rom_path[0] == '\0') {
    rom_path = smx_shell_find_rom(rom_default, sizeof(rom_default));
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
  bool audio_ready = SDL_InitSubSystem(SDL_INIT_AUDIO);

  SmxPresenter presenter;
  if (!smx_present_open(&presenter, "SuperMetroidX", SMX_PPU_WIDTH, SMX_PPU_HEIGHT, &display)) {
    SDL_Quit();
    return 1;
  }

  SmxCart cart;
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxApu apu;
  SmxClock clock;
  SmxSram sram;
  SmxMachine console;
  Uint32 rom_hash = 0;
  SmxTranslator misses;
  SDL_Gamepad *pad = NULL;
  Uint32 sram_clock = 0;
  bool playing = false;
  bool machine = false;
  smx_cart_init(&cart);
  SDL_memset(&misses, 0, sizeof(misses));
  if (embedded ? smx_cart_load_bytes(&cart, smx_game_rom, smx_game_rom_size)
               : smx_cart_load_path(&cart, rom_path)) {
    smx_bus_init(&bus, cart.bytes, cart.size, smx_cart_sram_bytes(cart.sram_size_code));
    smx_cpu_init(&cpu, &bus);
    smx_dma_init(&dma, &bus);
    smx_ppu_init(&ppu, &bus);
    smx_apu_init(&apu, &bus);
    smx_clock_init(&clock, &cpu, &dma);
    clock.ppu = &ppu;
    smx_cpu_reset(&cpu);
    if (smx_game.code.count != 0 && smx_game.rom_size == cart.size &&
        smx_game.rom_hash == smx_translate_rom_hash(cart.bytes, cart.size)) {
      cpu.code = &smx_game.code;
      smx_translate_init(&misses, cart.bytes, cart.size);
      cpu.miss = smx_shell_miss;
      cpu.miss_user = &misses;
      SDL_Log("running %u translated entry points", (unsigned)smx_game.code.count);
    } else if (smx_game.code.count != 0) {
      SDL_Log("the translation is of a different ROM; interpreting");
    }
    machine = true;
    playing = true;
    console.bus = &bus;
    console.cpu = &cpu;
    console.dma = &dma;
    console.ppu = &ppu;
    console.apu = &apu;
    console.clock = &clock;
    rom_hash = smx_translate_rom_hash(cart.bytes, cart.size);
    SDL_Log("loaded %s%s", cart.title, embedded ? " (built in)" : "");
  } else if (!smoke) {
    char message[1400];
    char *data = SDL_GetPrefPath("SuperMetroidX", "SuperMetroidX");
    SDL_snprintf(message, sizeof(message),
                 "Cannot load the ROM %s.\n\nPass the ROM's path on the command line, or put it "
                 "at SuperMetroid.sfc in the working directory or in %s.",
                 rom_path, data != NULL ? data : "the SuperMetroidX save folder");
    SDL_free(data);
    SDL_Log("%s", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SuperMetroidX", message, presenter.window);
    smx_cart_free(&cart);
    smx_present_close(&presenter);
    SDL_Quit();
    return 1;
  }
  SDL_memset(&sram, 0, sizeof(sram));
  if (machine && !smoke) {
    char path[1024];
    if (smx_sram_default_path(path, sizeof(path), rom_path) && smx_sram_open(&sram, path, &bus)) {
      SDL_Log("saves in %s", path);
    }
    /* --load-state starts from the quick-save, as F9 would. */
    if (load_state && smx_shell_state_path(path, sizeof(path), rom_path)) {
      if (smx_state_load(&console, rom_hash, path)) {
        SDL_Log("loaded state from %s", path);
      } else {
        SDL_Log("no state for this ROM and build in %s", path);
      }
    }
  }

  /* In-between pictures need the display's refresh, so they come with display sync. */
  SmxMotion *motion = NULL;
  SmxMotionShift shift;
  bool interpolating = true;
  if (machine && interpolate && display_sync && !smoke) {
    motion = (SmxMotion *)SDL_calloc(1, sizeof(SmxMotion));
    if (motion != NULL && !smx_motion_init(motion, &ppu)) {
      SDL_free(motion);
      motion = NULL;
    }
  }

  SDL_AudioStream *audio = NULL;
  if (playing && audio_ready) {
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 32040;
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio == NULL) {
      SDL_Log("audio unavailable: %s", SDL_GetError());
    } else if (!SDL_ResumeAudioStreamDevice(audio)) {
      SDL_Log("audio resume failed: %s", SDL_GetError());
      SDL_DestroyAudioStream(audio);
      audio = NULL;
    }
  }

  SmxPace pace;
  const SDL_DisplayMode *mode =
      SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(presenter.window));
  smx_pace_init(&pace, SMX_SHELL_FRAME_NS, mode != NULL ? mode->refresh_rate : 0.0f);
  Uint32 pending = 1;

  bool running = true;
  Uint64 started = SDL_GetTicks();
  Uint64 next_frame = SDL_GetTicksNS();
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                 event.key.scancode == SDL_SCANCODE_ESCAPE) {
        running = !smx_shell_confirm_quit(presenter.window);
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                 (event.key.scancode == SDL_SCANCODE_F11 ||
                  (event.key.scancode == SDL_SCANCODE_RETURN &&
                   (event.key.mod & SDL_KMOD_ALT) != 0))) {
        /* F11 or Alt+Enter toggles fullscreen. */
        smx_present_toggle_fullscreen(&presenter);
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && playing &&
                 (event.key.scancode == SDL_SCANCODE_F5 || event.key.scancode == SDL_SCANCODE_F9)) {
        /* F5 saves the quick-save state and F9 loads it. SRAM is not part of it. */
        char path[1024];
        if (smx_shell_state_path(path, sizeof(path), rom_path)) {
          if (event.key.scancode == SDL_SCANCODE_F5) {
            if (smx_state_save(&console, rom_hash, path)) {
              SDL_Log("saved state to %s", path);
            }
          } else if (smx_state_load(&console, rom_hash, path)) {
            SDL_Log("loaded state from %s", path);
            if (motion != NULL) {
              smx_motion_reset(motion);
            }
          } else {
            SDL_Log("no state for this ROM and build in %s", path);
          }
        }
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                 event.key.scancode == SDL_SCANCODE_F3 && motion != NULL) {
        /* F3 switches motion interpolation, to compare. */
        interpolating = !interpolating;
        SDL_Log("motion interpolation %s", interpolating ? "on" : "off");
      } else if (event.type == SDL_EVENT_GAMEPAD_ADDED && pad == NULL) {
        pad = SDL_OpenGamepad(event.gdevice.which);
      } else if (event.type == SDL_EVENT_WINDOW_OCCLUDED || event.type == SDL_EVENT_WINDOW_HIDDEN ||
                 event.type == SDL_EVENT_WINDOW_MINIMIZED) {
        /* A hidden window's presents stop waiting for the display. */
        smx_pace_set_vsync(&pace, false, SDL_GetTicksNS());
      } else if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_WINDOW_SHOWN ||
                 event.type == SDL_EVENT_WINDOW_RESTORED) {
        smx_pace_set_vsync(&pace, true, SDL_GetTicksNS());
      } else if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
        mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(presenter.window));
        smx_pace_init(&pace, SMX_SHELL_FRAME_NS, mode != NULL ? mode->refresh_rate : 0.0f);
      } else if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                 event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        smx_present_event(&presenter, &event);
      } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && pad != NULL &&
                 SDL_GetGamepadID(pad) == event.gdevice.which) {
        SDL_CloseGamepad(pad);
        pad = NULL;
      }
    }

    /*
     * With display sync, the pace counts the display's refreshes and says how
     * many game frames to run before this present, and the audio is resampled
     * to the game's speed. With audio sync, the audio queue paces emulation at
     * the console's rate, or a timer does without audio.
     */
    Uint32 frames = 1;
    if (playing && !smoke && display_sync) {
      frames = pending;
      if (!pace.vsync) {
        Uint64 wait = smx_pace_wait_ns(&pace, SDL_GetTicksNS());
        if (wait != 0) {
          SDL_DelayNS(wait);
        }
      }
      if (audio != NULL) {
        Uint32 queued = (Uint32)(SDL_GetAudioStreamQueued(audio) / 4);
        while (queued + frames * SMX_SHELL_FRAME_SAMPLES < SMX_SHELL_LOW_SAMPLES &&
               frames < SMX_SHELL_CATCH_UP_FRAMES) {
          frames += 1;
        }
        if (queued > SMX_SHELL_HIGH_SAMPLES) {
          frames = 0;
        }
        SDL_SetAudioStreamFrequencyRatio(
            audio, smx_pace_audio_ratio(&pace, queued, SMX_SHELL_TARGET_SAMPLES));
      }
    } else if (playing && !smoke) {
      if (audio != NULL) {
        if (SDL_GetAudioStreamQueued(audio) > SMX_SHELL_QUEUE_BYTES(SMX_SHELL_QUEUE_FRAMES)) {
          SDL_DelayNS(1000000);
          continue;
        }
        frames = SMX_SHELL_CATCH_UP_FRAMES;
      } else {
        Uint64 now = SDL_GetTicksNS();
        if (now < next_frame) {
          SDL_DelayNS(next_frame - now);
        }
        next_frame += SMX_SHELL_FRAME_NS;
        if (SDL_GetTicksNS() > next_frame + 4u * SMX_SHELL_FRAME_NS) {
          next_frame = SDL_GetTicksNS();
        }
      }
    }

    Uint32 batch = 0;
    while (playing && batch < frames) {
      const bool *keys = SDL_GetKeyboardState(NULL);
      Uint16 joy = 0;
      Uint32 key = 0;
      for (; key < sizeof(smx_keymap) / sizeof(smx_keymap[0]); key += 1) {
        /* Return is Start, except as part of Alt+Enter. */
        bool alt_enter = smx_keymap[key].scancode == SDL_SCANCODE_RETURN &&
                         (SDL_GetModState() & SDL_KMOD_ALT) != 0;
        if (keys[smx_keymap[key].scancode] && !alt_enter) {
          joy = (Uint16)(joy | smx_keymap[key].button);
        }
      }
      joy = (Uint16)(joy | smx_gamepad_joy(pad));
      clock.joy = joy;
      if (!smx_clock_frame(&clock)) {
        SDL_Log("cpu stopped at %u:%u", (unsigned)cpu.pb, (unsigned)cpu.pc);
        playing = false;
      }
      if (motion != NULL) {
        smx_motion_objects(motion, bus.wram);
      }
      sram_clock += 1;
      if (sram_clock == SMX_SHELL_SRAM_FRAMES) {
        sram_clock = 0;
        smx_sram_flush(&sram, &bus);
      }
      if (audio != NULL) {
        /* About 533 frames of 32040 Hz stereo per NTSC frame. */
        Sint16 frames[1024 * 2];
        Uint32 count = smx_apu_drain(&apu, frames, 1024);
        SDL_PutAudioStreamData(audio, frames, (int)(count * 2u * sizeof(Sint16)));
      }
      batch += 1;
      /* With audio sync, emulate more than one frame per present only to refill the queue. */
      if (!display_sync && audio != NULL &&
          SDL_GetAudioStreamQueued(audio) >= SMX_SHELL_QUEUE_BYTES(SMX_SHELL_QUEUE_FRAMES - 1u)) {
        break;
      }
    }

    const Uint8 *between = NULL;
    if (motion != NULL && interpolating && playing) {
      between = smx_motion_draw(motion, smx_pace_alpha(&pace), &shift);
    }
    smx_present_frame(&presenter, between != NULL ? between : (playing ? ppu.picture : NULL),
                      between != NULL ? &shift : NULL);
    if (display_sync) {
      pending = smx_pace_presented(&pace, SDL_GetTicksNS());
    }

    if (smoke && (SDL_GetTicks() - started) >= 200) {
      running = false;
    }
  }

  if (audio != NULL) {
    SDL_DestroyAudioStream(audio);
  }
  if (pad != NULL) {
    SDL_CloseGamepad(pad);
  }
  if (machine) {
    smx_sram_flush(&sram, &bus);
    smx_sram_close(&sram);
    if (!smoke) {
      smx_shell_save_coverage(&misses, rom_path, &cart);
    }
    smx_translate_free(&misses);
    SDL_Log("instructions: %llu translated, %llu interpreted", (unsigned long long)cpu.translated,
            (unsigned long long)cpu.interpreted);
    if (motion != NULL) {
      smx_motion_quit(motion);
      SDL_free(motion);
    }
    smx_ppu_quit(&ppu);
    smx_bus_quit(&bus);
  }
  smx_cart_free(&cart);
  smx_present_close(&presenter);
  SDL_Quit();
  return 0;
}
