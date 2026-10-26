#include "smx/clock.h"
#include "smx/apu.h"
#include "smx/ppu.h"

enum { SMX_EVENT_HDMA_INIT, SMX_EVENT_REFRESH, SMX_EVENT_HDMA, SMX_EVENT_LINE_END };

/* Auto-joypad: the read takes 34 steps of 128 clocks; bits shift in from step 4. */
#define SMX_JOY_STEP 128u
#define SMX_JOY_STEPS 34u

static void smx_clock_sync(void *user);
static void smx_clock_apu_sync(void *user, bool write);
static void smx_clock_cycle_begin(void *user, Uint32 clocks);
static void smx_clock_cycle_advance(void *user, Uint32 clocks);
static bool smx_clock_interrupt_soon(void *user);
static Uint8 smx_clock_reg_read(void *user, Uint16 addr, Uint8 open_bus);
static void smx_clock_reg_write(void *user, Uint16 addr, Uint8 value);
static void smx_clock_run(SmxClock *clock, Uint32 clocks);

/* Line 240 of every odd field is four master clocks short unless interlaced. */
static Uint32 smx_clock_line_length(const SmxClock *clock, Uint32 line, bool field) {
  bool interlace = (clock->bus->mmio[0x2133] & 0x01) != 0;

  if (line == SMX_CLOCK_SHORT_LINE && field && !interlace) {
    return SMX_CLOCK_DOTS_PER_LINE - 4u;
  }
  return SMX_CLOCK_DOTS_PER_LINE;
}

/* Schedule a new line's events. Refresh and HDMA init follow the line start's 8-clock phase. */
static void smx_clock_line_start(SmxClock *clock) {
  Uint32 offset = (Uint32)(clock->dots & 7u);

  clock->refresh_at = SMX_CLOCK_REFRESH_DOT - offset;
  if (clock->line == 0) {
    clock->event = SMX_EVENT_HDMA_INIT;
    clock->event_at = SMX_CLOCK_HDMA_INIT_DOT + offset;
  } else {
    clock->event = SMX_EVENT_REFRESH;
    clock->event_at = clock->refresh_at;
  }
}

static void smx_clock_dma_advance(void *user, Uint32 clocks) {
  smx_clock_run((SmxClock *)user, clocks);
}

void smx_clock_init(SmxClock *clock, SmxCpu *cpu, SmxDma *dma) {
  SDL_memset(clock, 0, sizeof(*clock));
  clock->cpu = cpu;
  clock->dma = dma;
  clock->bus = cpu->bus;
  clock->mul_a = 0xFF;
  clock->dividend = 0xFFFF;
  cpu->cycle_begin = smx_clock_cycle_begin;
  cpu->cycle_advance = smx_clock_cycle_advance;
  cpu->interrupt_soon = smx_clock_interrupt_soon;
  cpu->cycle_user = clock;
  if (dma != NULL) {
    dma->advance = smx_clock_dma_advance;
    dma->now = &clock->dots;
    dma->user = clock;
  }
  if (clock->bus != NULL) {
    clock->bus->ppu_sync = smx_clock_sync;
    clock->bus->apu_sync = smx_clock_apu_sync;
    clock->bus->reg_read = smx_clock_reg_read;
    clock->bus->reg_write = smx_clock_reg_write;
    clock->bus->sync_user = clock;
    /* The PPU comes out of reset forced blank. */
    clock->bus->mmio[0x2100] = 0x8F;
  }
  /* Power on starts line 0 with refresh, not an HDMA reload. */
  clock->event = SMX_EVENT_REFRESH;
  clock->event_at = SMX_CLOCK_REFRESH_DOT;
  clock->refresh_at = SMX_CLOCK_REFRESH_DOT;
}

/* ---- H/V counters and IRQ ---- */

/* The V counter after the tick at `dot`: it resets on line 0 at dot 2 and counts at dot 6. */
static Uint16 smx_clock_vcounter(Uint32 line, Uint32 dot) {
  if (line == 0) {
    return dot >= 2u ? 0u : (Uint16)(SMX_CLOCK_LINES - 1u);
  }
  return (Uint16)(dot >= 6u ? line : line - 1u);
}

