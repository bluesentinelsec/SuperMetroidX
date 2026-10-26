#include "smx/apu.h"
#include "smx/cart.h"
#include "smx/clock.h"
#include "smx/game.h"
#include "smx/ppu.h"
#include "smx/present.h"
#include "smx/translate.h"

#include <SDL3/SDL.h>

/** Process entry. `int` is required by the C standard. */
/** Audio queued ahead, in NTSC frames, before the shell waits. */
#define SMX_SHELL_QUEUE_FRAMES 3u
/** One average NTSC frame: 357366 master clocks at 21477270 Hz. */
#define SMX_SHELL_FRAME_NS 16639266u

/*
 * Keyboard to joypad, in the bit order of a 16-bit read of `$4218`:
 * B Y Select Start Up Down Left Right in the high byte, A X L R in the low.
 */
static const struct {
  SDL_Scancode scancode;
  Uint16 button;
} smx_keymap[] = {
    {SDL_SCANCODE_Z, 0x8000},      {SDL_SCANCODE_X, 0x4000},     {SDL_SCANCODE_RSHIFT, 0x2000},
    {SDL_SCANCODE_RETURN, 0x1000}, {SDL_SCANCODE_UP, 0x0800},    {SDL_SCANCODE_DOWN, 0x0400},
    {SDL_SCANCODE_LEFT, 0x0200},   {SDL_SCANCODE_RIGHT, 0x0100}, {SDL_SCANCODE_A, 0x0080},
    {SDL_SCANCODE_S, 0x0040},      {SDL_SCANCODE_Q, 0x0020},     {SDL_SCANCODE_W, 0x0010},
};

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
  bool audio_ready = SDL_InitSubSystem(SDL_INIT_AUDIO);

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
    clock.ppu = &ppu;
    smx_cpu_reset(&cpu);
    if (smx_game.code.count != 0 && smx_game.rom_size == cart.size &&
        smx_game.rom_hash == smx_translate_rom_hash(cart.bytes, cart.size)) {
      cpu.code = &smx_game.code;
      SDL_Log("running %u translated entry points", (unsigned)smx_game.code.count);
    } else if (smx_game.code.count != 0) {
      SDL_Log("the translation is of a different ROM; interpreting");
    }
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

  SDL_AudioStream *audio = NULL;
  if (playing && audio_ready) {
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 32040;
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (audio == NULL) {
      SDL_Log("audio unavailable: %s", SDL_GetError());
    } else if (!SDL_ResumeAudioStreamDevice(audio)) {
      SDL_Log("audio resume failed: %s", SDL_GetError());
      SDL_DestroyAudioStream(audio);
      audio = NULL;
    }
  }

  bool running = true;
  Uint64 started = SDL_GetTicks();
  Uint64 next_frame = SDL_GetTicksNS();
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
    }

    /*
     * Pace emulation by the audio queue when there is one, else by the
     * average NTSC frame of 357366 master clocks.
     */
    if (playing && !smoke) {
      if (audio != NULL) {
        if (SDL_GetAudioStreamQueued(audio) > (int)(SMX_SHELL_QUEUE_FRAMES * 534u * 4u)) {
          SDL_DelayNS(1000000);
          continue;
        }
      } else {
        Uint64 now = SDL_GetTicksNS();
        if (now < next_frame) {
          SDL_DelayNS(next_frame - now);
        }
        next_frame += SMX_SHELL_FRAME_NS;
        if (SDL_GetTicksNS() > next_frame + 4u * SMX_SHELL_FRAME_NS) {
          next_frame = SDL_GetTicksNS();
        }
      }
    }

    if (playing) {
      const bool *keys = SDL_GetKeyboardState(NULL);
      Uint16 joy = 0;
      Uint32 key = 0;
      for (; key < sizeof(smx_keymap) / sizeof(smx_keymap[0]); key += 1) {
        if (keys[smx_keymap[key].scancode]) {
          joy = (Uint16)(joy | smx_keymap[key].button);
        }
      }
      clock.joy = joy;
      if (!smx_clock_frame(&clock)) {
        SDL_Log("cpu stopped at %u:%u", (unsigned)cpu.pb, (unsigned)cpu.pc);
        playing = false;
      }
      if (audio != NULL) {
        /* About 533 frames of 32040 Hz stereo per NTSC frame. */
        Sint16 frames[1024 * 2];
        Uint32 count = smx_apu_drain(&apu, frames, 1024);
        SDL_PutAudioStreamData(audio, frames, (int)(count * 2u * sizeof(Sint16)));
      }
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

  if (audio != NULL) {
    SDL_DestroyAudioStream(audio);
  }
  if (texture != NULL) {
    SDL_DestroyTexture(texture);
  }
  if (machine) {
    SDL_Log("instructions: %llu translated, %llu interpreted", (unsigned long long)cpu.translated,
            (unsigned long long)cpu.interpreted);
    smx_ppu_quit(&ppu);
    smx_bus_quit(&bus);
  }
  smx_cart_free(&cart);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
