#include "smx/apu.h"
#include "smx/cart.h"
#include "smx/cheat.h"
#include "smx/clock.h"
#include "smx/game.h"
#include "smx/icon.h"
#include "smx/menu.h"
#include "smx/overlay.h"
#include "smx/ppu.h"
#include "smx/present.h"
#include "smx/settings.h"
#include "smx/skip.h"
#include "smx/sound.h"
#include "smx/sram.h"
#include "smx/state.h"
#include "smx/touch.h"
#include "smx/touchmenu.h"
#include "smx/translate.h"

#include <SDL3/SDL.h>
/* SDL's entry point: WinMain for windowed Windows builds, plain main elsewhere. */
#include <SDL3/SDL_main.h>

/*
 * Phones and tablets: touch controls, and a menu of the cheats alone in place
 * of the settings menu. Desktop builds have neither.
 */
#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_ANDROID)
#define SMX_SHELL_TOUCH 1
#endif

/** Process entry. `int` is required by the C standard. */
/** Audio queued ahead, in NTSC frames, before the shell waits. */
#define SMX_SHELL_QUEUE_FRAMES 4u
/** Most frames emulated per presented frame while the audio queue refills. */
#define SMX_SHELL_CATCH_UP_FRAMES 4u
/** 32040 Hz stereo frames of audio in `frames` NTSC frames. */
#define SMX_SHELL_QUEUE_SAMPLES(frames) ((frames) * 534u)
/** One average NTSC frame: 357366 master clocks at 21477270 Hz. */
#define SMX_SHELL_FRAME_NS 16639266u

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

/* Show a cheat switching, or the items it gave. */
static void smx_shell_cheat_shown(SmxOverlay *overlay, const SmxCheats *cheats, SmxCheatKind kind) {
  char text[48];

  if (kind == SMX_CHEAT_ITEMS) {
    SDL_snprintf(text, sizeof(text), "%s given", smx_cheat_title(kind));
  } else {
    SDL_snprintf(text, sizeof(text), "%s %s", smx_cheat_title(kind),
                 cheats->on[kind] ? "on" : "off");
  }
  SDL_Log("cheat: %s", text);
  smx_overlay_show(overlay, text, SDL_GetTicks());
}

/* Show turbo's new speed. */
static void smx_shell_turbo_shown(SmxOverlay *overlay, const SmxCheats *cheats) {
  char text[48];

  if (cheats->turbo == 0) {
    SDL_strlcpy(text, "Turbo off", sizeof(text));
  } else {
    SDL_snprintf(text, sizeof(text), "Turbo %s", smx_cheat_turbo_name(cheats->turbo));
  }
  SDL_Log("cheat: %s", text);
  smx_overlay_show(overlay, text, SDL_GetTicks());
}

/* What the settings menu changes, and where it saves. */
typedef struct SmxShellMenu {
  SmxMenu menu;
  SmxSettings *settings;
  SmxSettings *saved;
  const char *path;
  SmxPresenter *presenter;
  SmxSound *sound;
  /** The S-DSP, or NULL without a cartridge. */
  SmxDsp *dsp;
  SmxCheats *cheats;
  SmxOverlay *overlay;
  /** The left stick's last direction, so a push moves the menu once. */
  SmxMenuInput stick;
  bool stick_held;
#ifdef SMX_SHELL_TOUCH
  SmxTouch touch;
  SmxTouchMenu cheats_menu;
#endif
} SmxShellMenu;

/* Put the settings in effect: display, what is heard, and the resampler. */
static void smx_shell_apply(SmxShellMenu *shell) {
  smx_present_set_options(shell->presenter, &shell->settings->display);
  if (shell->dsp != NULL) {
    shell->dsp->heard = shell->settings->sound.interpolation;
  }
  smx_sound_set(shell->sound, &shell->settings->sound);
}

static bool smx_shell_menu_is_open(const SmxShellMenu *shell) {
#ifdef SMX_SHELL_TOUCH
  return shell->cheats_menu.open;
#else
  return shell->menu.open;
#endif
}

