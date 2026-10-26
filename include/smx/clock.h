#ifndef SMX_CLOCK_H_
#define SMX_CLOCK_H_

#include "smx/cpu.h"
#include "smx/dma.h"
#include "smx/ppu.h"

#include <SDL3/SDL.h>

/** Master clocks in one NTSC scanline. */
#define SMX_CLOCK_DOTS_PER_LINE 1364u
/** NTSC frames are 262 lines, with vblank starting on line 225. */
#define SMX_CLOCK_LINES 262u
#define SMX_CLOCK_VBLANK_LINE 225u
/** On odd fields without interlace this line is 1360 master clocks. */
#define SMX_CLOCK_SHORT_LINE 240u
/** Master clocks in a frame without the short line. */
#define SMX_CLOCK_DOTS_PER_FRAME (SMX_CLOCK_DOTS_PER_LINE * SMX_CLOCK_LINES)

/**
 * NTSC master clock and the SPC700 cycle rate. The APU crystal is nominally
 * 24.576 MHz, but consoles average 32040 output samples a second, which is
 * 1.02528 MHz at 32 cycles a sample. Mesen and bsnes use the same average.
 */
#define SMX_CLOCK_MASTER_HZ 21477270u
#define SMX_CLOCK_SPC_HZ 1025280u
/**
 * Master clock in the line where DRAM refresh pauses the CPU for 40 clocks,
 * less the line start's offset from an 8-clock boundary.
 */
#define SMX_CLOCK_REFRESH_DOT 538u
#define SMX_CLOCK_REFRESH_DOTS 40u
/** HDMA reloads on line 0 this many clocks in, plus the line start's offset. */
#define SMX_CLOCK_HDMA_INIT_DOT 12u
/** HDMA transfers on lines 0 to 224 at this dot. */
#define SMX_CLOCK_HDMA_DOT 1104u
/** At PPU dot `h`, the pixels left of `h - 21` have been drawn. */
#define SMX_CLOCK_FIRST_PIXEL_DOT 21u

/**
 * The S-CPU's clock and its internal registers (`$4200-$421F`).
 *
 * Time advances as each CPU cycle happens. A read samples the bus 4 clocks
 * before its cycle ends; a write lands at the end. `dots` is the master
 * clock, `line` and `dot` the beam position, `frame` counts frames since
 * power on, and `field` flips at each new frame. Odd fields drop four master
 * clocks from line 240 unless `$2133` bit 0 selects interlace.
 *
 * Each line pauses the CPU 40 clocks for DRAM refresh. HDMA reloads early on
 * line 0 and transfers late on lines 0 to 224, and general-purpose DMA starts
 * after a write to `$420B`. Each waits for a CPU cycle start (see `SmxDma`).
 *
 * Interrupts: vblank sets `$4210` bit 7 two clocks into line 225, and four
 * clocks later signals NMI when `$4200` bit 7 is on. The H/V counters tick
 * every 4 clocks. A match with `$4207-$420A` sets `$4211` bit 7 a tick later
 * and drives IRQ a tick after that; IRQ stays up until `$4211` is read.
 * At each CPU cycle start the CPU samples both lines, unless DMA has just
 * run then. After an instruction it takes a pending NMI, else an IRQ seen
 * unmasked at its last cycle start.
 *
 * Auto-joypad reads `joy` into `$4218-$4219` over 4224 clocks from just
 * after vblank begins, holding `$4212` bit 0 meanwhile. When `ppu` is set,
 * each visible line is drawn as the beam leaves it, and a PPU register write
 * first draws the line up to the beam. The SPC700 is run up to each APU port
 * access and to the end of each step; `apu_at` is the master clock it has
 * reached. `start` is when the current instruction began. `step_frame` is
 * the frame the current step began in. `ended` means a translated block has
 * already finished its last instruction.
 */
typedef struct SmxClock {
  SmxBus *bus;
  SmxCpu *cpu;
  SmxDma *dma;
  SmxPpu *ppu;
  Uint64 dots;
  Uint64 frame;
  Uint32 line;
  Uint32 dot;
  bool field;
  Uint32 drawn_x;
  Uint64 apu_at;
  Uint32 dma_count;
  /** Auto-joypad value: low byte to `$4218` (A X L R), high byte to `$4219`. */
  Uint16 joy;
  /* Line events: the next at `event_at`, and this line's refresh dot. */
  Uint8 event;
  Uint32 event_at;
  Uint32 refresh_at;
  /* NMI: `$4210` bit 7, cycle starts until it is recognized, and recognized. */
  bool nmi_flag;
  Uint8 nmi_count;
  bool need_nmi;
  /* IRQ: `$4211` bit 7, ticks until the flag or line changes, the line, the H/V match. */
  bool irq_flag;
  Uint8 irq_delay;
  bool irq_line;
  bool irq_match;
  bool prev_irq;
  /* Auto-joypad: when this frame's read starts, and the value it reads. */
  Uint64 joy_start;
  Uint16 joy_latched;
  bool joy_enabled;
  /* Multiply and divide: the operands, the results, and cycles left. */
  Uint8 mul_a;
  Uint16 dividend;
  Uint16 quotient;
  Uint16 product;
  Uint32 alu_shift;
  Uint8 mul_left;
  Uint8 div_left;
  Uint64 alu_cycle;
  /** CPU cycles started, for the multiply and divide unit. */
  Uint64 cpu_cycles;
  Uint64 start;
  Uint64 step_frame;
  bool ended;
} SmxClock;

void smx_clock_init(SmxClock *clock, SmxCpu *cpu, SmxDma *dma);

/**
 * Run one instruction, then take a pending interrupt.
 * With translated code, one step may run a block of instructions, each timed
 * as if stepped alone; the block ends early at an interrupt or a new frame.
 */
bool smx_clock_step(SmxClock *clock);

/**
 * Put the beam at `dot` of `line` in the current frame, as if time had run
 * there without refresh or HDMA. For tests that start mid-frame.
 */
void smx_clock_seek(SmxClock *clock, Uint32 line, Uint32 dot);

/** Run master clocks up to the start of the next NTSC frame. */
bool smx_clock_frame(SmxClock *clock);

#endif /* SMX_CLOCK_H_ */
