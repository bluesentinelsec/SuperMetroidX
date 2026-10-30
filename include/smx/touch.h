#ifndef SMX_TOUCH_H_
#define SMX_TOUCH_H_

#include <SDL3/SDL.h>

/** Fingers followed at once. */
#define SMX_TOUCH_FINGERS 10u

/** The on-screen controls. */
typedef enum SmxTouchControl {
  SMX_TOUCH_DPAD,
  SMX_TOUCH_A,
  SMX_TOUCH_B,
  SMX_TOUCH_X,
  SMX_TOUCH_Y,
  SMX_TOUCH_L,
  SMX_TOUCH_R,
  SMX_TOUCH_SELECT,
  SMX_TOUCH_START,
  /** Opens the cheats menu. */
  SMX_TOUCH_MENU,
  SMX_TOUCH_CONTROLS
} SmxTouchControl;

/** A control's center and size in window pixels. */
typedef struct SmxTouchShape {
  float x;
  float y;
  float w;
  float h;
} SmxTouchShape;

/** A finger on the screen, in window pixels. */
typedef struct SmxTouchFinger {
  SDL_FingerID id;
  float x;
  float y;
  bool down;
  /** It came down on the D-pad, so it steers the D-pad wherever it moves. */
  bool dpad;
} SmxTouchFinger;

/**
 * A controller drawn over the window for phones and tablets, in the colors of
 * the Super Famicom: the D-pad on the left with L and R above it, where the
 * left thumb reaches them, X, A, B, and Y in a diamond on the right, and
 * Select and Start at the bottom in the middle. Any number of fingers press
 * at once. A finger that comes down on the D-pad steers it until it lifts;
 * other fingers press whatever they are over, so a thumb can slide between
 * buttons or rest between two to press both, as on the real pad.
 */
typedef struct SmxTouch {
  float width;
  float height;
  SmxTouchShape shapes[SMX_TOUCH_CONTROLS];
  SmxTouchFinger fingers[SMX_TOUCH_FINGERS];
  /** Drawn; the shell hides the controls while a gamepad is in use. */
  bool shown;
} SmxTouch;

void smx_touch_init(SmxTouch *touch);

/**
 * Lay the controls out in a `width` x `height` window whose safe area, clear
 * of rounded corners, the camera, and the home indicator, is `safe`, all in
 * pixels.
 */
void smx_touch_layout(SmxTouch *touch, Sint32 width, Sint32 height, const SDL_Rect *safe);

/** Follow a finger event. Returns true when it tapped the menu button. */
bool smx_touch_event(SmxTouch *touch, const SDL_Event *event);

/** Lift every finger, as when the menu opens over the controls. */
void smx_touch_release(SmxTouch *touch);

/** The SNES buttons the fingers hold, as joypad bits. */
Uint16 smx_touch_joy(const SmxTouch *touch);

/** Draw the controls through `renderer`, the held ones lit. */
void smx_touch_draw(const SmxTouch *touch, SDL_Renderer *renderer);

/** Fill a circle of radius `r` at `x`, `y`, blended, through `renderer`. */
void smx_touch_fill_circle(SDL_Renderer *renderer, float x, float y, float r, SDL_FColor color);

/** Fill a box with round ends, `w` x `h` around `x`, `y`, blended, through `renderer`. */
void smx_touch_fill_capsule(SDL_Renderer *renderer, float x, float y, float w, float h,
                            SDL_FColor color);

#endif /* SMX_TOUCH_H_ */
