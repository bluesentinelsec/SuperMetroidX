#include "smx/touchmenu.h"

#include "smx/touch.h"

/* Layout on the picture, in pixels: rows tall enough for a fingertip at any scale a phone shows. */
#define SMX_TOUCHMENU_LEFT 20.0f
#define SMX_TOUCHMENU_RIGHT 236.0f
#define SMX_TOUCHMENU_TOP 12.0f
#define SMX_TOUCHMENU_BOTTOM 212.0f
#define SMX_TOUCHMENU_ROWS_TOP 42.0f
#define SMX_TOUCHMENU_ROW_H 26.0f
#define SMX_TOUCHMENU_LABEL_X 34.0f
#define SMX_TOUCHMENU_SWITCH_X 210.0f
#define SMX_TOUCHMENU_SWITCH_W 30.0f
#define SMX_TOUCHMENU_SPEED_X 168.0f
#define SMX_TOUCHMENU_SPEED_W 40.0f
#define SMX_TOUCHMENU_GIVE_X 200.0f
#define SMX_TOUCHMENU_GIVE_W 56.0f
#define SMX_TOUCHMENU_DONE_X 206.0f
#define SMX_TOUCHMENU_DONE_Y 25.0f
#define SMX_TOUCHMENU_DONE_W 44.0f
#define SMX_TOUCHMENU_BUTTON_H 16.0f
/* Taps this far outside a button still hit it. */
#define SMX_TOUCHMENU_SLOP 4.0f

/* Turbo's switch turns on at 2x until a tap chooses another speed. */
#define SMX_TOUCHMENU_TURBO_DEFAULT 2u

static const char *const smx_touchmenu_labels[SMX_TOUCHMENU_ROWS] = {
    "Infinite life", "Infinite ammo", "Hyper beam", "Fly mode", "Turbo", "All items",
};

static const SmxCheatKind smx_touchmenu_cheats[SMX_TOUCHMENU_ROWS] = {
    SMX_CHEAT_LIFE, SMX_CHEAT_AMMO,  SMX_CHEAT_HYPER,
    SMX_CHEAT_FLY,  SMX_CHEAT_COUNT, SMX_CHEAT_ITEMS,
};

void smx_touchmenu_init(SmxTouchMenu *menu) {
  SDL_memset(menu, 0, sizeof(*menu));
  menu->turbo_speed = SMX_TOUCHMENU_TURBO_DEFAULT;
}

void smx_touchmenu_open(SmxTouchMenu *menu, const SmxCheats *cheats) {
  menu->open = true;
  menu->row = 0;
  menu->row_shown = false;
  if (cheats->turbo != 0) {
    menu->turbo_speed = cheats->turbo;
  }
}

static float smx_touchmenu_row_y(Uint32 row) {
  return SMX_TOUCHMENU_ROWS_TOP + SMX_TOUCHMENU_ROW_H * ((float)row + 0.5f);
}

static bool smx_touchmenu_hit(float x, float y, float cx, float cy, float w, float h) {
  return SDL_fabsf(x - cx) <= w * 0.5f + SMX_TOUCHMENU_SLOP &&
         SDL_fabsf(y - cy) <= h * 0.5f + SMX_TOUCHMENU_SLOP;
}

/* Flip the row's switch, or give the items. */
static SmxMenuEvent smx_touchmenu_flip(SmxTouchMenu *menu, SmxCheats *cheats, Uint32 row) {
  SmxCheatKind cheat = smx_touchmenu_cheats[row];

  if (row == SMX_TOUCHMENU_TURBO) {
    cheats->turbo = cheats->turbo != 0 ? 0 : menu->turbo_speed;
    return SMX_MENU_TURBO;
  }
  if (cheat == SMX_CHEAT_ITEMS && cheats->on[cheat]) {
    return SMX_MENU_NOTHING;
  }
  smx_cheat_toggle(cheats, cheat);
  menu->cheat = cheat;
  return SMX_MENU_CHEAT;
}

/* Turbo's next speed, which applies at once if turbo is on. */
static SmxMenuEvent smx_touchmenu_speed(SmxTouchMenu *menu, SmxCheats *cheats, bool up) {
  Uint32 speeds = SMX_CHEAT_TURBO_SPEEDS - 1u;

  menu->turbo_speed = (menu->turbo_speed - 1u + (up ? 1u : speeds - 1u)) % speeds + 1u;
  if (cheats->turbo == 0) {
    return SMX_MENU_NOTHING;
  }
  cheats->turbo = menu->turbo_speed;
  return SMX_MENU_TURBO;
}

