#include "smx/present.h"

#include <SDL3/SDL.h>

int main(void) {
  Uint8 red = 0;
  Uint8 green = 0;
  Uint8 blue = 0;

  smx_backdrop(&red, &green, &blue);
  if (red != 12 || green != 18 || blue != 32) {
    SDL_Log("backdrop mismatch: %u %u %u", (unsigned)red, (unsigned)green, (unsigned)blue);
    return 1;
  }

  Sint32 x = 0;
  Sint32 y = 0;
  Sint32 w = 0;
  Sint32 h = 0;
  smx_present_fit(256, 224, 960, 540, &x, &y, &w, &h);
  if (w != 512 || h != 448 || x != 224 || y != 46) {
    SDL_Log("fit mismatch: %d %d %d %d", (int)x, (int)y, (int)w, (int)h);
    return 1;
  }
  return 0;
}
