#include "smx/touch.h"
#include "smx/touchmenu.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("touch: %s", name);
    g_failed = true;
  }
}

/* An iPhone in landscape: 2622 x 1206 pixels, the camera side and the home indicator unsafe. */
static void phone(SmxTouch *touch) {
  SDL_Rect safe = {177, 0, 2268, 1143};

  smx_touch_init(touch);
  smx_touch_layout(touch, 2622, 1206, &safe);
}

static bool finger(SmxTouch *touch, SDL_EventType type, SDL_FingerID id, float x, float y) {
  SDL_Event event;

  SDL_memset(&event, 0, sizeof(event));
  event.type = type;
  event.tfinger.fingerID = id;
  event.tfinger.x = x / touch->width;
  event.tfinger.y = y / touch->height;
  return smx_touch_event(touch, &event);
}

static const SmxTouchShape *at(const SmxTouch *touch, SmxTouchControl control) {
  return &touch->shapes[control];
}

static void test_layout(void) {
  SmxTouch touch;
  const SmxTouchShape *pad = NULL;

  phone(&touch);
  pad = at(&touch, SMX_TOUCH_DPAD);
  expect("the D-pad is on the left, inside the safe area",
         pad->x - pad->w * 0.5f >= 177.0f && pad->x < 2622.0f * 0.25f);
  expect("L and R are above the D-pad, L on the left",
         at(&touch, SMX_TOUCH_L)->y < pad->y - pad->h * 0.5f &&
             at(&touch, SMX_TOUCH_R)->y < pad->y - pad->h * 0.5f &&
             at(&touch, SMX_TOUCH_L)->x < at(&touch, SMX_TOUCH_R)->x &&
             SDL_fabsf((at(&touch, SMX_TOUCH_L)->x + at(&touch, SMX_TOUCH_R)->x) * 0.5f - pad->x) <
                 1.0f);
  expect("X is above B, Y left of A, on the right",
         at(&touch, SMX_TOUCH_X)->y < at(&touch, SMX_TOUCH_B)->y &&
             at(&touch, SMX_TOUCH_Y)->x < at(&touch, SMX_TOUCH_A)->x &&
             at(&touch, SMX_TOUCH_A)->x + at(&touch, SMX_TOUCH_A)->w * 0.5f <= 177.0f + 2268.0f &&
             at(&touch, SMX_TOUCH_Y)->x > 2622.0f * 0.75f);
  expect("Select and Start are centered at the bottom, Select first",
         at(&touch, SMX_TOUCH_SELECT)->x < 1311.0f && at(&touch, SMX_TOUCH_START)->x > 1311.0f &&
             SDL_fabsf(at(&touch, SMX_TOUCH_SELECT)->x + at(&touch, SMX_TOUCH_START)->x - 2622.0f) <
                 1.0f &&
             at(&touch, SMX_TOUCH_START)->y > 1000.0f && at(&touch, SMX_TOUCH_START)->y < 1143.0f);
}

static void test_buttons(void) {
  SmxTouch touch;
  const SmxTouchShape *a = NULL;
  const SmxTouchShape *b = NULL;
  const SmxTouchShape *pad = NULL;

  phone(&touch);
  a = at(&touch, SMX_TOUCH_A);
  b = at(&touch, SMX_TOUCH_B);
  pad = at(&touch, SMX_TOUCH_DPAD);
  expect("nothing is held at first", smx_touch_joy(&touch) == 0);
  finger(&touch, SDL_EVENT_FINGER_DOWN, 1, a->x, a->y);
  expect("A", smx_touch_joy(&touch) == 0x0080);
  finger(&touch, SDL_EVENT_FINGER_MOTION, 1, (a->x + b->x) * 0.5f, (a->y + b->y) * 0.5f);
  expect("between A and B holds both", smx_touch_joy(&touch) == (0x0080 | 0x8000));
  finger(&touch, SDL_EVENT_FINGER_MOTION, 1, b->x, b->y);
  expect("sliding onto B lets go of A", smx_touch_joy(&touch) == 0x8000);
  finger(&touch, SDL_EVENT_FINGER_DOWN, 2, pad->x + pad->w * 0.4f, pad->y);
  expect("a second finger on the D-pad holds right too", smx_touch_joy(&touch) == (0x8000 | 0x0100));
  finger(&touch, SDL_EVENT_FINGER_MOTION, 2, pad->x - pad->w * 0.3f, pad->y - pad->w * 0.3f);
  expect("the D-pad finger rolls to up-left", smx_touch_joy(&touch) == (0x8000 | 0x0800 | 0x0200));
  finger(&touch, SDL_EVENT_FINGER_MOTION, 2, pad->x, pad->y + pad->w * 2.0f);
  expect("and keeps steering off the pad", smx_touch_joy(&touch) == (0x8000 | 0x0400));
  finger(&touch, SDL_EVENT_FINGER_MOTION, 2, pad->x, pad->y);
  expect("its middle holds no direction", smx_touch_joy(&touch) == 0x8000);
  finger(&touch, SDL_EVENT_FINGER_UP, 1, b->x, b->y);
  finger(&touch, SDL_EVENT_FINGER_CANCELED, 2, pad->x, pad->y);
  expect("lifted fingers hold nothing", smx_touch_joy(&touch) == 0);
  finger(&touch, SDL_EVENT_FINGER_DOWN, 3, at(&touch, SMX_TOUCH_L)->x, at(&touch, SMX_TOUCH_L)->y);
  finger(&touch, SDL_EVENT_FINGER_DOWN, 4, at(&touch, SMX_TOUCH_START)->x,
         at(&touch, SMX_TOUCH_START)->y);
  expect("L and Start", smx_touch_joy(&touch) == (0x0020 | 0x1000));
  smx_touch_release(&touch);
  expect("release lets go of everything", smx_touch_joy(&touch) == 0);
  expect("the menu button reports a tap",
         finger(&touch, SDL_EVENT_FINGER_DOWN, 5, at(&touch, SMX_TOUCH_MENU)->x,
                at(&touch, SMX_TOUCH_MENU)->y) &&
             smx_touch_joy(&touch) == 0);
  expect("the open game is no button", !finger(&touch, SDL_EVENT_FINGER_DOWN, 6, 1311.0f, 500.0f) &&
                                           smx_touch_joy(&touch) == 0);
}

