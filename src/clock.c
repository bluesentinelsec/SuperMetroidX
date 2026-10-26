#include "smx/clock.h"
#include "smx/apu.h"
#include "smx/ppu.h"

static void smx_clock_sync(void *user);
static void smx_clock_apu_sync(void *user);
static void smx_clock_time_sync(void *user);

void smx_clock_init(SmxClock *clock, SmxCpu *cpu, SmxDma *dma) {
  SDL_memset(clock, 0, sizeof(*clock));
  clock->cpu = cpu;
  clock->dma = dma;
  clock->bus = cpu->bus;
  clock->prev_vblank = false;
  if (clock->bus != NULL) {
    clock->bus->ppu_sync = smx_clock_sync;
    clock->bus->apu_sync = smx_clock_apu_sync;
    clock->bus->time_sync = smx_clock_time_sync;
    clock->bus->sync_user = clock;
  }
  /* The PPU comes out of reset forced blank. */
  if (clock->bus != NULL) {
    clock->bus->mmio[0x2100] = 0x8F;
  }
}

/* Line 240 of every odd field is four master clocks short unless interlaced. */
static Uint32 smx_clock_line_length(const SmxClock *clock, Uint32 line) {
  bool interlace = (clock->bus->mmio[0x2133] & 0x01) != 0;

  if (line == SMX_CLOCK_SHORT_LINE && clock->field && !interlace) {
    return SMX_CLOCK_DOTS_PER_LINE - 4u;
  }
  return SMX_CLOCK_DOTS_PER_LINE;
}

/* TIMEUP holds the IRQ line until `$4211` is read. `at` is when it rose. */
static void smx_clock_irq(SmxClock *clock, Uint64 at) {
  if ((clock->bus->mmio[0x4211] & 0x80) == 0) {
    clock->bus->mmio[0x4211] = 0x80;
    clock->irq_edge = at;
  }
}

/* `dots` must not run past the end of the current line. */
static void smx_clock_span(SmxClock *clock, Uint32 dots) {
  Uint32 line_before = clock->line;
  Uint32 dot_before = clock->dot;
  Uint32 line_after = 0;
  bool vblank = false;

  clock->dots += dots;
  clock->dot += dots;
  if (clock->dot >= smx_clock_line_length(clock, line_before)) {
    clock->dot = 0;
    clock->line += 1;
    if (clock->line == SMX_CLOCK_LINES) {
      clock->line = 0;
      clock->frame += 1;
      clock->field = !clock->field;
    }
  }
  line_after = clock->line;
  {
    /* Refresh follows the CPU's 8-clock phase: dot 538, or 534 when the line starts 4 off. */
    Uint32 refresh = SMX_CLOCK_REFRESH_DOT - (Uint32)((clock->dots - dots - dot_before) & 7u);
    if (dot_before < refresh && dot_before + dots >= refresh) {
      clock->stall += SMX_CLOCK_REFRESH_DOTS;
    }
  }
  if (line_before != line_after && clock->ppu != NULL && line_before >= 1u &&
      line_before <= SMX_PPU_HEIGHT) {
    smx_ppu_present_span(clock->ppu, line_before, clock->drawn_x, SMX_PPU_WIDTH);
  }
  if (line_before != line_after) {
    clock->drawn_x = 0;
  }
  if (clock->dma != NULL && line_before != line_after) {
    if (line_after == 0) {
      clock->stall += smx_dma_hdma_init(clock->dma);
    } else if (line_after < SMX_CLOCK_VBLANK_LINE) {
      clock->stall += smx_dma_hdma_line(clock->dma);
    }
  }
  vblank = line_after >= SMX_CLOCK_VBLANK_LINE;
  {
    Uint8 status = (Uint8)(clock->bus->mmio[0x4212] & (Uint8)~0x81u);
    /* Auto-joypad read holds bit 0 for 4224 master clocks from vblank. */
    if (vblank) {
      status = (Uint8)(status | 0x80);
    }
    if (vblank &&
        (line_after - SMX_CLOCK_VBLANK_LINE) * SMX_CLOCK_DOTS_PER_LINE + clock->dot < 4224u) {
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
      clock->nmi_pending = true;
      clock->nmi_edge = clock->dots;
    }
  }
  if (line_before != line_after) {
    Uint16 vtime = (Uint16)(clock->bus->mmio[0x4209] | ((Uint16)clock->bus->mmio[0x420A] << 8));
    bool v_irq = (clock->bus->mmio[0x4200] & 0x20) != 0;
    bool h_irq = (clock->bus->mmio[0x4200] & 0x10) != 0;
    if (v_irq && !h_irq && line_after == (vtime & 0x01FFu)) {
      smx_clock_irq(clock, clock->dots);
    }
  }
  {
    Uint16 htime = (Uint16)(clock->bus->mmio[0x4207] | ((Uint16)clock->bus->mmio[0x4208] << 8));
    Uint16 vtime = (Uint16)(clock->bus->mmio[0x4209] | ((Uint16)clock->bus->mmio[0x420A] << 8));
    bool h_irq = (clock->bus->mmio[0x4200] & 0x10) != 0;
    bool v_irq = (clock->bus->mmio[0x4200] & 0x20) != 0;
    /* HTIME counts PPU dots, four master clocks each. */
    Uint32 at = (Uint32)(htime & 0x01FFu) * 4u;
    if (h_irq && at >= dot_before && at < dot_before + dots &&
        (!v_irq || line_before == (vtime & 0x01FFu))) {
      smx_clock_irq(clock, clock->dots - dots + (at - dot_before));
    }
  }
  clock->prev_vblank = vblank;
}

