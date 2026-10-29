#include "smx/menu.h"

/* Layout on the picture, in pixels: SDL's debug font is 8x8. */
#define SMX_MENU_LINE_H 10.0f
#define SMX_MENU_TEXT_X 12.0f
#define SMX_MENU_VALUE_X 132.0f
#define SMX_MENU_TEXT_Y 10.0f

static const char *const smx_menu_labels[SMX_MENU_ROWS] = {
    "Window",
    "Scale",
    "Aspect",
    "Scanlines",
    "Sound filter",
    "Resampler",
    "Infinite life",
    "Infinite ammo",
    "Hyper beam",
    "Fly mode",
    "Turbo",
    "Give all items",
    "Reset to defaults",
    "Resume",
};

/* The names the menu shows for each setting's values, in their order. */
static const char *const smx_menu_values[SMX_SETTING_COUNT][3] = {
    {"Windowed", "Borderless", "Exclusive"},
    {"Integer", "Fill", NULL},
    {"Square", "TV 8:7", NULL},
    {"Off", "On", NULL},
    {"Console", "Cubic", "Sinc"},
    {"Sinc", "SDL", NULL},
};

/* The sections, by the row they start at. */
static const struct {
  Uint32 row;
  const char *title;
} smx_menu_sections[] = {
    {SMX_SETTING_WINDOW_MODE, "DISPLAY"},
    {SMX_SETTING_AUDIO_INTERPOLATION, "SOUND"},
    {SMX_MENU_ROW_LIFE, "CHEATS"},
};

void smx_menu_init(SmxMenu *menu) {
  SDL_memset(menu, 0, sizeof(*menu));
}

void smx_menu_open(SmxMenu *menu) {
  menu->open = true;
  menu->ask = SMX_MENU_ASK_NONE;
}

/* The cheat a row switches, or SMX_CHEAT_COUNT. */
static SmxCheatKind smx_menu_cheat(Uint32 row) {
  switch (row) {
  case SMX_MENU_ROW_LIFE:
    return SMX_CHEAT_LIFE;
  case SMX_MENU_ROW_AMMO:
    return SMX_CHEAT_AMMO;
  case SMX_MENU_ROW_HYPER:
    return SMX_CHEAT_HYPER;
  case SMX_MENU_ROW_FLY:
    return SMX_CHEAT_FLY;
  case SMX_MENU_ROW_ITEMS:
    return SMX_CHEAT_ITEMS;
  default:
    return SMX_CHEAT_COUNT;
  }
}

/* An answer to the question the menu is asking. */
static SmxMenuEvent smx_menu_answer(SmxMenu *menu, SmxSettings *settings, bool yes) {
  SmxMenuAsk ask = menu->ask;

  menu->ask = SMX_MENU_ASK_NONE;
  if (ask == SMX_MENU_ASK_RESET) {
    return yes ? SMX_MENU_RESET : SMX_MENU_NOTHING;
  }
  if (!yes) {
    smx_settings_set(settings, SMX_SETTING_WINDOW_MODE, menu->window_before);
    menu->changed = SMX_SETTING_WINDOW_MODE;
    return SMX_MENU_SETTING;
  }
  return SMX_MENU_NOTHING;
}

/* Step a setting by `step` (1 or the count less 1), wrapping. */
static SmxMenuEvent smx_menu_step(SmxMenu *menu, SmxSettings *settings, SmxSettingId id,
                                  Uint32 step, Uint64 now_ms) {
  Uint32 choices = smx_settings_choices(id);
  Uint32 before = smx_settings_get(settings, id);

  smx_settings_set(settings, id, (before + step) % choices);
  menu->changed = id;
  if (id == SMX_SETTING_WINDOW_MODE) {
    menu->ask = SMX_MENU_ASK_KEEP_DISPLAY;
    menu->ask_yes = true;
    menu->ask_until_ms = now_ms + SMX_MENU_KEEP_SECONDS * 1000u;
    menu->window_before = before;
  }
  return SMX_MENU_SETTING;
}

