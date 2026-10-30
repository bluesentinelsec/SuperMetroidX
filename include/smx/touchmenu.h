#ifndef SMX_TOUCHMENU_H_
#define SMX_TOUCHMENU_H_

#include "smx/cheat.h"
#include "smx/menu.h"

#include <SDL3/SDL.h>

/** The rows of the cheats menu, top to bottom. */
typedef enum SmxTouchMenuRow {
  SMX_TOUCHMENU_LIFE,
  SMX_TOUCHMENU_AMMO,
  SMX_TOUCHMENU_HYPER,
  SMX_TOUCHMENU_FLY,
  SMX_TOUCHMENU_TURBO,
  SMX_TOUCHMENU_ITEMS,
  SMX_TOUCHMENU_ROWS
} SmxTouchMenuRow;

/**
 * The menu of phones and tablets, in place of the settings menu: the cheats
 * alone, each an on/off switch, drawn over the paused game in picture pixels.
 * A tap on a row flips its switch. Turbo's switch runs at the speed beside
 * it, which a tap steps through; all items is a button, since items are given
 * once. Done, or a tap outside the panel, closes the menu. A gamepad moves
 * through the rows instead: up and down choose, accept flips, left and right
 * step turbo's speed, and back closes.
 */
typedef struct SmxTouchMenu {
  bool open;
  Uint32 row;
  /** A gamepad moved through the rows, so the chosen one is marked. */
  bool row_shown;
  /** The speed turbo's switch turns on, from 1 to SMX_CHEAT_TURBO_SPEEDS - 1. */
  Uint32 turbo_speed;
  /** The cheat the last SMX_MENU_CHEAT switched. */
  SmxCheatKind cheat;
} SmxTouchMenu;

void smx_touchmenu_init(SmxTouchMenu *menu);

/** Open over the game at the first row, showing turbo's speed if `cheats` has turbo on. */
void smx_touchmenu_open(SmxTouchMenu *menu, const SmxCheats *cheats);

/** Act on a tap at `x`, `y` in picture pixels. */
SmxMenuEvent smx_touchmenu_tap(SmxTouchMenu *menu, SmxCheats *cheats, float x, float y);

/** Act on a gamepad's or keyboard's input. */
SmxMenuEvent smx_touchmenu_input(SmxTouchMenu *menu, SmxCheats *cheats, SmxMenuInput input);

/** Draw the menu on a `width` x `height` picture through `renderer`. */
void smx_touchmenu_draw(const SmxTouchMenu *menu, const SmxCheats *cheats, SDL_Renderer *renderer,
                        Uint32 width, Uint32 height);

#endif /* SMX_TOUCHMENU_H_ */
