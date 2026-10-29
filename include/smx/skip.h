#ifndef SMX_SKIP_H_
#define SMX_SKIP_H_

#include <SDL3/SDL.h>

/** Frames any button is held before the intro is skipped: a second and a half. */
#define SMX_SKIP_HOLD_FRAMES 90u
/** Frames held before the progress shows, so a tap to turn a page of text shows nothing. */
#define SMX_SKIP_SHOW_FRAMES 12u

/** What a frame did to the intro. */
typedef enum SmxSkipEvent {
  SMX_SKIP_NONE,
  /** The intro began, so the player can be told how to skip it. */
  SMX_SKIP_INTRO,
  /** The intro is ending early. */
  SMX_SKIP_SKIPPED
} SmxSkipEvent;

/**
 * Skipping the intro of a new game by holding any button. Like the cheats,
 * it writes Super Metroid's RAM between frames: it hands the intro to the
 * routine its own last scene ends with, which fades out and starts the game
 * at Ceres, and first undoes what the intro's flashback borrows if the
 * flashback has not finished.
 */
typedef struct SmxSkip {
  /** Frames the buttons have been held in the intro. */
  Uint32 held;
  bool in_intro;
} SmxSkip;

void smx_skip_init(SmxSkip *skip);

/** Count a frame that reads joypad `joy`, before it runs, and skip once held long enough. */
SmxSkipEvent smx_skip_apply(SmxSkip *skip, Uint8 *wram, Uint16 joy);

/** The hold to show, from 0 (none) to SMX_SKIP_HOLD_FRAMES. */
Uint32 smx_skip_shown(const SmxSkip *skip);

#endif /* SMX_SKIP_H_ */