SmxMenuEvent smx_menu_input(SmxMenu *menu, SmxSettings *settings, SmxCheats *cheats,
                            SmxMenuInput input, Uint64 now_ms) {
  SmxCheatKind cheat = smx_menu_cheat(menu->row);

  if (!menu->open) {
    return SMX_MENU_NOTHING;
  }
  if (menu->ask != SMX_MENU_ASK_NONE) {
    if (input == SMX_MENU_LEFT || input == SMX_MENU_RIGHT) {
      menu->ask_yes = !menu->ask_yes;
    } else if (input == SMX_MENU_ACCEPT) {
      return smx_menu_answer(menu, settings, menu->ask_yes);
    } else if (input == SMX_MENU_BACK) {
      return smx_menu_answer(menu, settings, false);
    }
    return SMX_MENU_NOTHING;
  }
  switch (input) {
  case SMX_MENU_UP:
    menu->row = (menu->row + SMX_MENU_ROWS - 1u) % SMX_MENU_ROWS;
    return SMX_MENU_NOTHING;
  case SMX_MENU_DOWN:
    menu->row = (menu->row + 1u) % SMX_MENU_ROWS;
    return SMX_MENU_NOTHING;
  case SMX_MENU_BACK:
    menu->open = false;
    return SMX_MENU_CLOSED;
  default:
    break;
  }
  if (menu->row < SMX_SETTING_COUNT) {
    Uint32 choices = smx_settings_choices((SmxSettingId)menu->row);
    return smx_menu_step(menu, settings, (SmxSettingId)menu->row,
                         input == SMX_MENU_LEFT ? choices - 1u : 1u, now_ms);
  }
  if (menu->row == SMX_MENU_ROW_TURBO) {
    Uint32 step = input == SMX_MENU_LEFT ? SMX_CHEAT_TURBO_SPEEDS - 1u : 1u;
    cheats->turbo = (cheats->turbo + step) % SMX_CHEAT_TURBO_SPEEDS;
    return SMX_MENU_TURBO;
  }
  if (cheat != SMX_CHEAT_COUNT) {
    /* Items are an action: left and right do not give them. */
    if (cheat == SMX_CHEAT_ITEMS && input != SMX_MENU_ACCEPT) {
      return SMX_MENU_NOTHING;
    }
    smx_cheat_toggle(cheats, cheat);
    menu->cheat = cheat;
    return SMX_MENU_CHEAT;
  }
  if (input != SMX_MENU_ACCEPT) {
    return SMX_MENU_NOTHING;
  }
  if (menu->row == SMX_MENU_ROW_RESET) {
    menu->ask = SMX_MENU_ASK_RESET;
    menu->ask_yes = false;
    return SMX_MENU_NOTHING;
  }
  menu->open = false;
  return SMX_MENU_CLOSED;
}

SmxMenuEvent smx_menu_tick(SmxMenu *menu, SmxSettings *settings, Uint64 now_ms) {
  if (menu->ask == SMX_MENU_ASK_KEEP_DISPLAY && now_ms >= menu->ask_until_ms) {
    return smx_menu_answer(menu, settings, false);
  }
  return SMX_MENU_NOTHING;
}

static void smx_menu_text(SDL_Renderer *renderer, float x, float y, const char *text, Uint8 r,
                          Uint8 g, Uint8 b) {
  SDL_SetRenderDrawColor(renderer, r, g, b, 255);
  SDL_RenderDebugText(renderer, x, y, text);
}

static void smx_menu_box(SDL_Renderer *renderer, float x, float y, float w, float h, Uint8 alpha) {
  SDL_FRect box;

  box.x = x;
  box.y = y;
  box.w = w;
  box.h = h;
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
  SDL_RenderFillRect(renderer, &box);
  SDL_SetRenderDrawColor(renderer, 120, 140, 200, 255);
  SDL_RenderRect(renderer, &box);
}

/* The value a row shows, or NULL for an action. */
static const char *smx_menu_value(Uint32 row, const SmxSettings *settings,
                                  const SmxCheats *cheats) {
  SmxCheatKind cheat = smx_menu_cheat(row);

  if (row < SMX_SETTING_COUNT) {
    return smx_menu_values[row][smx_settings_get(settings, (SmxSettingId)row)];
  }
  if (row == SMX_MENU_ROW_TURBO) {
    return smx_cheat_turbo_name(cheats->turbo);
  }
  if (cheat != SMX_CHEAT_COUNT && cheat != SMX_CHEAT_ITEMS) {
    return cheats->on[cheat] ? "On" : "Off";
  }
  if (cheat == SMX_CHEAT_ITEMS && cheats->on[cheat]) {
    return "Next frame";
  }
  return NULL;
}