/* The H counter after the tick at `dot`: it resets at dots 6 and 10 and counts every 4 after. */
static Uint16 smx_clock_hcounter(const SmxClock *clock, Uint32 line, Uint32 dot) {
  if (dot >= 14u) {
    return (Uint16)((dot - 10u) / 4u);
  }
  if (dot >= 6u) {
    return 0;
  }
  {
    Uint32 prev = line == 0 ? SMX_CLOCK_LINES - 1u : line - 1u;
    bool field = line == 0 ? !clock->field : clock->field;
    return (Uint16)((smx_clock_line_length(clock, prev, field) - 12u) / 4u + 1u);
  }
}

/* Compare the counters as of the last tick with `$4207-$420A`. A new match starts the IRQ delay. */
static void smx_clock_irq_match(SmxClock *clock, Uint32 line, Uint32 dot, bool at_tick) {
  Uint8 enable = clock->bus->mmio[0x4200];
  bool h_irq = (enable & 0x10) != 0;
  bool v_irq = (enable & 0x20) != 0;
  Uint16 htime =
      (Uint16)((clock->bus->mmio[0x4207] | ((Uint16)clock->bus->mmio[0x4208] << 8)) & 0x1FFu);
  Uint16 vtime =
      (Uint16)((clock->bus->mmio[0x4209] | ((Uint16)clock->bus->mmio[0x420A] << 8)) & 0x1FFu);
  bool match = false;

  if (!h_irq && !v_irq) {
    clock->irq_match = false;
    return;
  }
  match = (!h_irq || htime == smx_clock_hcounter(clock, line, dot)) &&
          (!v_irq || vtime == smx_clock_vcounter(line, dot));
  if (!clock->irq_match && match) {
    clock->irq_delay = (Uint8)(h_irq && at_tick && dot == 6u ? 3u : 2u);
  }
  clock->irq_match = match;
}

/* The beam position of the last tick, at or before now. */
static void smx_clock_last_tick(const SmxClock *clock, Uint32 *line, Uint32 *dot) {
  if (clock->dot >= 2u) {
    *line = clock->line;
    *dot = clock->dot - ((clock->dot - 2u) & 3u);
    return;
  }
  *line = clock->line == 0 ? SMX_CLOCK_LINES - 1u : clock->line - 1u;
  *dot = smx_clock_line_length(clock, *line, clock->line == 0 ? !clock->field : clock->field) - 2u;
}

static void smx_clock_irq_update(SmxClock *clock) {
  Uint32 line = 0;
  Uint32 dot = 0;

  smx_clock_last_tick(clock, &line, &dot);
  smx_clock_irq_match(clock, line, dot, false);
}

static bool smx_clock_irq_active(const SmxClock *clock) {
  return (clock->bus->mmio[0x4200] & 0x30) != 0 || clock->irq_delay != 0;
}

/* The counters tick every 4 master clocks, at dots 2, 6, 10, and on. */
static void smx_clock_tick(SmxClock *clock) {
  if (clock->irq_delay != 0) {
    clock->irq_delay -= 1;
    if (clock->irq_delay == 1) {
      clock->irq_flag = true;
    } else if (clock->irq_delay == 0) {
      clock->irq_line = clock->irq_flag;
    }
  }
  if (clock->dot == 2u) {
    if (clock->line == SMX_CLOCK_VBLANK_LINE) {
      clock->nmi_flag = true;
    } else if (clock->line == 0) {
      clock->nmi_flag = false;
    }
  } else if (clock->dot == 6u && clock->line == SMX_CLOCK_VBLANK_LINE &&
             (clock->bus->mmio[0x4200] & 0x80) != 0) {
    clock->nmi_count = 1;
  }
  smx_clock_irq_match(clock, clock->line, clock->dot, true);
}

/* ---- Auto-joypad ---- */

static Uint16 smx_clock_joy_value(const SmxClock *clock) {
  Uint64 step = 0;

  if (!clock->joy_enabled || clock->dots < clock->joy_start + SMX_JOY_STEP) {
    return (Uint16)(clock->bus->mmio[0x4218] | ((Uint16)clock->bus->mmio[0x4219] << 8));
  }
  step = (clock->dots - clock->joy_start) / SMX_JOY_STEP;
  if (step >= SMX_JOY_STEPS) {
    return clock->joy_latched;
  }
  /* Even steps from 4 shift one more button in, B first. */
  step = step < 4u ? 0u : (step - 2u) / 2u;
  return step == 0 ? 0 : (Uint16)(clock->joy_latched >> (16u - step));
}