SmxMenuEvent smx_touchmenu_tap(SmxTouchMenu *menu, SmxCheats *cheats, float x, float y) {
  Uint32 row = 0;

  if (!menu->open) {
    return SMX_MENU_NOTHING;
  }
  menu->row_shown = false;
  if (x < SMX_TOUCHMENU_LEFT || x > SMX_TOUCHMENU_RIGHT || y < SMX_TOUCHMENU_TOP ||
      y > SMX_TOUCHMENU_BOTTOM ||
      smx_touchmenu_hit(x, y, SMX_TOUCHMENU_DONE_X, SMX_TOUCHMENU_DONE_Y, SMX_TOUCHMENU_DONE_W,
                        SMX_TOUCHMENU_BUTTON_H)) {
    menu->open = false;
    return SMX_MENU_CLOSED;
  }
  if (y < SMX_TOUCHMENU_ROWS_TOP) {
    return SMX_MENU_NOTHING;
  }
  row = (Uint32)((y - SMX_TOUCHMENU_ROWS_TOP) / SMX_TOUCHMENU_ROW_H);
  if (row >= SMX_TOUCHMENU_ROWS) {
    return SMX_MENU_NOTHING;
  }
  menu->row = row;
  if (row == SMX_TOUCHMENU_TURBO &&
      smx_touchmenu_hit(x, y, SMX_TOUCHMENU_SPEED_X, smx_touchmenu_row_y(row),
                        SMX_TOUCHMENU_SPEED_W, SMX_TOUCHMENU_BUTTON_H)) {
    return smx_touchmenu_speed(menu, cheats, true);
  }
  return smx_touchmenu_flip(menu, cheats, row);
}

SmxMenuEvent smx_touchmenu_input(SmxTouchMenu *menu, SmxCheats *cheats, SmxMenuInput input) {
  if (!menu->open) {
    return SMX_MENU_NOTHING;
  }
  menu->row_shown = true;
  switch (input) {
  case SMX_MENU_UP:
    menu->row = (menu->row + SMX_TOUCHMENU_ROWS - 1u) % SMX_TOUCHMENU_ROWS;
    return SMX_MENU_NOTHING;
  case SMX_MENU_DOWN:
    menu->row = (menu->row + 1u) % SMX_TOUCHMENU_ROWS;
    return SMX_MENU_NOTHING;
  case SMX_MENU_LEFT:
  case SMX_MENU_RIGHT:
    if (menu->row == SMX_TOUCHMENU_TURBO) {
      return smx_touchmenu_speed(menu, cheats, input == SMX_MENU_RIGHT);
    }
    return SMX_MENU_NOTHING;
  case SMX_MENU_ACCEPT:
    return smx_touchmenu_flip(menu, cheats, menu->row);
  default:
    menu->open = false;
    return SMX_MENU_CLOSED;
  }
}

static SDL_FColor smx_touchmenu_rgba(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
  SDL_FColor color;

  color.r = (float)r / 255.0f;
  color.g = (float)g / 255.0f;
  color.b = (float)b / 255.0f;
  color.a = (float)a / 255.0f;
  return color;
}