/* The question, centered over the menu. */
static void smx_menu_draw_ask(const SmxMenu *menu, SDL_Renderer *renderer, float width,
                              float height, Uint64 now_ms) {
  const char *question = "Reset every setting?";
  const char *yes = "Reset";
  const char *no = "Cancel";
  char detail[40];
  float x = 0.0f;
  float y = height / 2.0f - 24.0f;

  detail[0] = '\0';
  if (menu->ask == SMX_MENU_ASK_KEEP_DISPLAY) {
    Uint64 left = menu->ask_until_ms > now_ms ? (menu->ask_until_ms - now_ms + 999u) / 1000u : 0u;
    question = "Keep this display?";
    yes = "Keep";
    no = "Revert";
    SDL_snprintf(detail, sizeof(detail), "Reverting in %u", (unsigned)left);
  } else {
    SDL_strlcpy(detail, "and turn cheats off", sizeof(detail));
  }
  smx_menu_box(renderer, 24.0f, y, width - 48.0f, 50.0f, 240);
  x = (width - (float)SDL_strlen(question) * 8.0f) / 2.0f;
  smx_menu_text(renderer, x, y + 6.0f, question, 255, 255, 255);
  x = (width - (float)SDL_strlen(detail) * 8.0f) / 2.0f;
  smx_menu_text(renderer, x, y + 18.0f, detail, 170, 170, 190);
  if (menu->ask_yes) {
    smx_menu_text(renderer, 56.0f, y + 34.0f, ">", 255, 220, 80);
  } else {
    smx_menu_text(renderer, 136.0f, y + 34.0f, ">", 255, 220, 80);
  }
  smx_menu_text(renderer, 66.0f, y + 34.0f, yes, menu->ask_yes ? 255 : 170,
                menu->ask_yes ? 220 : 170, menu->ask_yes ? 80 : 190);
  smx_menu_text(renderer, 146.0f, y + 34.0f, no, menu->ask_yes ? 170 : 255,
                menu->ask_yes ? 170 : 220, menu->ask_yes ? 190 : 80);
}

void smx_menu_draw(const SmxMenu *menu, const SmxSettings *settings, const SmxCheats *cheats,
                   SDL_Renderer *renderer, Uint32 width, Uint32 height, Uint64 now_ms) {
  float y = SMX_MENU_TEXT_Y;
  Uint32 row = 0;
  Uint32 section = 0;

  if (!menu->open || renderer == NULL) {
    return;
  }
  smx_menu_box(renderer, 4.0f, 4.0f, (float)width - 8.0f, (float)height - 8.0f, 236);
  smx_menu_text(renderer, ((float)width - 8.0f * 8.0f) / 2.0f, y, "SETTINGS", 255, 255, 255);
  y += SMX_MENU_LINE_H + 4.0f;
  for (; row < SMX_MENU_ROWS; row += 1) {
    const char *value = smx_menu_value(row, settings, cheats);
    bool selected = row == menu->row;
    if (section < sizeof(smx_menu_sections) / sizeof(smx_menu_sections[0]) &&
        smx_menu_sections[section].row == row) {
      smx_menu_text(renderer, SMX_MENU_TEXT_X - 4.0f, y, smx_menu_sections[section].title, 120, 140,
                    200);
      y += SMX_MENU_LINE_H;
      section += 1;
    }
    if (row == SMX_MENU_ROW_RESET) {
      y += 4.0f;
    }
    if (selected) {
      SDL_FRect bar;
      bar.x = 8.0f;
      bar.y = y - 1.0f;
      bar.w = (float)width - 16.0f;
      bar.h = SMX_MENU_LINE_H;
      SDL_SetRenderDrawColor(renderer, 50, 60, 110, 255);
      SDL_RenderFillRect(renderer, &bar);
    }
    smx_menu_text(renderer, SMX_MENU_TEXT_X, y, smx_menu_labels[row], 255, selected ? 220 : 255,
                  selected ? 80 : 255);
    if (value != NULL) {
      char shown[24];
      SDL_snprintf(shown, sizeof(shown), selected ? "<%s>" : " %s", value);
      smx_menu_text(renderer, SMX_MENU_VALUE_X, y, shown, selected ? 255 : 190,
                    selected ? 220 : 200, selected ? 80 : 220);
    }
    y += SMX_MENU_LINE_H;
  }
  smx_menu_text(renderer, SMX_MENU_TEXT_X, (float)height - 18.0f, "Enter/A choose  Esc/B back", 130,
                130, 150);
  if (menu->ask != SMX_MENU_ASK_NONE) {
    smx_menu_draw_ask(menu, renderer, (float)width, (float)height, now_ms);
  }
}
