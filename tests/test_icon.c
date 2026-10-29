#include "smx/icon.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("icon: %s", name);
    g_failed = true;
  }
}

static const Uint8 *pixel(const Uint8 *rgba, Uint32 size, Uint32 x, Uint32 y) {
  return rgba + ((Uint64)y * size + x) * 4u;
}

static void test_full(void) {
  const Uint32 size = 256;
  Uint8 *rgba = (Uint8 *)SDL_malloc(size * size * 4u);
  Uint8 *again = (Uint8 *)SDL_malloc(size * size * 4u);

  if (rgba == NULL || again == NULL) {
    expect("memory for the icon", false);
    SDL_free(rgba);
    SDL_free(again);
    return;
  }
  smx_icon_draw(size, SMX_ICON_FULL, rgba);
  expect("the rounded corners are clear", pixel(rgba, size, 0, 0)[3] == 0 &&
                                              pixel(rgba, size, size - 1, size - 1)[3] == 0);
  expect("the tile is opaque", pixel(rgba, size, size / 2, size / 2)[3] == 255 &&
                                   pixel(rgba, size, size / 2, 8)[3] == 255);
  expect("the beams cross bright in the middle", pixel(rgba, size, size / 2, size / 2)[0] > 200 &&
                                                     pixel(rgba, size, size / 2, size / 2)[2] > 200);
  smx_icon_draw(size, SMX_ICON_FULL, again);
  expect("the icon draws the same every time", SDL_memcmp(rgba, again, size * size * 4u) == 0);
  SDL_free(rgba);
  SDL_free(again);
}

static void test_macos(void) {
  const Uint32 size = 1024;
  Uint8 *rgba = (Uint8 *)SDL_malloc(size * size * 4u);

  if (rgba == NULL) {
    expect("memory for the icon", false);
    return;
  }
  smx_icon_draw(size, SMX_ICON_MACOS, rgba);
  expect("Apple's margin is clear", pixel(rgba, size, 50, 512)[3] == 0 &&
                                        pixel(rgba, size, 512, 50)[3] == 0);
  expect("the tile fills Apple's grid", pixel(rgba, size, 110, 512)[3] == 255 &&
                                            pixel(rgba, size, 913, 512)[3] == 255);
  expect("a shadow falls below the tile",
         pixel(rgba, size, 512, 930)[3] > 0 && pixel(rgba, size, 512, 930)[3] < 255 &&
             pixel(rgba, size, 512, 930)[0] == 0);
  SDL_free(rgba);
}

static void test_surface(void) {
  SDL_Surface *surface = smx_icon_surface();
  int count = 0;
  SDL_Surface **images = NULL;

  expect("a window icon surface", surface != NULL);
  if (surface == NULL) {
    return;
  }
  images = SDL_GetSurfaceImages(surface, &count);
  expect("the window icon has sizes to choose from", images != NULL && count >= 5);
  SDL_free(images);
  SDL_DestroySurface(surface);
}

int main(void) {
  test_full();
  test_macos();
  test_surface();
  return g_failed ? 1 : 0;
}
