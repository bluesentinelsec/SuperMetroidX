#ifndef SMX_MENU_H_
#define SMX_MENU_H_

#include "smx/cheat.h"
#include "smx/settings.h"

#include <SDL3/SDL.h>

/** Seconds a new window mode waits to be kept before it is put back. */
#define SMX_MENU_KEEP_SECONDS 10u

/** The menu's rows, top to bottom. The first SMX_SETTING_COUNT are the settings. */
typedef enum SmxMenuRow {
  SMX_MENU_ROW_LIFE = SMX_SETTING_COUNT,
  SMX_MENU_ROW_AMMO,
  SMX_MENU_ROW_HYPER,
  SMX_MENU_ROW_FLY,
  SMX_MENU_ROW_ITEMS,
  SMX_MENU_ROW_RESET,
  SMX_MENU_ROW_RESUME,
  SMX_MENU_ROWS
} SmxMenuRow;

/** What the player pressed, from the keyboard or a gamepad. */
typedef enum SmxMenuInput {
  SMX_MENU_UP,
  SMX_MENU_DOWN,
  SMX_MENU_LEFT,
  SMX_MENU_RIGHT,
  SMX_MENU_ACCEPT,
  SMX_MENU_BACK
} SmxMenuInput;

/** What the shell has to do after an input. */
typedef enum SmxMenuEvent {
  SMX_MENU_NOTHING,
  /** A setting changed: apply it and save. `SmxMenu.changed` says which. */
  SMX_MENU_SETTING,
  /** A cheat was switched, or items asked for: `SmxMenu.cheat` says which. */
  SMX_MENU_CHEAT,
  /** Every setting back to its default, the file removed, and the cheats off. */
  SMX_MENU_RESET,
  SMX_MENU_CLOSED
} SmxMenuEvent;

/** A question the menu waits on. */
typedef enum SmxMenuAsk {
  SMX_MENU_ASK_NONE,
  SMX_MENU_ASK_KEEP_DISPLAY,
  SMX_MENU_ASK_RESET
} SmxMenuAsk;

/**
 * The settings menu drawn over the paused game. Up and down choose a row;
 * left and right change a setting or switch a cheat; accept does the same or
 * runs an action; back closes the menu. Changes apply at once. A new window
 * mode asks to be kept, and goes back after SMX_MENU_KEEP_SECONDS without an
 * answer, so a mode the display cannot show undoes itself. Resetting asks
 * first.
 */
typedef struct SmxMenu {
  bool open;
  Uint32 row;
  SmxMenuAsk ask;
  /** The answer chosen: keep, or reset. */
  bool ask_yes;
  Uint64 ask_until_ms;
  /** The window mode to go back to if a new one is not kept. */
  Uint32 window_before;
  SmxSettingId changed;
  SmxCheatKind cheat;
} SmxMenu;

void smx_menu_init(SmxMenu *menu);
void smx_menu_open(SmxMenu *menu);

/** Act on an input. `settings` and `cheats` change in place. */
SmxMenuEvent smx_menu_input(SmxMenu *menu, SmxSettings *settings, SmxCheats *cheats,
                            SmxMenuInput input, Uint64 now_ms);

/** Put the window mode back when its question has waited too long. */
SmxMenuEvent smx_menu_tick(SmxMenu *menu, SmxSettings *settings, Uint64 now_ms);

/** Draw the menu on a `width` x `height` picture through `renderer`, with SDL's 8x8 font. */
void smx_menu_draw(const SmxMenu *menu, const SmxSettings *settings, const SmxCheats *cheats,
                   SDL_Renderer *renderer, Uint32 width, Uint32 height, Uint64 now_ms);

#endif /* SMX_MENU_H_ */