static void smx_shell_menu_open(SmxShellMenu *shell) {
#ifdef SMX_SHELL_TOUCH
  smx_touchmenu_open(&shell->cheats_menu, shell->cheats);
  smx_touch_release(&shell->touch);
#else
  smx_menu_open(&shell->menu);
#endif
  smx_overlay_clear(shell->overlay);
  smx_sound_pause(shell->sound, true);
}

static void smx_shell_menu_event(SmxShellMenu *shell, SmxMenuEvent event) {
  Uint32 kind = 0;

  if (event == SMX_MENU_SETTING) {
    SmxSettingId id = shell->menu.changed;
    smx_settings_set(shell->saved, id, smx_settings_get(shell->settings, id));
    smx_shell_apply(shell);
    if (shell->path != NULL) {
      smx_settings_save(shell->saved, shell->path);
    }
  } else if (event == SMX_MENU_RESET) {
    smx_settings_defaults(shell->saved);
    *shell->settings = *shell->saved;
    smx_shell_apply(shell);
    if (shell->path != NULL) {
      smx_settings_remove(shell->path);
    }
    for (; kind < SMX_CHEAT_COUNT; kind += 1) {
      shell->cheats->on[kind] = false;
    }
    shell->cheats->turbo = 0;
  } else if (event == SMX_MENU_CLOSED) {
    smx_sound_pause(shell->sound, false);
  }
}

/* Open the menu, or close it and play on. */
static void smx_shell_menu_toggle(SmxShellMenu *shell) {
  if (!smx_shell_menu_is_open(shell)) {
    smx_shell_menu_open(shell);
    return;
  }
#ifdef SMX_SHELL_TOUCH
  shell->cheats_menu.open = false;
#else
  shell->menu.open = false;
#endif
  smx_shell_menu_event(shell, SMX_MENU_CLOSED);
}

static void smx_shell_menu_input(SmxShellMenu *shell, SmxMenuInput input) {
#ifdef SMX_SHELL_TOUCH
  smx_shell_menu_event(shell, smx_touchmenu_input(&shell->cheats_menu, shell->cheats, input));
#else
  smx_shell_menu_event(
      shell, smx_menu_input(&shell->menu, shell->settings, shell->cheats, input, SDL_GetTicks()));
#endif
}

#ifdef SMX_SHELL_TOUCH
/* Lay the controls out for the window as it is now, clear of its unsafe edges. */
static void smx_shell_touch_layout(SmxShellMenu *shell) {
  SDL_Window *window = shell->presenter->window;
  float density = SDL_GetWindowPixelDensity(window);
  int width = 0;
  int height = 0;
  SDL_Rect safe;

  SDL_GetWindowSizeInPixels(window, &width, &height);
  if (!SDL_GetWindowSafeArea(window, &safe)) {
    safe.x = 0;
    safe.y = 0;
    safe.w = (int)((float)width / density);
    safe.h = (int)((float)height / density);
  }
  safe.x = (int)((float)safe.x * density);
  safe.y = (int)((float)safe.y * density);
  safe.w = (int)((float)safe.w * density);
  safe.h = (int)((float)safe.h * density);
  smx_touch_layout(&shell->touch, (Sint32)width, (Sint32)height, &safe);
}

/* A finger plays on the controls, or taps the open menu at its place on the picture. */
static void smx_shell_touch_event(SmxShellMenu *shell, const SDL_Event *event) {
  if (shell->cheats_menu.open) {
    Sint32 x = 0;
    Sint32 y = 0;
    Sint32 w = 0;
    Sint32 h = 0;
    if (event->type != SDL_EVENT_FINGER_DOWN) {
      return;
    }
    smx_present_fit(&shell->presenter->options, SMX_PPU_WIDTH, SMX_PPU_HEIGHT,
                    (Sint32)shell->touch.width, (Sint32)shell->touch.height, &x, &y, &w, &h);
    smx_shell_menu_event(
        shell,
        smx_touchmenu_tap(
            &shell->cheats_menu, shell->cheats,
            (event->tfinger.x * shell->touch.width - (float)x) * SMX_PPU_WIDTH / (float)w,
            (event->tfinger.y * shell->touch.height - (float)y) * SMX_PPU_HEIGHT / (float)h));
    return;
  }
  if (event->type == SDL_EVENT_FINGER_DOWN) {
    shell->touch.shown = true;
  }
  if (smx_touch_event(&shell->touch, event)) {
    smx_shell_menu_open(shell);
  }
}