static void smx_clock_advance(SmxClock *clock, Uint32 dots) {
  while (dots != 0) {
    Uint32 chunk = smx_clock_line_length(clock, clock->line) - clock->dot;
    if (chunk > dots) {
      chunk = dots;
    }
    smx_clock_span(clock, chunk);
    dots -= chunk;
  }
}

/* Run the SPC700 until it has caught up to master clock `now`. */
static void smx_clock_apu_to(SmxClock *clock, Uint64 now) {
  SmxApu *apu = (SmxApu *)clock->bus->apu_user;

  if (apu == NULL || now <= clock->apu_at) {
    return;
  }
  clock->apu_credit += (Sint64)(now - clock->apu_at) * SMX_CLOCK_SPC_HZ;
  clock->apu_at = now;
  /* Run only instructions that finish by `now`, so a port write never shows early. */
  while (clock->apu_credit >= (Sint64)smx_apu_next_cycles(apu) * SMX_CLOCK_MASTER_HZ) {
    if (!smx_apu_step(apu)) {
      clock->apu_credit = 0;
      return;
    }
    clock->apu_credit -= (Sint64)apu->cycles * SMX_CLOCK_MASTER_HZ;
  }
}

static void smx_clock_spend(SmxClock *clock, Uint32 dots) {
  smx_clock_advance(clock, dots);
  smx_clock_apu_to(clock, clock->dots);
}

/* Clocks the current instruction has spent that the clock has not caught up to. */
static Uint32 smx_clock_owed(const SmxClock *clock) {
  return clock->cpu->clocks - clock->cpu_done;
}

/* Spend `dots`, then any refresh or HDMA stall that came due meanwhile. */
static void smx_clock_pay(SmxClock *clock, Uint32 dots) {
  smx_clock_spend(clock, dots);
  while (clock->stall != 0) {
    dots = clock->stall;
    clock->stall = 0;
    smx_clock_spend(clock, dots);
  }
}

/* The CPU samples the bus four clocks before an access cycle ends. */
static void smx_clock_apu_sync(void *user) {
  SmxClock *clock = (SmxClock *)user;
  smx_clock_apu_to(clock, clock->dots + smx_clock_owed(clock) - 4u);
}

/* Bring time up to an access whose result depends on the beam or the timers. */
static void smx_clock_time_sync(void *user) {
  SmxClock *clock = (SmxClock *)user;
  Uint32 owed = smx_clock_owed(clock);

  clock->cpu_done = clock->cpu->clocks;
  smx_clock_spend(clock, owed);
}

