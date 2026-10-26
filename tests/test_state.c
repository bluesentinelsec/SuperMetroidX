#include "smx/state.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("state: %s", name);
    g_failed = true;
  }
}

typedef struct Console {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxApu apu;
  SmxClock clock;
  SmxMachine machine;
} Console;

static void console_init(Console *console, const Uint8 *rom, Uint32 size) {
  smx_bus_init(&console->bus, rom, size, 0x800);
  smx_cpu_init(&console->cpu, &console->bus);
  smx_dma_init(&console->dma, &console->bus);
  smx_ppu_init(&console->ppu, &console->bus);
  smx_apu_init(&console->apu, &console->bus);
  smx_clock_init(&console->clock, &console->cpu, &console->dma);
  console->clock.ppu = &console->ppu;
  smx_cpu_reset(&console->cpu);
  console->machine.bus = &console->bus;
  console->machine.cpu = &console->cpu;
  console->machine.dma = &console->dma;
  console->machine.ppu = &console->ppu;
  console->machine.apu = &console->apu;
  console->machine.clock = &console->clock;
}

static void console_quit(Console *console) {
  smx_ppu_quit(&console->ppu);
  smx_bus_quit(&console->bus);
}

static void run_frames(Console *console, Uint32 frames) {
  for (; frames != 0; frames -= 1) {
    smx_clock_frame(&console->clock);
  }
}

int main(void) {
  static Uint8 rom[0x8000];
  /* Native mode, then loop: INC $10; LDA $2140; STA $11; BRA. */
  static const Uint8 code[] = {0x18, 0xFB, 0xE2, 0x30, 0xEE, 0x10, 0x00, 0xAD,
                               0x40, 0x21, 0x8D, 0x11, 0x00, 0x80, 0xF5};
  Console *a = (Console *)SDL_calloc(1, sizeof(Console));
  Console *b = (Console *)SDL_calloc(1, sizeof(Console));
  Uint32 size = smx_state_size();
  Uint8 *state = (Uint8 *)SDL_malloc(size);
  Uint8 *sram = NULL;

  SDL_memcpy(rom, code, sizeof(code));
  rom[0x7FFC] = 0x00;
  rom[0x7FFD] = 0x80;
  console_init(a, rom, sizeof(rom));
  console_init(b, rom, sizeof(rom));
  b->bus.sram[0] = 0x5A;
  sram = b->bus.sram;

  run_frames(a, 3);
  smx_state_capture(&a->machine, 0x1234u, state);
  run_frames(a, 2);

  expect_true("other rom refused", !smx_state_restore(&b->machine, 0x9999u, state, size));
  expect_true("short state refused", !smx_state_restore(&b->machine, 0x1234u, state, size - 1u));
  expect_true("restored", smx_state_restore(&b->machine, 0x1234u, state, size));
  expect_true("sram kept", b->bus.sram == sram && b->bus.sram[0] == 0x5A);
  expect_true("pointers kept", b->cpu.bus == &b->bus && b->clock.cpu == &b->cpu &&
                                   b->apu.dsp.ram == b->apu.ram && b->dma.now == &b->clock.dots);
  run_frames(b, 2);

  expect_true("same wram", SDL_memcmp(a->bus.wram, b->bus.wram, SMX_BUS_WRAM_SIZE) == 0);
  expect_true("counter ran", a->bus.wram[0x10] != 0);
  expect_true("same cpu", a->cpu.pc == b->cpu.pc && a->cpu.a == b->cpu.a && a->cpu.p == b->cpu.p);
  expect_true("same time", a->clock.dots == b->clock.dots && a->clock.frame == b->clock.frame);
  expect_true("same spc",
              a->apu.core.cycle == b->apu.core.cycle && a->apu.core.pc == b->apu.core.pc);
  expect_true("same picture", SDL_memcmp(a->ppu.pixels, b->ppu.pixels,
                                         (size_t)SMX_PPU_WIDTH * SMX_PPU_HEIGHT * 4u) == 0);

  console_quit(a);
  console_quit(b);
  SDL_free(a);
  SDL_free(b);
  SDL_free(state);
  return g_failed ? 1 : 0;
}