static void test_draw(void) {
  SmxTouch touch;
  SDL_Surface *surface = SDL_CreateSurface(2622, 1206, SDL_PIXELFORMAT_XRGB8888);
  SDL_Renderer *renderer = surface != NULL ? SDL_CreateSoftwareRenderer(surface) : NULL;
  const SmxTouchShape *a = NULL;
  Uint8 r = 0;
  Uint8 g = 0;
  Uint8 b = 0;

  expect("renderer", renderer != NULL);
  if (renderer == NULL) {
    return;
  }
  phone(&touch);
  a = at(&touch, SMX_TOUCH_A);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  smx_touch_draw(&touch, renderer);
  SDL_RenderPresent(renderer);
  SDL_ReadSurfacePixel(surface, (int)(a->x + a->w * 0.3f), (int)a->y, &r, &g, &b, NULL);
  expect("A is red", r > 80 && r > g * 2 && r > b * 2);
  touch.shown = false;
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  smx_touch_draw(&touch, renderer);
  SDL_RenderPresent(renderer);
  SDL_ReadSurfacePixel(surface, (int)(a->x + a->w * 0.3f), (int)a->y, &r, &g, &b, NULL);
  expect("hidden controls draw nothing", r == 0 && g == 0 && b == 0);
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(surface);
}

static void test_menu(void) {
  SmxTouchMenu menu;
  SmxCheats cheats;

  smx_cheat_init(&cheats, NULL, 0);
  smx_touchmenu_init(&menu);
  expect("a closed menu ignores taps", smx_touchmenu_tap(&menu, &cheats, 128, 100) ==
                                           SMX_MENU_NOTHING);
  smx_touchmenu_open(&menu, &cheats);
  /* Rows are 26 pixels tall from 42; switches are at x 210. */
  expect("a tap on a row switches its cheat",
         smx_touchmenu_tap(&menu, &cheats, 210, 42 + 13) == SMX_MENU_CHEAT &&
             menu.cheat == SMX_CHEAT_LIFE && cheats.on[SMX_CHEAT_LIFE]);
  expect("and off again", smx_touchmenu_tap(&menu, &cheats, 60, 42 + 13) == SMX_MENU_CHEAT &&
                              !cheats.on[SMX_CHEAT_LIFE]);
  expect("fly", smx_touchmenu_tap(&menu, &cheats, 210, 42 + 26 * 3 + 13) == SMX_MENU_CHEAT &&
                    cheats.on[SMX_CHEAT_FLY]);
  expect("turbo's switch turns on at 2x",
         smx_touchmenu_tap(&menu, &cheats, 210, 42 + 26 * 4 + 13) == SMX_MENU_TURBO &&
             cheats.turbo == 2);
  expect("its speed steps while on",
         smx_touchmenu_tap(&menu, &cheats, 168, 42 + 26 * 4 + 13) == SMX_MENU_TURBO &&
             cheats.turbo == 3);
  smx_touchmenu_tap(&menu, &cheats, 210, 42 + 26 * 4 + 13);
  expect("off keeps the speed for next time", cheats.turbo == 0 && menu.turbo_speed == 3);
  expect("items are given once",
         smx_touchmenu_tap(&menu, &cheats, 200, 42 + 26 * 5 + 13) == SMX_MENU_CHEAT &&
             cheats.on[SMX_CHEAT_ITEMS] &&
             smx_touchmenu_tap(&menu, &cheats, 200, 42 + 26 * 5 + 13) == SMX_MENU_NOTHING &&
             cheats.on[SMX_CHEAT_ITEMS]);
  expect("Done closes", smx_touchmenu_tap(&menu, &cheats, 206, 25) == SMX_MENU_CLOSED && !menu.open);
  smx_touchmenu_open(&menu, &cheats);
  expect("a tap outside the panel closes",
         smx_touchmenu_tap(&menu, &cheats, 5, 100) == SMX_MENU_CLOSED && !menu.open);
  smx_touchmenu_open(&menu, &cheats);
  smx_touchmenu_input(&menu, &cheats, SMX_MENU_DOWN);
  expect("a gamepad moves and flips",
         smx_touchmenu_input(&menu, &cheats, SMX_MENU_ACCEPT) == SMX_MENU_CHEAT &&
             cheats.on[SMX_CHEAT_AMMO] && menu.row_shown);
  menu.row = SMX_TOUCHMENU_TURBO;
  smx_touchmenu_input(&menu, &cheats, SMX_MENU_LEFT);
  smx_touchmenu_input(&menu, &cheats, SMX_MENU_LEFT);
  smx_touchmenu_input(&menu, &cheats, SMX_MENU_LEFT);
  expect("left steps turbo's speed down, wrapping", menu.turbo_speed == 4 && cheats.turbo == 0);
  expect("back closes", smx_touchmenu_input(&menu, &cheats, SMX_MENU_BACK) == SMX_MENU_CLOSED);
  cheats.turbo = 1;
  smx_touchmenu_open(&menu, &cheats);
  expect("opening shows turbo's speed", menu.turbo_speed == 1);
}

int main(void) {
  test_layout();
  test_buttons();
  test_draw();
  test_menu();
  return g_failed ? 1 : 0;
}