/* Draw the current line up to the beam before a PPU register changes. */
static void smx_clock_sync(void *user) {
  SmxClock *clock = (SmxClock *)user;
  Uint32 line = clock->line;
  Uint32 dot = clock->dot + smx_clock_owed(clock);
  Uint32 x = SMX_PPU_WIDTH;

  if (clock->ppu == NULL || line < 1u || line > SMX_PPU_HEIGHT) {
    return;
  }
  if (dot < smx_clock_line_length(clock, line)) {
    x = dot / 4u < SMX_CLOCK_FIRST_PIXEL_DOT ? 0 : dot / 4u - SMX_CLOCK_FIRST_PIXEL_DOT;
    x = x > SMX_PPU_WIDTH ? SMX_PPU_WIDTH : x;
  }
  if (x > clock->drawn_x) {
    smx_ppu_present_span(clock->ppu, line, clock->drawn_x, x);
    clock->drawn_x = x;
  }
}

/*
 * After an instruction, take an interrupt whose edge came no later than
 * `deadline`, the start of that instruction's second-to-last cycle.
 */
static void smx_clock_interrupts(SmxClock *clock, Uint64 deadline) {
  if (clock->nmi_pending && clock->nmi_edge <= deadline) {
    clock->nmi_pending = false;
    smx_cpu_nmi(clock->cpu);
    return;
  }
  if ((clock->bus->mmio[0x4211] & 0x80) != 0 && clock->irq_edge <= deadline) {
    smx_cpu_irq(clock->cpu);
  }
}

bool smx_clock_step(SmxClock *clock) {
  bool ran = false;
  Uint32 dots = 0;

  /* The first frame starts on line 0 without crossing into it. */
  if (clock->dots == 0 && clock->dma != NULL) {
    clock->stall += smx_dma_hdma_init(clock->dma);
  }
  /* Refresh and HDMA found while advancing pause the CPU before it continues. */
  while (clock->stall != 0) {
    dots = clock->stall;
    clock->stall = 0;
    smx_clock_spend(clock, dots);
  }
  {
    Uint64 start = clock->dots;
    clock->cpu_done = 0;
    ran = smx_cpu_step(clock->cpu);
    dots = smx_clock_owed(clock);
    clock->cpu->clocks = 0;
    clock->cpu_done = 0;
    smx_clock_spend(clock, dots);
    /* A waiting CPU wakes as soon as the edge arrives. */
    smx_clock_interrupts(clock, clock->cpu->waiting ? clock->dots : start + clock->cpu->penult_at);
  }
  if (clock->bus->dma_requested) {
    Uint64 begin = clock->dots;
    Uint32 fetch = smx_bus_speed(clock->bus, ((Uint32)clock->cpu->pb << 16) | clock->cpu->pc);
    /*
     * The CPU starts its next opcode fetch before it halts. DMA waits for an
     * 8-clock boundary after that fetch, transfers, then waits until a whole
     * number of fetch-length cycles has passed since the fetch's read point,
     * four clocks before its end. The fetch itself is spent by the next step.
     */
    smx_clock_pay(clock, 8u - (Uint32)((clock->dots + fetch) & 7u));
    smx_clock_pay(clock, smx_dma_run(clock->dma, clock->bus->dma_channels));
    smx_clock_pay(clock, fetch - (Uint32)((clock->dots - begin + 4u) % fetch));
    clock->bus->mmio[0x420B] = 0;
    clock->bus->dma_requested = false;
    clock->dma_count += 1;
  }
  return ran;
}

bool smx_clock_frame(SmxClock *clock) {
  /* Stop at the next line 0 so a long DMA cannot move the frame boundary. */
  Uint64 frame = clock->frame;
  bool ran = true;

  while (ran && clock->frame == frame) {
    ran = smx_clock_step(clock);
  }
  if (clock->ppu != NULL) {
    smx_ppu_note_picture(clock->ppu);
  }
  return ran;
}