/* The presenter's hook: the controls over the window, unless the menu is open. */
static void smx_shell_touch_draw(void *user, SDL_Renderer *renderer, Sint32 width, Sint32 height) {
  SmxShellMenu *shell = (SmxShellMenu *)user;

  (void)width;
  (void)height;
  if (!shell->cheats_menu.open) {
    smx_touch_draw(&shell->touch, renderer);
  }
}
#endif

/* The menu input a key gives, if any. */
static bool smx_shell_menu_key(SDL_Scancode scancode, SmxMenuInput *input) {
  switch (scancode) {
  case SDL_SCANCODE_UP:
    *input = SMX_MENU_UP;
    return true;
  case SDL_SCANCODE_DOWN:
    *input = SMX_MENU_DOWN;
    return true;
  case SDL_SCANCODE_LEFT:
    *input = SMX_MENU_LEFT;
    return true;
  case SDL_SCANCODE_RIGHT:
    *input = SMX_MENU_RIGHT;
    return true;
  case SDL_SCANCODE_RETURN:
  case SDL_SCANCODE_KP_ENTER:
  case SDL_SCANCODE_SPACE:
    *input = SMX_MENU_ACCEPT;
    return true;
  case SDL_SCANCODE_ESCAPE:
  case SDL_SCANCODE_BACKSPACE:
    *input = SMX_MENU_BACK;
    return true;
  default:
    return false;
  }
}

/* The menu input a gamepad button gives, if any: south accepts and east goes back. */
static bool smx_shell_menu_button(Uint8 button, SmxMenuInput *input) {
  switch (button) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    *input = SMX_MENU_UP;
    return true;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    *input = SMX_MENU_DOWN;
    return true;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    *input = SMX_MENU_LEFT;
    return true;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    *input = SMX_MENU_RIGHT;
    return true;
  case SDL_GAMEPAD_BUTTON_SOUTH:
    *input = SMX_MENU_ACCEPT;
    return true;
  case SDL_GAMEPAD_BUTTON_EAST:
  case SDL_GAMEPAD_BUTTON_START:
    *input = SMX_MENU_BACK;
    return true;
  default:
    return false;
  }
}

/* A push of the left stick past half way, once per push. */
static bool smx_shell_menu_stick(SmxShellMenu *shell, SDL_Gamepad *pad, SmxMenuInput *input) {
  Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
  Sint16 y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
  bool held = true;

  if (y < -SMX_SHELL_AXIS_PRESS) {
    *input = SMX_MENU_UP;
  } else if (y > SMX_SHELL_AXIS_PRESS) {
    *input = SMX_MENU_DOWN;
  } else if (x < -SMX_SHELL_AXIS_PRESS) {
    *input = SMX_MENU_LEFT;
  } else if (x > SMX_SHELL_AXIS_PRESS) {
    *input = SMX_MENU_RIGHT;
  } else {
    held = false;
  }
  if (!held) {
    shell->stick_held = false;
    return false;
  }
  if (shell->stick_held && shell->stick == *input) {
    return false;
  }
  shell->stick_held = true;
  shell->stick = *input;
  return true;
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
  SDL_Log("  --cheats LIST       turn on cheats, comma-separated, or all; keys 1-5 switch them:");
  SDL_Log("                      life (1), ammo (2), items (3), hyper (4), fly (5);");
  SDL_Log("                      6 steps turbo through 1.5x, 2x, 3x, and 4x, then off");
  smx_present_usage();
  smx_sound_usage();
  SDL_Log("  --reset-settings    forget the saved settings and start from the defaults");
  SDL_Log("  Settings saved from the menu (~ on the keyboard, R3 on a gamepad) apply first;");
  SDL_Log("  options given here override them for this run.");
  SDL_Log("  --version           print the version and exit");
  SDL_Log("  --smoke             present a few frames and exit, for CI");
}

