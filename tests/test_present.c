#include "smx/present.h"

#include <SDL3/SDL.h>

static bool check_fit(const SmxPresentOptions *options, Sint32 dst_w, Sint32 dst_h, Sint32 want_x,
                      Sint32 want_y, Sint32 want_w, Sint32 want_h) {
  Sint32 x = 0;
  Sint32 y = 0;
  Sint32 w = 0;
  Sint32 h = 0;

  smx_present_fit(options, 256, 224, dst_w, dst_h, &x, &y, &w, &h);
  if (x != want_x || y != want_y || w != want_w || h != want_h) {
    SDL_Log("fit %dx%d: got %d %d %d %d, want %d %d %d %d", (int)dst_w, (int)dst_h, (int)x, (int)y,
            (int)w, (int)h, (int)want_x, (int)want_y, (int)want_w, (int)want_h);
    return false;
  }
  return true;
}

static bool check_window(const SmxPresentOptions *options, Sint32 usable_w, Sint32 usable_h,
                         Sint32 want_w, Sint32 want_h) {
  Sint32 w = 0;
  Sint32 h = 0;

  smx_present_window_size(options, 256, 224, usable_w, usable_h, &w, &h);
  if (w != want_w || h != want_h) {
    SDL_Log("window for %dx%d: got %dx%d, want %dx%d", (int)usable_w, (int)usable_h, (int)w, (int)h,
            (int)want_w, (int)want_h);
    return false;
  }
  return true;
}

static bool test_fit(void) {
  SmxPresentOptions options;
  bool ok = true;

  smx_present_defaults(&options);
  /* Filling with 8:7 pixels: 292.57 x 224 scaled to the window's height or width. */
  ok = check_fit(&options, 960, 540, 127, 0, 705, 540) && ok;
  ok = check_fit(&options, 800, 1000, 0, 193, 800, 613) && ok;
  ok = check_fit(&options, 1920, 1080, 254, 0, 1411, 1080) && ok;
  options.tv_aspect = false;
  ok = check_fit(&options, 1920, 1080, 343, 0, 1234, 1080) && ok;
  options.fill = false;
  ok = check_fit(&options, 960, 540, 224, 46, 512, 448) && ok;
  ok = check_fit(&options, 100, 100, -78, -62, 256, 224) && ok;
  options.tv_aspect = true;
  ok = check_fit(&options, 960, 540, 187, 46, 585, 448) && ok;
  /* 3x by height, but only 2x fits across: 3 * 292.57 > 860. */
  ok = check_fit(&options, 860, 700, 137, 126, 585, 448) && ok;
  return ok;
}

static bool test_window_size(void) {
  SmxPresentOptions options;
  bool ok = true;

  smx_present_defaults(&options);
  ok = check_window(&options, 1920, 1055, 1170, 896) && ok;
  ok = check_window(&options, 1440, 875, 878, 672) && ok;
  ok = check_window(&options, 640, 480, 293, 224) && ok;
  options.tv_aspect = false;
  ok = check_window(&options, 1440, 875, 768, 672) && ok;
  return ok;
}

static bool test_options(void) {
  SmxPresentOptions options;
  char *window[] = {"--window-mode", "exclusive"};
  char *fullscreen[] = {"--window-mode", "fullscreen"};
  char *windowed[] = {"--window-mode", "windowed"};
  char *borderless[] = {"--window-mode", "borderless"};
  char *scale[] = {"--scale", "integer"};
  char *aspect[] = {"--aspect", "square"};
  char *scanlines[] = {"--scanlines", "off"};
  char *bad[] = {"--scanlines", "maybe"};
  char *missing[] = {"--aspect"};
  char *other[] = {"--load-state", "rom.sfc"};

  smx_present_defaults(&options);
  if (!options.fill || !options.tv_aspect || !options.scanlines) {
    SDL_Log("enhancements should default on");
    return false;
  }
#ifdef NDEBUG
  if (options.window_mode != SMX_WINDOW_BORDERLESS) {
    SDL_Log("release builds should default to borderless fullscreen");
    return false;
  }
#else
  if (options.window_mode != SMX_WINDOW_WINDOWED) {
    SDL_Log("debug builds should default to a window");
    return false;
  }
#endif
  if (smx_present_option(&options, 2, window) != 2 || options.window_mode != SMX_WINDOW_EXCLUSIVE ||
      smx_present_option(&options, 2, borderless) != 2 ||
      options.window_mode != SMX_WINDOW_BORDERLESS ||
      smx_present_option(&options, 2, fullscreen) != 2 ||
      options.window_mode != SMX_WINDOW_EXCLUSIVE ||
      smx_present_option(&options, 2, windowed) != 2 ||
      options.window_mode != SMX_WINDOW_WINDOWED) {
    SDL_Log("--window-mode not applied");
    return false;
  }
  if (smx_present_option(&options, 2, scale) != 2 || options.fill ||
      smx_present_option(&options, 2, aspect) != 2 || options.tv_aspect ||
      smx_present_option(&options, 2, scanlines) != 2 || options.scanlines) {
    SDL_Log("enhancement switches not applied");
    return false;
  }
  if (smx_present_option(&options, 2, bad) != -1 ||
      smx_present_option(&options, 1, missing) != -1 ||
      smx_present_option(&options, 2, other) != 0) {
    SDL_Log("bad display options not reported");
    return false;
  }
  return true;
}

int main(void) {
  Uint8 red = 0;
  Uint8 green = 0;
  Uint8 blue = 0;
  bool ok = true;

  smx_backdrop(&red, &green, &blue);
  if (red != 12 || green != 18 || blue != 32) {
    SDL_Log("backdrop mismatch: %u %u %u", (unsigned)red, (unsigned)green, (unsigned)blue);
    ok = false;
  }
  ok = test_fit() && ok;
  ok = test_window_size() && ok;
  ok = test_options() && ok;
  return ok ? 0 : 1;
}
