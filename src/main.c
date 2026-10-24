#include "smx/apu.h"
#include "smx/cart.h"
#include "smx/clock.h"
#include "smx/ppu.h"
#include "smx/present.h"

#include <SDL3/SDL.h>

/** Process entry. `int` is required by the C standard. */
int main(int argc, char **argv) {
  bool smoke = false;
  const char *rom_path = NULL;
  Sint32 index = 1;

  for (; index < argc; index += 1) {
    if (SDL_strcmp(argv[index], "--smoke") == 0) {
      smoke = true;
    } else if (rom_path == NULL) {
      rom_path = argv[index];
    }
  }
  if (rom_path == NULL) {
    rom_path = SDL_getenv("SMX_ROM");
  }
  if (rom_path == NULL || rom_path[0] == '\0') {
    rom_path = "SuperMetroid.sfc";
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

  SmxCart cart;
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxApu apu;
  SmxClock clock;
  bool playing = false;
  bool machine = false;
  smx_cart_init(&cart);
  if (smx_cart_load_path(&cart, rom_path)) {
    smx_bus_init(&bus, cart.bytes, cart.size, smx_cart_sram_bytes(cart.sram_size_code));
    smx_cpu_init(&cpu, &bus);
    smx_dma_init(&dma, &bus);
    smx_ppu_init(&ppu, &bus);
    smx_apu_init(&apu, &bus);
    smx_clock_init(&clock, &cpu, &dma);
    smx_cpu_reset(&cpu);
    machine = true;
    playing = true;
    SDL_Log("loaded %s", cart.title);
  }

  SDL_Texture *texture = NULL;
  if (playing) {
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                SMX_PPU_WIDTH, SMX_PPU_HEIGHT);
    if (texture == NULL) {
      SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
      playing = false;
    } else {
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
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

    if (playing) {
      if (!smx_clock_frame(&clock)) {
        SDL_Log("cpu stopped at %u:%u", (unsigned)cpu.pb, (unsigned)cpu.pc);
        playing = false;
      }
      smx_ppu_render(&ppu);
      SDL_UpdateTexture(texture, NULL, ppu.pixels, SMX_PPU_WIDTH * 4);
      int win_w = 0;
      int win_h = 0;
      Sint32 dst_x = 0;
      Sint32 dst_y = 0;
      Sint32 dst_w = 0;
      Sint32 dst_h = 0;
      SDL_GetWindowSize(window, &win_w, &win_h);
      smx_present_fit(SMX_PPU_WIDTH, SMX_PPU_HEIGHT, (Sint32)win_w, (Sint32)win_h, &dst_x, &dst_y,
                      &dst_w, &dst_h);
      SDL_FRect dst;
      dst.x = (float)dst_x;
      dst.y = (float)dst_y;
      dst.w = (float)dst_w;
      dst.h = (float)dst_h;
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      SDL_RenderTexture(renderer, texture, NULL, &dst);
    } else {
      SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
      SDL_RenderClear(renderer);
    }
    SDL_RenderPresent(renderer);

    if (smoke && (SDL_GetTicks() - started) >= 200) {
      running = false;
    }
  }

  if (texture != NULL) {
    SDL_DestroyTexture(texture);
  }
  if (machine) {
    smx_ppu_quit(&ppu);
    smx_bus_quit(&bus);
  }
  smx_cart_free(&cart);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
