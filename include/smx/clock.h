#ifndef SMX_CLOCK_H_
#define SMX_CLOCK_H_

#include "smx/cpu.h"
#include "smx/dma.h"
#include "smx/ppu.h"

#include <SDL3/SDL.h>

/** Master clocks in one NTSC scanline, including the short-line average. */
#define SMX_CLOCK_DOTS_PER_LINE 1364u
/** NTSC frames are 262 lines, with vblank starting on line 225. */
#define SMX_CLOCK_LINES 262u
#define SMX_CLOCK_VBLANK_LINE 225u
#define SMX_CLOCK_DOTS_PER_FRAME (SMX_CLOCK_DOTS_PER_LINE * SMX_CLOCK_LINES)

/**
 * Instruction-timed NTSC clock.
 * Each instruction spends 18 master clocks, three fast-ROM cycles.
 * `$4212` bit 7 follows vblank.
 * Bit 0 stays set for 4224 master clocks from the start of vblank.
 * and `$4200` bit 7 arms an NMI on the rising edge. A write to `$420B`
 * runs general-purpose DMA before the next instruction. `$4200` bit 4
 * fires an IRQ when the dot in the line matches `$4207`/`$4208`. Bit 5
 * still fires at the start of the line in `$4209`/`$420A`. Both bits wait
 * for that dot on that line. When `ppu` is set, each visible line is
 * drawn as the dot counter leaves it, before HDMA for the next line.
 * The SPC700 runs one instruction every 84 master clocks. That is four
 * SPC cycles at the NTSC ratio of 21 master clocks per cycle.
 */
typedef struct SmxClock {
  SmxBus *bus;
  SmxCpu *cpu;
  SmxDma *dma;
  SmxPpu *ppu;
  Uint32 dots;
  Uint32 apu_dots;
  Uint32 dma_count;
  Uint16 joy;
  bool prev_vblank;
  bool irq_line;
} SmxClock;

void smx_clock_init(SmxClock *clock, SmxCpu *cpu, SmxDma *dma);

/** Run one instruction and the dots it consumes. */
bool smx_clock_step(SmxClock *clock);

/** Run one NTSC frame of master clocks. */
bool smx_clock_frame(SmxClock *clock);

#endif /* SMX_CLOCK_H_ */
