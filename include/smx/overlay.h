#ifndef SMX_OVERLAY_H_
#define SMX_OVERLAY_H_

#include <SDL3/SDL.h>

/** Messages shown at once; a new one pushes out the oldest. */
#define SMX_OVERLAY_LINES 4u
/** How long a message shows. */
#define SMX_OVERLAY_MS 2500u

/**
 * Short messages in the bottom left of the picture, such as a cheat turning
 * on. They are drawn with SDL's built-in 8x8 font, through a software
 * renderer, onto a copy of the picture, so they scale with it and the
 * picture the console drew is left alone.
 */
typedef struct SmxOverlay {
  Uint32 width;
  Uint32 height;
  Uint8 *pixels;
  SDL_Surface *surface;
  SDL_Renderer *renderer;
  char text[SMX_OVERLAY_LINES][48];
  Uint64 until[SMX_OVERLAY_LINES];
  Uint32 count;
  /** A hold in progress, such as to skip the intro: its label and how far along it is. */
  char hold_text[32];
  Uint32 hold;
  Uint32 hold_of;
} SmxOverlay;

/** For XRGB8888 pictures `width` by `height`. */
bool smx_overlay_init(SmxOverlay *overlay, Uint32 width, Uint32 height);
void smx_overlay_quit(SmxOverlay *overlay);

/** Drop every message and the hold, as when the settings menu opens over them. */
void smx_overlay_clear(SmxOverlay *overlay);

/** Show `text` from `now_ms` for SMX_OVERLAY_MS. */
void smx_overlay_show(SmxOverlay *overlay, const char *text, Uint64 now_ms);

/**
 * Show `text` over a bar `done` of `total` full in the bottom right, until
 * called again; a `done` of 0 hides it.
 */
void smx_overlay_hold(SmxOverlay *overlay, const char *text, Uint32 done, Uint32 total);

/**
 * `picture` with the messages showing at `now_ms` drawn on a copy, or
 * `picture` itself when there are none.
 */
const Uint8 *smx_overlay_draw(SmxOverlay *overlay, const Uint8 *picture, Uint64 now_ms);

/**
 * Copy `picture` to draw more on it, such as the settings menu, through the
 * renderer this returns (NULL if the overlay could not start). Finish with
 * `smx_overlay_finish`, which adds the messages and the hold, and returns the
 * picture.
 */
SDL_Renderer *smx_overlay_begin(SmxOverlay *overlay, const Uint8 *picture);
const Uint8 *smx_overlay_finish(SmxOverlay *overlay, Uint64 now_ms);

#endif /* SMX_OVERLAY_H_ */
