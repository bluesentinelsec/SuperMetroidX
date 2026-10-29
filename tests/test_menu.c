#include "smx/menu.h"
#include "smx/overlay.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("menu: %s", name);
    g_failed = true;
  }
}

typedef struct World {
  SmxMenu menu;
  SmxSettings settings;
  SmxCheats cheats;
} World;

static void world(World *w) {
  smx_menu_init(&w->menu);
  smx_settings_defaults(&w->settings);
  smx_cheat_init(&w->cheats, NULL, 0);
  w->settings.display.window_mode = SMX_WINDOW_WINDOWED;
  smx_menu_open(&w->menu);
}

static SmxMenuEvent press(World *w, SmxMenuInput input, Uint64 now) {
  return smx_menu_input(&w->menu, &w->settings, &w->cheats, input, now);
}

static void go_to(World *w, Uint32 row) {
  while (w->menu.row != row) {
    press(w, SMX_MENU_DOWN, 0);
  }
}

static void test_navigation(void) {
  World w;

  world(&w);
  expect("opens on the first row", w.menu.open && w.menu.row == 0);
  expect("up wraps to the last row",
         press(&w, SMX_MENU_UP, 0) == SMX_MENU_NOTHING && w.menu.row == SMX_MENU_ROW_RESUME);
  press(&w, SMX_MENU_DOWN, 0);
  expect("down wraps to the first", w.menu.row == 0);
  expect("back closes", press(&w, SMX_MENU_BACK, 0) == SMX_MENU_CLOSED && !w.menu.open);
  expect("a closed menu ignores input", press(&w, SMX_MENU_DOWN, 0) == SMX_MENU_NOTHING);
  smx_menu_open(&w.menu);
  go_to(&w, SMX_MENU_ROW_RESUME);
  expect("resume closes", press(&w, SMX_MENU_ACCEPT, 0) == SMX_MENU_CLOSED && !w.menu.open);
}

static void test_settings(void) {
  World w;

  world(&w);
  go_to(&w, SMX_SETTING_SCANLINES);
  expect("right changes a setting", press(&w, SMX_MENU_RIGHT, 0) == SMX_MENU_SETTING &&
                                        w.menu.changed == SMX_SETTING_SCANLINES &&
                                        !w.settings.display.scanlines);
  expect("accept changes it too",
         press(&w, SMX_MENU_ACCEPT, 0) == SMX_MENU_SETTING && w.settings.display.scanlines);
  go_to(&w, SMX_SETTING_AUDIO_INTERPOLATION);
  press(&w, SMX_MENU_RIGHT, 0);
  expect("values wrap past the last", w.settings.sound.interpolation == SMX_DSP_GAUSSIAN);
  press(&w, SMX_MENU_LEFT, 0);
  expect("left steps back", w.settings.sound.interpolation == SMX_DSP_SINC);
}

static void test_keep_display(void) {
  World w;

  world(&w);
  expect("a new window mode applies at once",
         press(&w, SMX_MENU_RIGHT, 1000) == SMX_MENU_SETTING &&
             w.settings.display.window_mode == SMX_WINDOW_BORDERLESS &&
             w.menu.ask == SMX_MENU_ASK_KEEP_DISPLAY);
  expect("while asking, up and down do nothing",
         press(&w, SMX_MENU_DOWN, 1000) == SMX_MENU_NOTHING && w.menu.row == 0);
  expect("it waits for an answer",
         smx_menu_tick(&w.menu, &w.settings, 1000 + 9999) == SMX_MENU_NOTHING);
  expect("without one it goes back",
         smx_menu_tick(&w.menu, &w.settings, 1000 + 10000) == SMX_MENU_SETTING &&
             w.settings.display.window_mode == SMX_WINDOW_WINDOWED &&
             w.menu.ask == SMX_MENU_ASK_NONE);
  press(&w, SMX_MENU_RIGHT, 2000);
  expect("keeping it keeps it", press(&w, SMX_MENU_ACCEPT, 2000) == SMX_MENU_NOTHING &&
                                    w.settings.display.window_mode == SMX_WINDOW_BORDERLESS &&
                                    smx_menu_tick(&w.menu, &w.settings, 60000) == SMX_MENU_NOTHING);
  press(&w, SMX_MENU_RIGHT, 3000);
  expect("back reverts it", press(&w, SMX_MENU_BACK, 3000) == SMX_MENU_SETTING &&
                                w.settings.display.window_mode == SMX_WINDOW_BORDERLESS &&
                                w.menu.open);
  press(&w, SMX_MENU_RIGHT, 4000);
  press(&w, SMX_MENU_LEFT, 4000);
  expect("choosing revert reverts it", press(&w, SMX_MENU_ACCEPT, 4000) == SMX_MENU_SETTING &&
                                           w.settings.display.window_mode == SMX_WINDOW_BORDERLESS);
}

