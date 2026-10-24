#include "smx/present.h"

#include <SDL3/SDL.h>

/** Process entry. `int` is required by the C standard. */
int main(int argc, char **argv) {
  bool smoke = false;
  Sint32 index = 1;

  for (; index < argc; index += 1) {
    if (SDL_strcmp(argv[index], "--smoke") == 0) {
      smoke = true;
    }
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow("SuperMetroidX", 960, 540, SDL_WINDOW_RESIZABLE);
  if (window == NULL) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
  if (renderer == NULL) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  Uint8 red = 0;
  Uint8 green = 0;
  Uint8 blue = 0;
  smx_backdrop(&red, &green, &blue);

  bool running = true;
  Uint64 started = SDL_GetTicks();
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
    }

    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    if (smoke && (SDL_GetTicks() - started) >= 200) {
      running = false;
    }
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