/*
 * What SDL tells the system about the game. On Linux the identifier names the
 * window, so the desktop matches it to its desktop entry and icon.
 */
static void smx_shell_metadata(void) {
  SDL_SetAppMetadata("SuperMetroidX", SMX_VERSION, SMX_APP_ID);
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, SMX_VENDOR);
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, SMX_HOMEPAGE);
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");
}

int main(int argc, char **argv) {
  bool smoke = false;
  bool load_state = false;
  const char *rom_path = NULL;
  const char *cheat_list = NULL;
  /* `saved` is what the settings file holds; `settings`, what this run uses. */
  SmxSettings saved;
  SmxSettings settings;
  char settings_path[1024];
  bool have_settings_path = smx_settings_path(settings_path, sizeof(settings_path));
  Sint32 index = 1;

  smx_settings_defaults(&saved);
  for (; index < argc; index += 1) {
    if (SDL_strcmp(argv[index], "--reset-settings") == 0 && have_settings_path) {
      if (smx_settings_remove(settings_path)) {
        SDL_Log("settings reset to the defaults");
      } else {
        SDL_Log("cannot remove %s: %s", settings_path, SDL_GetError());
      }
    }
  }
  if (have_settings_path && smx_settings_load(&saved, settings_path)) {
    SDL_Log("settings from %s", settings_path);
  }
  settings = saved;
  for (index = 1; index < argc; index += 1) {
    Sint32 used = smx_present_option(&settings.display, (Sint32)argc - index, argv + index);
    if (used == 0) {
      used = smx_sound_option(&settings.sound, (Sint32)argc - index, argv + index);
    }
    if (used < 0) {
      SDL_Log("%s needs one of the values below", argv[index]);
      smx_shell_usage();
      return 1;
    } else if (used > 0) {
      index += used - 1;
    } else if (SDL_strcmp(argv[index], "--help") == 0 || SDL_strcmp(argv[index], "-h") == 0) {
      smx_shell_usage();
      return 0;
    } else if (SDL_strcmp(argv[index], "--version") == 0) {
      SDL_Log("SuperMetroidX %s", SMX_VERSION);
      return 0;
    } else if (SDL_strcmp(argv[index], "--smoke") == 0) {
      smoke = true;
    } else if (SDL_strcmp(argv[index], "--reset-settings") == 0) {
    } else if (SDL_strcmp(argv[index], "--load-state") == 0) {
      load_state = true;
    } else if (SDL_strcmp(argv[index], "--cheats") == 0) {
      SmxCheats check;
      smx_cheat_init(&check, NULL, 0);
      if (index + 1 >= argc || !smx_cheat_parse(&check, argv[index + 1])) {
        SDL_Log("--cheats needs a list of life, ammo, items, hyper, and fly, or all");
        smx_shell_usage();
        return 1;
      }
      index += 1;
      cheat_list = argv[index];
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

  smx_shell_metadata();
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
  bool audio_ready = SDL_InitSubSystem(SDL_INIT_AUDIO);

  SmxPresenter presenter;
  if (!smx_present_open(&presenter, "SuperMetroidX", SMX_PPU_WIDTH, SMX_PPU_HEIGHT,
                        &settings.display)) {
    SDL_Quit();
    return 1;
  }
#ifndef SDL_PLATFORM_APPLE
  /* macOS takes the icon from the app bundle. */
  {
    SDL_Surface *icon = smx_icon_surface();
    if (icon != NULL) {
      SDL_SetWindowIcon(presenter.window, icon);
      SDL_DestroySurface(icon);
    }
  }
#endif

  SmxCart cart;
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxApu apu;
  SmxClock clock;
  SmxSram sram;
  SmxMachine console;
  SmxCheats cheats;
  SmxOverlay overlay;
  SmxSkip skip;
  Uint32 rom_hash = 0;
  SmxTranslator misses;
  SDL_Gamepad *pad = NULL;
  Uint32 sram_clock = 0;
  bool playing = false;
  bool machine = false;
  smx_cart_init(&cart);
  smx_cheat_init(&cheats, NULL, 0);
  smx_skip_init(&skip);
  SDL_memset(&misses, 0, sizeof(misses));
  if (embedded ? smx_cart_load_bytes(&cart, smx_game_rom, smx_game_rom_size)
               : smx_cart_load_path(&cart, rom_path)) {
    smx_bus_init(&bus, cart.bytes, cart.size, smx_cart_sram_bytes(cart.sram_size_code));
    smx_cpu_init(&cpu, &bus);
    smx_dma_init(&dma, &bus);
    smx_ppu_init(&ppu, &bus);
    smx_apu_init(&apu, &bus);
    apu.dsp.heard = settings.sound.interpolation;
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
    smx_cheat_init(&cheats, cart.bytes, cart.size);
    if (cheat_list != NULL) {
      smx_cheat_parse(&cheats, cheat_list);
    }
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

  SmxSound sound;
  bool audio = false;
  SDL_memset(&sound, 0, sizeof(sound));
  if (playing && audio_ready) {
    audio = smx_sound_open(&sound, &settings.sound);
  }

  smx_overlay_init(&overlay, SMX_PPU_WIDTH, SMX_PPU_HEIGHT);
  for (index = 0; index < SMX_CHEAT_COUNT; index += 1) {
    if (cheats.on[index] && index != SMX_CHEAT_ITEMS) {
      smx_shell_cheat_shown(&overlay, &cheats, (SmxCheatKind)index);
    }
  }

  SmxShellMenu shell;
  SDL_memset(&shell, 0, sizeof(shell));
  smx_menu_init(&shell.menu);
  shell.settings = &settings;
  shell.saved = &saved;
  shell.path = have_settings_path ? settings_path : NULL;
  shell.presenter = &presenter;
  shell.sound = &sound;
  shell.dsp = machine ? &apu.dsp : NULL;
  shell.cheats = &cheats;
  shell.overlay = &overlay;
#ifdef SMX_SHELL_TOUCH
  smx_touch_init(&shell.touch);
  smx_touchmenu_init(&shell.cheats_menu);
  presenter.draw = smx_shell_touch_draw;
  presenter.draw_user = &shell;
#endif

  bool running = true;
  /* A phone or tablet has put the game in the background; it waits there. */
  bool background = false;
  /* Turbo's speed in percent, and the hundredths of a frame it owes without audio to pace it. */
  Uint32 speed = 100;
  Uint32 speed_owed = 0;
  Uint64 started = SDL_GetTicks();
  Uint64 next_frame = SDL_GetTicksNS();
  while (running) {
    SDL_Event event;
#ifdef SMX_SHELL_TOUCH
    smx_shell_touch_layout(&shell);
#endif
    while (SDL_PollEvent(&event)) {
      SmxMenuInput input = SMX_MENU_BACK;
#ifdef SMX_SHELL_TOUCH
      /* A gamepad in use hides the touch controls; a touch shows them again. */
      if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        shell.touch.shown = false;
      }
#endif
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && playing &&
                 event.key.scancode == SDL_SCANCODE_GRAVE) {
        /* ~ opens and closes the settings menu. */
        smx_shell_menu_toggle(&shell);
      } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && playing && pad != NULL &&
                 event.gbutton.which == SDL_GetGamepadID(pad) &&
                 event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
        /* So does pressing the right stick. */
        smx_shell_menu_toggle(&shell);
      } else if (smx_shell_menu_is_open(&shell) && event.type == SDL_EVENT_KEY_DOWN &&
                 smx_shell_menu_key(event.key.scancode, &input) &&
                 (!event.key.repeat || input < SMX_MENU_ACCEPT)) {
        smx_shell_menu_input(&shell, input);
      } else if (smx_shell_menu_is_open(&shell) && event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                 pad != NULL && event.gbutton.which == SDL_GetGamepadID(pad) &&
                 smx_shell_menu_button(event.gbutton.button, &input)) {
        smx_shell_menu_input(&shell, input);
      } else if (smx_shell_menu_is_open(&shell) && event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                 pad != NULL && event.gaxis.which == SDL_GetGamepadID(pad) &&
                 smx_shell_menu_stick(&shell, pad, &input)) {
        smx_shell_menu_input(&shell, input);
#ifdef SMX_SHELL_TOUCH
      } else if (playing &&
                 (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_MOTION ||
                  event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)) {
        smx_shell_touch_event(&shell, &event);
#endif
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
          } else {
            SDL_Log("no state for this ROM and build in %s", path);
          }
        }
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && playing &&
                 event.key.scancode >= SDL_SCANCODE_1 &&
                 event.key.scancode < SDL_SCANCODE_1 + SMX_CHEAT_COUNT) {
        /* Number keys switch the cheats; items are shown once given. */
        SmxCheatKind kind = (SmxCheatKind)(event.key.scancode - SDL_SCANCODE_1);
        smx_cheat_toggle(&cheats, kind);
        if (kind != SMX_CHEAT_ITEMS) {
          smx_shell_cheat_shown(&overlay, &cheats, kind);
        }
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && playing &&
                 event.key.scancode == SDL_SCANCODE_1 + SMX_CHEAT_COUNT) {
        /* The key after the cheats' steps through turbo's speeds. */
        smx_cheat_turbo_next(&cheats);
        smx_shell_turbo_shown(&overlay, &cheats);
      } else if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
                 event.type == SDL_EVENT_TERMINATING) {
        /* iOS can end a suspended app without warning, so the save goes out first. */
        if (machine) {
          smx_sram_flush(&sram, &bus);
        }
        smx_sound_pause(&sound, true);
        background = true;
      } else if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        background = false;
        if (!smx_shell_menu_is_open(&shell)) {
          smx_sound_pause(&sound, false);
        }
        next_frame = SDL_GetTicksNS();
      } else if (event.type == SDL_EVENT_GAMEPAD_ADDED && pad == NULL) {
        pad = SDL_OpenGamepad(event.gdevice.which);
      } else if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                 event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        smx_present_event(&presenter, &event);
      } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && pad != NULL &&
                 SDL_GetGamepadID(pad) == event.gdevice.which) {
        SDL_CloseGamepad(pad);
        pad = NULL;
      }
    }

    if (background) {
      SDL_Delay(16);
      continue;
    }

    /* The game waits while the menu is open. */
    if (smx_shell_menu_is_open(&shell)) {
      Uint64 now = SDL_GetTicks();
      SDL_Renderer *renderer = smx_overlay_begin(&overlay, ppu.picture);
#ifdef SMX_SHELL_TOUCH
      smx_touchmenu_draw(&shell.cheats_menu, &cheats, renderer, SMX_PPU_WIDTH, SMX_PPU_HEIGHT);
#else
      smx_shell_menu_event(&shell, smx_menu_tick(&shell.menu, &settings, now));
      smx_menu_draw(&shell.menu, &settings, &cheats, renderer, SMX_PPU_WIDTH, SMX_PPU_HEIGHT, now);
#endif
      smx_present_frame(&presenter,
                        renderer != NULL ? smx_overlay_finish(&overlay, now) : ppu.picture);
      SDL_Delay(8);
      next_frame = SDL_GetTicksNS();
      continue;
    }

    /* Turbo plays the queue faster, so the queue's limits grow with it to last as long. */
    if (smx_cheat_turbo_percent(cheats.turbo) != speed) {
      speed = smx_cheat_turbo_percent(cheats.turbo);
      smx_sound_set_speed(&sound, speed);
    }
    Uint32 queue_most = SMX_SHELL_QUEUE_SAMPLES(SMX_SHELL_QUEUE_FRAMES) * speed / 100u;
    Uint32 queue_enough = SMX_SHELL_QUEUE_SAMPLES(SMX_SHELL_QUEUE_FRAMES - 1u) * speed / 100u;
    Uint32 catch_up = (SMX_SHELL_CATCH_UP_FRAMES * speed + 99u) / 100u;
    Uint32 due = 1;

    /*
     * Pace emulation by the audio queue when there is one, else by the
     * average NTSC frame of 357366 master clocks, with turbo's extra frames
     * run in batches.
     */
    if (playing && !smoke) {
      if (audio) {
        if (smx_sound_queued(&sound) > queue_most) {
          SDL_DelayNS(1000000);
          continue;
        }
      } else {
        Uint64 now = SDL_GetTicksNS();
        if (now < next_frame) {
          SDL_DelayNS(next_frame - now);
        }
        next_frame += SMX_SHELL_FRAME_NS;
        if (SDL_GetTicksNS() > next_frame + 4u * SMX_SHELL_FRAME_NS) {
          next_frame = SDL_GetTicksNS();
        }
        speed_owed += speed;
        due = speed_owed / 100u;
        speed_owed %= 100u;
      }
    }

    /*
     * Presenting can wait for the display, whose refresh is not the SNES
     * frame rate. Emulate more than one frame per present while the audio
     * queue is short so it never runs dry.
     */
    Uint32 batch = 0;
    while (playing) {
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
#ifdef SMX_SHELL_TOUCH
      joy = (Uint16)(joy | smx_touch_joy(&shell.touch));
#endif
      clock.joy = joy;
      if (smx_cheat_apply(&cheats, bus.wram, joy)) {
        smx_shell_cheat_shown(&overlay, &cheats, SMX_CHEAT_ITEMS);
      }
      switch (smx_skip_apply(&skip, bus.wram, joy)) {
      case SMX_SKIP_INTRO:
        smx_overlay_show(&overlay, "Hold any button to skip", SDL_GetTicks());
        break;
      case SMX_SKIP_SKIPPED:
        SDL_Log("intro skipped");
        break;
      default:
        break;
      }
      if (!smx_clock_frame(&clock)) {
        SDL_Log("cpu stopped at %u:%u", (unsigned)cpu.pb, (unsigned)cpu.pc);
        playing = false;
      }
      sram_clock += 1;
      if (sram_clock == SMX_SHELL_SRAM_FRAMES) {
        sram_clock = 0;
        smx_sram_flush(&sram, &bus);
      }
      if (audio) {
        /* About 533 frames of 32040 Hz stereo per NTSC frame. */
        Sint16 frames[1024 * 2];
        Uint32 count = smx_apu_drain(&apu, frames, 1024);
        smx_sound_put(&sound, frames, count);
      }
      batch += 1;
      if (smoke || (!audio && batch >= due) || batch >= catch_up ||
          (audio && smx_sound_queued(&sound) >= queue_enough)) {
        break;
      }
    }
    /* The hold's bar takes the place of the hint to hold. */
    if (smx_skip_shown(&skip) != 0 && overlay.hold == 0) {
      smx_overlay_clear(&overlay);
    }
    smx_overlay_hold(&overlay, "Hold to skip", smx_skip_shown(&skip), SMX_SKIP_HOLD_FRAMES);

    smx_present_frame(&presenter,
                      playing ? smx_overlay_draw(&overlay, ppu.picture, SDL_GetTicks()) : NULL);

    if (smoke && (SDL_GetTicks() - started) >= 200) {
      running = false;
    }
  }

  smx_sound_close(&sound);
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
    smx_ppu_quit(&ppu);
    smx_bus_quit(&bus);
  }
  smx_overlay_quit(&overlay);
  smx_cart_free(&cart);
  smx_present_close(&presenter);
  SDL_Quit();
  return 0;
}