static bool smx_clock_joy_busy(const SmxClock *clock) {
  return clock->joy_enabled && clock->dots >= clock->joy_start + SMX_JOY_STEP &&
         clock->dots < clock->joy_start + SMX_JOY_STEP * SMX_JOY_STEPS;
}

/* Vblank: finish last frame's read and start this one at the next 256-clock step. */
static void smx_clock_joy_start(SmxClock *clock) {
  Uint64 from = clock->dots + 130u;

  if (clock->joy_enabled) {
    clock->bus->mmio[0x4218] = (Uint8)clock->joy_latched;
    clock->bus->mmio[0x4219] = (Uint8)(clock->joy_latched >> 8);
  }
  clock->joy_start = from + ((from & 0xFFu) != 0 ? 256u - (from & 0xFFu) : 0u) - SMX_JOY_STEP;
  clock->joy_latched = clock->joy;
  clock->joy_enabled = (clock->bus->mmio[0x4200] & 0x01) != 0;
}

/* ---- Lines and events ---- */

static void smx_clock_line_end(SmxClock *clock) {
  if (clock->ppu != NULL) {
    clock->ppu->beam_line = &clock->line;
  }
  Uint32 line_before = clock->line;

  if (clock->ppu != NULL && line_before >= 1u && line_before <= SMX_PPU_HEIGHT) {
    smx_ppu_present_span(clock->ppu, line_before, clock->drawn_x, SMX_PPU_WIDTH);
  }
  clock->drawn_x = 0;
  clock->dot = 0;
  clock->line += 1;
  if (clock->line == SMX_CLOCK_LINES) {
    clock->line = 0;
    clock->frame += 1;
    clock->field = !clock->field;
  }
  if (clock->line == SMX_CLOCK_VBLANK_LINE) {
    smx_clock_joy_start(clock);
  }
  smx_clock_line_start(clock);
}

/* Returns the clocks the event adds to the current pause. */
static Uint32 smx_clock_event(SmxClock *clock) {
  switch (clock->event) {
  case SMX_EVENT_HDMA_INIT:
    if (clock->dma != NULL) {
      smx_dma_request_hdma_init(clock->dma);
    }
    clock->event = SMX_EVENT_REFRESH;
    clock->event_at = clock->refresh_at;
    return 0;
  case SMX_EVENT_REFRESH:
    if (clock->line < SMX_CLOCK_VBLANK_LINE) {
      clock->event = SMX_EVENT_HDMA;
      clock->event_at = SMX_CLOCK_HDMA_DOT;
    } else {
      clock->event = SMX_EVENT_LINE_END;
      clock->event_at = smx_clock_line_length(clock, clock->line, clock->field);
    }
    return SMX_CLOCK_REFRESH_DOTS;
  case SMX_EVENT_HDMA:
    if (clock->dma != NULL) {
      smx_dma_request_hdma(clock->dma);
    }
    clock->event = SMX_EVENT_LINE_END;
    clock->event_at = smx_clock_line_length(clock, clock->line, clock->field);
    return 0;
  default:
    smx_clock_line_end(clock);
    return 0;
  }
}

/* The next dot after now where something happens. */
static Uint32 smx_clock_next_point(const SmxClock *clock) {
  Uint32 next = clock->event_at;
  Uint32 dot = clock->dot;

  if (smx_clock_irq_active(clock)) {
    Uint32 tick = dot < 2u ? 2u : dot + 4u - ((dot - 2u) & 3u);
    next = tick < next ? tick : next;
  } else if (clock->line == SMX_CLOCK_VBLANK_LINE && dot < 6u) {
    Uint32 tick = dot < 2u ? 2u : 6u;
    next = tick < next ? tick : next;
  } else if (clock->line == 0 && dot < 2u) {
    next = 2u < next ? 2u : next;
  }
  return next;
}

