#ifndef SMX_APU_H_
#define SMX_APU_H_

#include "smx/bus.h"
#include "smx/dsp.h"

#include <SDL3/SDL.h>

/**
 * APU as seen from the 65816, plus the SPC700 and its S-DSP.
 * Reset starts the IPL ROM at `$FFC0`. The IPL writes `$AA`/`$BB`, accepts a
 * block on `$2140-$2143`, and jumps to it. `$F2`/`$F3` reach `dsp`.
 * One `smx_apu_step` is one SPC700 instruction. `cycles` is what it spent:
 * the opcode's cycle count, plus two when a branch is taken. The DSP runs
 * the same cycles. A `$F3` access first runs it to the instruction's last
 * cycle; `op_cycles` and `dsp_done` track that within the instruction.
 */
typedef struct SmxApu {
  SmxBus *bus;
  Uint8 ram[65536];
  Uint8 in[4];
  Uint8 out[4];
  Uint8 dsp_addr;
  Uint8 control;
  Uint8 timer_out[3];
  Uint8 timer_target[3];
  Uint16 timer_stage[3];
  Uint16 timer_div[3];
  Uint16 entry;
  Uint16 pc;
  Uint8 a;
  Uint8 x;
  Uint8 y;
  Uint8 sp;
  Uint8 psw;
  Uint8 cycles;
  Uint8 op_cycles;
  Uint8 dsp_done;
  bool program_ready;
  bool running;
  SmxDsp dsp;
} SmxApu;

void smx_apu_init(SmxApu *apu, SmxBus *bus);
void smx_apu_write_port(SmxApu *apu, Uint8 index, Uint8 value);
Uint8 smx_apu_read_port(const SmxApu *apu, Uint8 index);
Uint8 smx_apu_ram(const SmxApu *apu, Uint16 addr);
bool smx_apu_core_running(const SmxApu *apu);

/** Cycles the next instruction takes, not counting a taken branch. */
Uint8 smx_apu_next_cycles(const SmxApu *apu);

/** Execute one SPC700 instruction. False when the core is stopped. */
bool smx_apu_step(SmxApu *apu);

/** Move up to `max` stereo frames of DSP output into `frames`. Returns the count. */
Uint32 smx_apu_drain(SmxApu *apu, Sint16 *frames, Uint32 max);

#endif /* SMX_APU_H_ */
