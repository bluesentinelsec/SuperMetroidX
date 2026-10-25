#include "smx/clock.h"
#include "smx/apu.h"
#include "smx/ppu.h"

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

static void smx_clock_irq(SmxClock *clock) {
  clock->bus->mmio[0x4211] = 0x80;
  smx_cpu_irq(clock->cpu);
}

static void smx_clock_advance(SmxClock *clock, Uint32 dots) {
  Uint32 line_before = smx_clock_line(clock->dots);
  Uint32 line_after = 0;
  bool vblank = false;

  clock->dots += dots;
  line_after = smx_clock_line(clock->dots);
  if (line_before != line_after && clock->ppu != NULL && line_before >= 1u &&
      line_before <= SMX_PPU_HEIGHT) {
    smx_ppu_present_line(clock->ppu, line_before);
  }
  if (clock->dma != NULL && line_before != line_after && line_after > 0 &&
      line_after < SMX_CLOCK_VBLANK_LINE) {
    smx_dma_hdma_line(clock->dma);
  }
  vblank = line_after >= SMX_CLOCK_VBLANK_LINE;
  {
    Uint32 frame_dot = clock->dots % SMX_CLOCK_DOTS_PER_FRAME;
    Uint32 vblank_at = SMX_CLOCK_VBLANK_LINE * SMX_CLOCK_DOTS_PER_LINE;
    Uint8 status = (Uint8)(clock->bus->mmio[0x4212] & (Uint8)~0x81u);
    /* Auto-joypad read holds bit 0 for 4224 master clocks from vblank. */
    if (vblank) {
      status = (Uint8)(status | 0x80);
    }
    if (frame_dot >= vblank_at && frame_dot < vblank_at + 4224u) {
      status = (Uint8)(status | 0x01);
    }
    clock->bus->mmio[0x4212] = status;
  }
  if (!clock->prev_vblank && vblank) {
    clock->bus->mmio[0x4210] = (Uint8)(clock->bus->mmio[0x4210] | 0x80);
    if ((clock->bus->mmio[0x4200] & 0x01) != 0) {
      clock->bus->mmio[0x4218] = (Uint8)clock->joy;
      clock->bus->mmio[0x4219] = (Uint8)(clock->joy >> 8);
    }
    if ((clock->bus->mmio[0x4200] & 0x80) != 0) {
      smx_cpu_nmi(clock->cpu);
    }
  }
  if (line_before != line_after) {
    Uint16 vtime = (Uint16)(clock->bus->mmio[0x4209] | ((Uint16)clock->bus->mmio[0x420A] << 8));
    bool v_irq = (clock->bus->mmio[0x4200] & 0x20) != 0;
    bool h_irq = (clock->bus->mmio[0x4200] & 0x10) != 0;
    if (v_irq && !h_irq && line_after == (vtime & 0x01FFu)) {
      smx_clock_irq(clock);
    }
  }
  {
    Uint32 step = 0;
    Uint16 htime = (Uint16)(clock->bus->mmio[0x4207] | ((Uint16)clock->bus->mmio[0x4208] << 8));
    Uint16 vtime = (Uint16)(clock->bus->mmio[0x4209] | ((Uint16)clock->bus->mmio[0x420A] << 8));
    bool h_irq = (clock->bus->mmio[0x4200] & 0x10) != 0;
    bool v_irq = (clock->bus->mmio[0x4200] & 0x20) != 0;
    htime = (Uint16)(htime & 0x01FFu);
    for (; h_irq && step < dots; step += 1) {
      Uint32 at = clock->dots - dots + step;
      Uint32 line = smx_clock_line(at);
      Uint32 dot = (at % SMX_CLOCK_DOTS_PER_FRAME) % SMX_CLOCK_DOTS_PER_LINE;
      if (dot == htime && (!v_irq || line == (vtime & 0x01FFu))) {
        smx_clock_irq(clock);
      }
    }
  }
  clock->prev_vblank = vblank;
}

bool smx_clock_step(SmxClock *clock) {
  bool ran = false;

  if (clock->dma != NULL && (clock->dots % SMX_CLOCK_DOTS_PER_FRAME) == 0) {
    smx_dma_hdma_init(clock->dma);
    smx_dma_hdma_line(clock->dma);
  }
  ran = smx_cpu_step(clock->cpu);

  if (clock->bus->dma_requested) {
    smx_dma_run(clock->dma, clock->bus->dma_channels);
    clock->bus->mmio[0x420B] = 0;
    clock->bus->dma_requested = false;
    clock->dma_count += 1;
  }
  /* Three fast-ROM cycles. A fast cycle is 6 master clocks. */
  smx_clock_advance(clock, 18);
  if (clock->bus->apu_user != NULL) {
    /* Four SPC cycles at 21 master clocks per cycle. */
    clock->apu_dots += 18u;
    while (clock->apu_dots >= 84u) {
      clock->apu_dots -= 84u;
      smx_apu_step((SmxApu *)clock->bus->apu_user);
    }
  }
  return ran;
}

bool smx_clock_frame(SmxClock *clock) {
  Uint32 target = clock->dots + SMX_CLOCK_DOTS_PER_FRAME;
  bool ran = true;

  while (ran && clock->dots < target) {
    ran = smx_clock_step(clock);
  }
  if (clock->ppu != NULL) {
    smx_ppu_note_picture(clock->ppu);
  }
  return ran;
}