/* Let `clocks` master clocks pass, handling each event and tick on the way. */
static void smx_clock_run(SmxClock *clock, Uint32 clocks) {
  while (clocks != 0) {
    Uint32 next = smx_clock_next_point(clock);
    Uint32 step = next - clock->dot;
    if (step > clocks) {
      clock->dot += clocks;
      clock->dots += clocks;
      return;
    }
    clock->dot += step;
    clock->dots += step;
    clocks -= step;
    if (clock->dot == clock->event_at) {
      clocks += smx_clock_event(clock);
    }
    if (clock->dot != 0 && (clock->dot & 3u) == 2u &&
        (smx_clock_irq_active(clock) || clock->line == SMX_CLOCK_VBLANK_LINE || clock->line == 0)) {
      smx_clock_tick(clock);
    }
  }
}

/* ---- The CPU's view ---- */

/* A CPU cycle starts: pending DMA runs, then the CPU samples NMI and IRQ. */
static void smx_clock_cycle_begin(void *user, Uint32 clocks) {
  SmxClock *clock = (SmxClock *)user;
  bool lock = false;

  clock->cpu_cycles += 1;
  if (clock->dma != NULL && smx_dma_pending(clock->dma)) {
    bool gp = clock->dma->gp;
    clock->dma->speed = (Uint8)clocks;
    lock = smx_dma_service(clock->dma);
    if (lock && gp && !clock->dma->gp) {
      clock->dma_count += 1;
    }
  }
  if (clock->nmi_count != 0) {
    clock->nmi_count -= 1;
    if (clock->nmi_count == 0) {
      if (!lock) {
        clock->need_nmi = true;
        clock->cpu->wai_over = true;
      } else {
        /* An NMI that arrives as DMA finishes waits one more cycle. */
        clock->nmi_count = 1;
      }
    }
  }
  if (!lock) {
    clock->prev_irq = clock->irq_line && (clock->cpu->p & 0x04) == 0;
    if (clock->irq_line) {
      clock->cpu->wai_over = true;
    }
  } else {
    clock->prev_irq = false;
  }
}

static void smx_clock_cycle_advance(void *user, Uint32 clocks) {
  smx_clock_run((SmxClock *)user, clocks);
}

static bool smx_clock_interrupt_soon(void *user) {
  const SmxClock *clock = (const SmxClock *)user;

  return ((clock->irq_line || clock->prev_irq) && (clock->cpu->p & 0x04) == 0) ||
         clock->nmi_count == 1 || clock->need_nmi;
}

/* Run the SPC700 up to master clock `now`. */
static void smx_clock_apu_to(SmxClock *clock, Uint64 now) {
  SmxApu *apu = (SmxApu *)clock->bus->apu_user;

  if (apu == NULL || now <= clock->apu_at) {
    return;
  }
  clock->apu_at = now;
  smx_apu_run(apu, now * SMX_CLOCK_SPC_HZ);
}

/* The bus calls this at the access, which is already the current time. */
static void smx_clock_apu_sync(void *user, bool write) {
  SmxClock *clock = (SmxClock *)user;
  SmxApu *apu = (SmxApu *)clock->bus->apu_user;

  (void)write;
  smx_clock_apu_to(clock, clock->dots);
  if (apu != NULL) {
    smx_apu_cpu_sync(apu, clock->dots * SMX_CLOCK_SPC_HZ);
  }
}

/* Draw the current line up to the beam before a PPU register changes. */
static void smx_clock_sync(void *user) {
  SmxClock *clock = (SmxClock *)user;
  Uint32 x = SMX_PPU_WIDTH;

  if (clock->ppu == NULL || clock->line < 1u || clock->line > SMX_PPU_HEIGHT) {
    return;
  }
  x = clock->dot / 4u < SMX_CLOCK_FIRST_PIXEL_DOT ? 0 : clock->dot / 4u - SMX_CLOCK_FIRST_PIXEL_DOT;
  x = x > SMX_PPU_WIDTH ? SMX_PPU_WIDTH : x;
  if (x > clock->drawn_x) {
    smx_ppu_present_span(clock->ppu, clock->line, clock->drawn_x, x);
    clock->drawn_x = x;
  }
}

/* ---- Multiply and divide ---- */