/* `text` centered on `x`, `y`. */
static void smx_touchmenu_text(SDL_Renderer *renderer, float x, float y, const char *text, Uint8 r,
                               Uint8 g, Uint8 b) {
  float width = (float)SDL_strlen(text) * (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

  SDL_SetRenderDrawColor(renderer, r, g, b, 255);
  SDL_RenderDebugText(renderer, x - width * 0.5f,
                      y - (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 0.5f, text);
}

/* A switch in the style of the system's: green with the knob right when on. */
static void smx_touchmenu_switch(SDL_Renderer *renderer, float y, bool on) {
  float knob = SMX_TOUCHMENU_SWITCH_W * 0.5f - SMX_TOUCHMENU_BUTTON_H * 0.5f;

  smx_touch_fill_capsule(renderer, SMX_TOUCHMENU_SWITCH_X, y, SMX_TOUCHMENU_SWITCH_W,
                         SMX_TOUCHMENU_BUTTON_H,
                         on ? smx_touchmenu_rgba(52, 199, 89, 255)
                            : smx_touchmenu_rgba(84, 84, 96, 255));
  smx_touch_fill_circle(renderer, SMX_TOUCHMENU_SWITCH_X + (on ? knob : -knob), y,
                        SMX_TOUCHMENU_BUTTON_H * 0.5f - 2.0f,
                        smx_touchmenu_rgba(255, 255, 255, 255));
}

static void smx_touchmenu_button(SDL_Renderer *renderer, float x, float y, float w,
                                 const char *text, bool lit) {
  smx_touch_fill_capsule(renderer, x, y, w, SMX_TOUCHMENU_BUTTON_H,
                         lit ? smx_touchmenu_rgba(60, 90, 200, 255)
                             : smx_touchmenu_rgba(70, 70, 84, 255));
  smx_touchmenu_text(renderer, x, y, text, 255, 255, 255);
}

void smx_touchmenu_draw(const SmxTouchMenu *menu, const SmxCheats *cheats, SDL_Renderer *renderer,
                        Uint32 width, Uint32 height) {
  SDL_FRect box;
  Uint32 row = 0;

  if (!menu->open || renderer == NULL) {
    return;
  }
  (void)width;
  (void)height;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  box.x = SMX_TOUCHMENU_LEFT;
  box.y = SMX_TOUCHMENU_TOP;
  box.w = SMX_TOUCHMENU_RIGHT - SMX_TOUCHMENU_LEFT;
  box.h = SMX_TOUCHMENU_BOTTOM - SMX_TOUCHMENU_TOP;
  SDL_SetRenderDrawColor(renderer, 12, 14, 24, 236);
  SDL_RenderFillRect(renderer, &box);
  SDL_SetRenderDrawColor(renderer, 120, 140, 200, 255);
  SDL_RenderRect(renderer, &box);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_RenderDebugText(renderer, SMX_TOUCHMENU_LABEL_X, SMX_TOUCHMENU_DONE_Y - 4.0f, "CHEATS");
  smx_touchmenu_button(renderer, SMX_TOUCHMENU_DONE_X, SMX_TOUCHMENU_DONE_Y, SMX_TOUCHMENU_DONE_W,
                       "Done", true);
  for (; row < SMX_TOUCHMENU_ROWS; row += 1) {
    float y = smx_touchmenu_row_y(row);
    SmxCheatKind cheat = smx_touchmenu_cheats[row];
    if (menu->row_shown && menu->row == row) {
      box.x = SMX_TOUCHMENU_LEFT + 4.0f;
      box.y = y - SMX_TOUCHMENU_ROW_H * 0.5f + 1.0f;
      box.w = SMX_TOUCHMENU_RIGHT - SMX_TOUCHMENU_LEFT - 8.0f;
      box.h = SMX_TOUCHMENU_ROW_H - 2.0f;
      SDL_SetRenderDrawColor(renderer, 50, 60, 110, 255);
      SDL_RenderFillRect(renderer, &box);
    }
    if (row > 0) {
      SDL_SetRenderDrawColor(renderer, 44, 48, 72, 255);
      SDL_RenderLine(renderer, SMX_TOUCHMENU_LEFT + 10.0f, y - SMX_TOUCHMENU_ROW_H * 0.5f,
                     SMX_TOUCHMENU_RIGHT - 10.0f, y - SMX_TOUCHMENU_ROW_H * 0.5f);
    }
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(renderer, SMX_TOUCHMENU_LABEL_X, y - 4.0f, smx_touchmenu_labels[row]);
    if (row == SMX_TOUCHMENU_TURBO) {
      smx_touchmenu_button(renderer, SMX_TOUCHMENU_SPEED_X, y, SMX_TOUCHMENU_SPEED_W,
                           smx_cheat_turbo_name(menu->turbo_speed), false);
      smx_touchmenu_switch(renderer, y, cheats->turbo != 0);
    } else if (cheat == SMX_CHEAT_ITEMS) {
      smx_touchmenu_button(renderer, SMX_TOUCHMENU_GIVE_X, y, SMX_TOUCHMENU_GIVE_W,
                           cheats->on[cheat] ? "Queued" : "Give", !cheats->on[cheat]);
    } else {
      smx_touchmenu_switch(renderer, y, cheats->on[cheat]);
    }
  }
}
