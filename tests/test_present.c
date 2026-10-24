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
  return 0;
}
