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

/** NTSC master clock and the SPC700's 1.024 MHz cycle rate. */
#define SMX_CLOCK_MASTER_HZ 21477272u
#define SMX_CLOCK_SPC_HZ 1024000u
/** Master clock in the line where DRAM refresh pauses the CPU for 40 clocks. */
#define SMX_CLOCK_REFRESH_DOT 538u
#define SMX_CLOCK_REFRESH_DOTS 40u
/** At PPU dot `h`, the pixels left of `h - 21` have been drawn. */
#define SMX_CLOCK_FIRST_PIXEL_DOT 21u

/**
 * Instruction-timed NTSC clock.
 * Each instruction spends the master clocks the CPU counted for it: every
 * access at its bus speed and 6 for each internal cycle. General-purpose DMA,
 * HDMA, and DRAM refresh pause the CPU for the clocks they cost, which
 * `stall` holds until the next step. Time advances one line at a time.
 * `$4212` bit 7 follows vblank.
 * Bit 0 stays set for 4224 master clocks from the start of vblank,
 * and `$4200` bit 7 arms an NMI on the rising edge. A write to `$420B`
 * runs general-purpose DMA before the next instruction. `$4200` bit 4
 * fires an IRQ when the line reaches PPU dot `$4207`/`$4208`, four master
 * clocks per dot. Bit 5
 * still fires at the start of the line in `$4209`/`$420A`. Both bits wait
 * for that dot on that line. HDMA reloads when line 0 starts and runs as
 * each of lines 1 to 224 starts. When `ppu` is set, each visible line is
 * drawn as the dot counter leaves it, before HDMA for the next line. A PPU
 * register write first draws the line up to the beam, so the write only
 * changes the pixels after it. `drawn_x` is how far the line has been drawn.
 * The SPC700 runs whole instructions to keep pace with the master clock at
 * the ratio of `SMX_CLOCK_SPC_HZ` to `SMX_CLOCK_MASTER_HZ`. `apu_credit` is
 * that lead in units of one master clock times `SMX_CLOCK_SPC_HZ`.
 */
typedef struct SmxClock {
  SmxBus *bus;
  SmxCpu *cpu;
  SmxDma *dma;
  SmxPpu *ppu;
  Uint64 dots;
  Uint32 stall;
  Uint32 drawn_x;
  Sint64 apu_credit;
  Uint32 dma_count;
  /** Auto-joypad value: low byte to `$4218` (A X L R), high byte to `$4219`. */
  Uint16 joy;
  bool prev_vblank;
  bool irq_line;
} SmxClock;

void smx_clock_init(SmxClock *clock, SmxCpu *cpu, SmxDma *dma);

/** Pay any pending stall, then run one instruction and the dots it consumes. */
bool smx_clock_step(SmxClock *clock);

/** Run master clocks up to the start of the next NTSC frame. */
bool smx_clock_frame(SmxClock *clock);

#endif /* SMX_CLOCK_H_ */