/* Each unit step takes one CPU cycle. A read lands a cycle before a write would. */
static void smx_clock_alu_run(SmxClock *clock, bool read) {
  Uint64 target = clock->cpu_cycles - (read ? 1u : 0u);

  while (clock->alu_cycle < target && (clock->mul_left != 0 || clock->div_left != 0)) {
    clock->alu_cycle += 1;
    if (clock->mul_left != 0) {
      clock->mul_left -= 1;
      if ((clock->quotient & 1u) != 0) {
        clock->product = (Uint16)(clock->product + clock->alu_shift);
      }
      clock->alu_shift <<= 1;
      clock->quotient >>= 1;
    }
    if (clock->div_left != 0) {
      clock->div_left -= 1;
      clock->alu_shift >>= 1;
      clock->quotient = (Uint16)(clock->quotient << 1);
      if (clock->product >= clock->alu_shift) {
        clock->product = (Uint16)(clock->product - clock->alu_shift);
        clock->quotient |= 1u;
      }
    }
  }
  if (clock->alu_cycle < target) {
    clock->alu_cycle = target;
  }
}

static void smx_clock_alu_write(SmxClock *clock, Uint16 addr, Uint8 value) {
  bool busy = false;

  smx_clock_alu_run(clock, true);
  /* An operation finishing on this cycle still blocks a new one. */
  busy = clock->mul_left != 0 || clock->div_left != 0;
  smx_clock_alu_run(clock, false);
  switch (addr) {
  case 0x4202:
    clock->mul_a = value;
    break;
  case 0x4203:
    clock->product = 0;
    if (!busy) {
      clock->mul_left = 8;
      clock->quotient = (Uint16)(((Uint16)value << 8) | clock->mul_a);
      clock->alu_shift = value;
    } else if (clock->mul_left == 0 && clock->div_left == 0) {
      clock->quotient = (Uint16)(((Uint16)value << 8) | clock->mul_a);
    }
    break;
  case 0x4204:
    clock->dividend = (Uint16)((clock->dividend & 0xFF00u) | value);
    break;
  case 0x4205:
    clock->dividend = (Uint16)((clock->dividend & 0x00FFu) | ((Uint16)value << 8));
    break;
  default:
    clock->product = clock->dividend;
    if (!busy) {
      clock->div_left = 16;
      clock->alu_shift = (Uint32)value << 16;
    }
    break;
  }
}

/* ---- Registers ---- */

static Uint8 smx_clock_reg_read(void *user, Uint16 addr, Uint8 open_bus) {
  SmxClock *clock = (SmxClock *)user;
  Uint8 value = open_bus;

  switch (addr) {
  case 0x4210:
    value = (Uint8)((clock->nmi_flag ? 0x80 : 0) | 0x02 | (open_bus & 0x70));
    /* The flag holds for four clocks after it rises. */
    if (clock->nmi_flag && (clock->dot >= 6u || clock->line != SMX_CLOCK_VBLANK_LINE)) {
      clock->nmi_flag = false;
    }
    return value;
  case 0x4211:
    value = (Uint8)((clock->irq_flag ? 0x80 : 0) | (open_bus & 0x7F));
    if (clock->irq_flag && clock->irq_delay == 0) {
      clock->irq_flag = false;
      clock->irq_line = false;
    }
    return value;
  case 0x4212:
    return (Uint8)((clock->line >= SMX_CLOCK_VBLANK_LINE ? 0x80 : 0) |
                   (clock->dot >= 4u && clock->dot <= 1096u ? 0 : 0x40) |
                   (smx_clock_joy_busy(clock) ? 0x01 : 0) | (open_bus & 0x3E));
  case 0x4213:
    return clock->bus->mmio[0x4201];
  case 0x4214:
    smx_clock_alu_run(clock, true);
    return (Uint8)clock->quotient;
  case 0x4215:
    smx_clock_alu_run(clock, true);
    return (Uint8)(clock->quotient >> 8);
  case 0x4216:
    smx_clock_alu_run(clock, true);
    return (Uint8)clock->product;
  case 0x4217:
    smx_clock_alu_run(clock, true);
    return (Uint8)(clock->product >> 8);
  case 0x4218:
    return (Uint8)smx_clock_joy_value(clock);
  case 0x4219:
    return (Uint8)(smx_clock_joy_value(clock) >> 8);
  default:
    return addr >= 0x421A ? 0 : open_bus;
  }
}

