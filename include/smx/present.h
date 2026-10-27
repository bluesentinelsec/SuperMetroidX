#ifndef SMX_PRESENT_H_
#define SMX_PRESENT_H_

#include <SDL3/SDL.h>

/** How the window covers the display. */
typedef enum SmxWindowMode {
  SMX_WINDOW_WINDOWED,
  /** A borderless window the size of the display, at the desktop's video mode. */
  SMX_WINDOW_BORDERLESS,
  /** Exclusive fullscreen at the display mode nearest the desktop's size and 60 Hz. */
  SMX_WINDOW_EXCLUSIVE,
} SmxWindowMode;

/** Display options. Each enhancement has a command-line switch to turn it off. */
typedef struct SmxPresentOptions {
  SmxWindowMode window_mode;
  /** Scale to fill the window; false keeps whole-number scales. */
  bool fill;
  /** Show pixels 8:7 wide, as an NTSC television did; false shows them square. */
  bool tv_aspect;
  /** Draw the picture as a CRT's scanlines; needs OpenGL 3.2. */
  bool scanlines;
} SmxPresentOptions;

/** The picture drawn through OpenGL, or through an SDL renderer when OpenGL is unavailable. */
typedef struct SmxPresenter {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  /** The OpenGL presenter; NULL when presenting through `renderer`. */
  struct SmxPresentGl *gl;
  SmxPresentOptions options;
  Sint32 width;
  Sint32 height;
} SmxPresenter;

/**
 * Backdrop color drawn when no cartridge is loaded.
 * Components are written as 8-bit channels in the range 0-255.
 */
void smx_backdrop(Uint8 *red, Uint8 *green, Uint8 *blue);

/**
 * The defaults: every enhancement on, a borderless fullscreen window in
 * release builds, and a window in debug builds.
 */
void smx_present_defaults(SmxPresentOptions *options);

/**
 * Apply the display option in `args[0]` with its value in `args[1]`.
 * Returns 2 when both were used, 0 when `args[0]` is not a display option,
 * and -1 when the value is missing or unknown.
 */
Sint32 smx_present_option(SmxPresentOptions *options, Sint32 count, char **args);

/** Log the display options and their values. */
void smx_present_usage(void);

/**
 * Place a `src_w` x `src_h` picture in a `dst_w` x `dst_h` window, centered,
 * at the shape and scale `options` asks for. The scale is at least 1.
 */
void smx_present_fit(const SmxPresentOptions *options, Sint32 src_w, Sint32 src_h, Sint32 dst_w,
                     Sint32 dst_h, Sint32 *x, Sint32 *y, Sint32 *w, Sint32 *h);

/**
 * The size of a window that shows the picture at the largest whole-number
 * scale, at most 4, that fits in 85% of a `usable_w` x `usable_h` display.
 */
void smx_present_window_size(const SmxPresentOptions *options, Sint32 src_w, Sint32 src_h,
                             Sint32 usable_w, Sint32 usable_h, Sint32 *w, Sint32 *h);

/**
 * Open the window for a `width` x `height` XRGB8888 picture. Falls back to an
 * SDL renderer, without scanlines, when OpenGL 3.2 is unavailable.
 */
bool smx_present_open(SmxPresenter *presenter, const char *title, Sint32 width, Sint32 height,
                      const SmxPresentOptions *options);

/** Show `pixels`, or the backdrop when it is NULL, and swap buffers. */
void smx_present_frame(SmxPresenter *presenter, const Uint8 *pixels);

/** Switch between a window and the fullscreen mode in the options (borderless if windowed). */
void smx_present_toggle_fullscreen(SmxPresenter *presenter);

/** Hide the mouse pointer while fullscreen. Call for each window event. */
void smx_present_event(SmxPresenter *presenter, const SDL_Event *event);

void smx_present_close(SmxPresenter *presenter);

#endif /* SMX_PRESENT_H_ */
