#include "smx/clock.h"

void smx_clock_init(SmxClock *clock, SmxCpu *cpu, SmxDma *dma) {
  SDL_memset(clock, 0, sizeof(*clock));
  clock->cpu = cpu;
  clock->dma = dma;
  clock->bus = cpu->bus;
  clock->prev_vblank = false;
  /* The PPU comes out of reset forced blank. */
  if (clock->bus != NULL) {
    clock->bus->mmio[0x2100] = 0x8F;
  }
}

static Uint32 smx_clock_line(Uint32 dots) {
  return (dots % SMX_CLOCK_DOTS_PER_FRAME) / SMX_CLOCK_DOTS_PER_LINE;
}

static void smx_clock_advance(SmxClock *clock, Uint32 dots) {
  Uint32 line_after = 0;
  bool vblank = false;

  clock->dots += dots;
  line_after = smx_clock_line(clock->dots);
  vblank = line_after >= SMX_CLOCK_VBLANK_LINE;
  if (vblank) {
    clock->bus->mmio[0x4212] = (Uint8)(clock->bus->mmio[0x4212] | 0x80);
  } else {
    clock->bus->mmio[0x4212] = (Uint8)(clock->bus->mmio[0x4212] & 0x7F);
  }
  if (!clock->prev_vblank && vblank) {
    clock->bus->mmio[0x4210] = (Uint8)(clock->bus->mmio[0x4210] | 0x80);
    if ((clock->bus->mmio[0x4200] & 0x80) != 0) {
      smx_cpu_nmi(clock->cpu);
    }
  }
  clock->prev_vblank = vblank;
}

bool smx_clock_step(SmxClock *clock) {
  bool ran = smx_cpu_step(clock->cpu);

  if (clock->bus->dma_requested) {
    smx_dma_run(clock->dma, clock->bus->dma_channels);
    clock->bus->mmio[0x420B] = 0;
    clock->bus->dma_requested = false;
    clock->dma_count += 1;
  }
  smx_clock_advance(clock, 8);
  return ran;
}

bool smx_clock_frame(SmxClock *clock) {
  Uint32 target = clock->dots + SMX_CLOCK_DOTS_PER_FRAME;
  bool ran = true;

  while (ran && clock->dots < target) {
    ran = smx_clock_step(clock);
  }
  return ran;
}