static void smx_clock_reg_write(void *user, Uint16 addr, Uint8 value) {
  SmxClock *clock = (SmxClock *)user;
  Uint8 before = clock->bus->mmio[addr];

  clock->bus->mmio[addr] = value;
  switch (addr) {
  case 0x4200:
    /* Turning NMI on during vblank, with the flag still set, signals one at once. */
    if (clock->nmi_flag && (value & 0x80) != 0 && (before & 0x80) == 0) {
      clock->nmi_count = 2;
    }
    if ((value & 0x30) == 0) {
      clock->irq_flag = false;
      clock->irq_line = false;
    }
    smx_clock_irq_update(clock);
    break;
  case 0x4202:
  case 0x4203:
  case 0x4204:
  case 0x4205:
  case 0x4206:
    smx_clock_alu_write(clock, addr, value);
    break;
  case 0x4207:
  case 0x4208:
  case 0x4209:
  case 0x420A:
    smx_clock_irq_update(clock);
    break;
  case 0x420B:
    if (clock->dma != NULL) {
      smx_dma_request(clock->dma, value);
    }
    break;
  default:
    break;
  }
}

void smx_clock_seek(SmxClock *clock, Uint32 line, Uint32 dot) {
  clock->line = line;
  clock->dot = 0;
  clock->dots =
      (Uint64)clock->frame * SMX_CLOCK_DOTS_PER_FRAME + (Uint64)line * SMX_CLOCK_DOTS_PER_LINE;
  smx_clock_line_start(clock);
  /* Events already passed on this line are skipped, so a seek mid-line has no refresh or HDMA. */
  while (clock->event != SMX_EVENT_LINE_END && clock->event_at <= dot) {
    if (clock->event == SMX_EVENT_REFRESH) {
      clock->event = clock->line < SMX_CLOCK_VBLANK_LINE ? SMX_EVENT_HDMA : SMX_EVENT_LINE_END;
      clock->event_at = clock->event == SMX_EVENT_HDMA
                            ? SMX_CLOCK_HDMA_DOT
                            : smx_clock_line_length(clock, clock->line, clock->field);
    } else if (clock->event == SMX_EVENT_HDMA) {
      clock->event = SMX_EVENT_LINE_END;
      clock->event_at = smx_clock_line_length(clock, clock->line, clock->field);
    } else {
      clock->event = SMX_EVENT_REFRESH;
      clock->event_at = clock->refresh_at;
    }
  }
  clock->dot = dot;
  clock->dots += dot;
}

/* ---- Steps ---- */

static void smx_clock_begin(SmxClock *clock) {
  clock->start = clock->dots;
}

/* After an instruction: take an NMI, else an IRQ seen at its last cycle start. True when taken. */
static bool smx_clock_end(SmxClock *clock) {
  bool taken = false;

  clock->cpu->clocks = 0;
  if (!clock->cpu->waiting && !clock->cpu->stopped) {
    if (clock->need_nmi) {
      clock->need_nmi = false;
      smx_cpu_nmi(clock->cpu);
      taken = true;
    } else if (clock->prev_irq) {
      smx_cpu_irq(clock->cpu);
      taken = true;
    }
  }
  smx_clock_apu_to(clock, clock->dots);
  return taken;
}

/*
 * Between two translated instructions: finish the first, and start the
 * second unless an interrupt, a frame boundary, or WAI/STP ends the block.
 */
static bool smx_clock_next(void *user) {
  SmxClock *clock = (SmxClock *)user;

  if (smx_clock_end(clock) || clock->frame != clock->step_frame || clock->cpu->waiting ||
      clock->cpu->stopped) {
    clock->ended = true;
    return false;
  }
  smx_clock_begin(clock);
  return true;
}

bool smx_clock_step(SmxClock *clock) {
  bool ran = false;

  clock->cpu->next = smx_clock_next;
  clock->cpu->next_user = clock;
  clock->step_frame = clock->frame;
  clock->ended = false;
  smx_clock_begin(clock);
  ran = smx_cpu_run(clock->cpu);
  if (!clock->ended) {
    smx_clock_end(clock);
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