static void test_cheats_and_reset(void) {
  World w;

  world(&w);
  go_to(&w, SMX_MENU_ROW_FLY);
  expect("a cheat switches", press(&w, SMX_MENU_RIGHT, 0) == SMX_MENU_CHEAT &&
                                 w.menu.cheat == SMX_CHEAT_FLY && w.cheats.on[SMX_CHEAT_FLY]);
  press(&w, SMX_MENU_LEFT, 0);
  expect("and switches back", !w.cheats.on[SMX_CHEAT_FLY]);
  go_to(&w, SMX_MENU_ROW_ITEMS);
  expect("items need accept",
         press(&w, SMX_MENU_RIGHT, 0) == SMX_MENU_NOTHING && !w.cheats.on[SMX_CHEAT_ITEMS]);
  expect("items are asked for",
         press(&w, SMX_MENU_ACCEPT, 0) == SMX_MENU_CHEAT && w.cheats.on[SMX_CHEAT_ITEMS]);
  go_to(&w, SMX_MENU_ROW_RESET);
  expect("reset asks first", press(&w, SMX_MENU_ACCEPT, 0) == SMX_MENU_NOTHING &&
                                 w.menu.ask == SMX_MENU_ASK_RESET && !w.menu.ask_yes);
  expect("cancel is the default",
         press(&w, SMX_MENU_ACCEPT, 0) == SMX_MENU_NOTHING && w.menu.ask == SMX_MENU_ASK_NONE);
  press(&w, SMX_MENU_ACCEPT, 0);
  press(&w, SMX_MENU_RIGHT, 0);
  expect("reset when chosen", press(&w, SMX_MENU_ACCEPT, 0) == SMX_MENU_RESET);
}

static void test_draw(void) {
  World w;
  SmxOverlay overlay;
  Uint8 *picture = (Uint8 *)SDL_calloc(1, 256u * 224u * 4u);
  const Uint8 *drawn = NULL;
  SDL_Renderer *renderer = NULL;
  Uint32 lit = 0;
  Uint32 index = 0;

  world(&w);
  expect("overlay", smx_overlay_init(&overlay, 256, 224));
  renderer = smx_overlay_begin(&overlay, picture);
  smx_menu_draw(&w.menu, &w.settings, &w.cheats, renderer, 256, 224, 0);
  w.menu.ask = SMX_MENU_ASK_KEEP_DISPLAY;
  w.menu.ask_until_ms = 5000;
  smx_menu_draw(&w.menu, &w.settings, &w.cheats, renderer, 256, 224, 0);
  drawn = smx_overlay_finish(&overlay, 0);
  for (; drawn != NULL && index < 256u * 224u; index += 1) {
    lit += drawn[index * 4u + 1u] > 200u ? 1u : 0u;
  }
  expect("the menu draws text", lit > 500u);
  expect("over a copy of the picture", picture[(100u * 256u + 100u) * 4u] == 0);
  smx_overlay_quit(&overlay);
  SDL_free(picture);
}

int main(void) {
  test_navigation();
  test_settings();
  test_keep_display();
  test_cheats_and_reset();
  test_draw();
  return g_failed ? 1 : 0;
}
