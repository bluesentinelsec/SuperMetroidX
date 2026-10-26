#ifndef SMX_APU_H_
#define SMX_APU_H_

#include "smx/bus.h"
#include "smx/dsp.h"

#include <SDL3/SDL.h>

/** Port changes each side keeps so the other can read the value at its own time. */
#define SMX_APU_PORT_HISTORY 8u

/** Writes to one APU port, oldest first. `times` are in master clocks times `SMX_CLOCK_SPC_HZ`. */
typedef struct SmxApuPort {
  Uint8 base;
  Uint8 count;
  Uint64 times[SMX_APU_PORT_HISTORY];
  Uint8 values[SMX_APU_PORT_HISTORY];
} SmxApuPort;

/** SPC700 state an instruction can change. A blocked instruction restores it. */
typedef struct SmxSpcCore {
  Uint64 cycle;
  Uint16 pc;
  Uint8 a;
  Uint8 x;
  Uint8 y;
  Uint8 sp;
  Uint8 psw;
  Uint8 dsp_addr;
  Uint8 control;
  Uint8 timer_out[3];
  Uint8 timer_target[3];
  Uint16 timer_stage[3];
  Uint16 timer_div[3];
  Uint8 in[4];
  Uint8 out[4];
  SmxApuPort to_spc[4];
  SmxApuPort to_cpu[4];
  bool running;
  bool program_ready;
  Uint16 entry;
} SmxSpcCore;

/**
 * APU as seen from the 65816, plus the SPC700 and its S-DSP.
 * Reset starts the IPL ROM at `$FFC0`. The IPL writes `$AA`/`$BB`, accepts a
 * block on `$2140-$2143`, and jumps to it. `$F2`/`$F3` reach `dsp`.
 * Every SPC700 cycle is one bus access or one internal cycle, as on the
 * chip, so each access happens on its own cycle `core.cycle`. Cycle `n`
 * starts at `n * SMX_CLOCK_MASTER_HZ` in units of master clocks times
 * `SMX_CLOCK_SPC_HZ`. Port writes from either side are kept with their times,
 * and each side reads a port as of its own access. An instruction that would
 * read a port later than `limit`, the time the CPU has reached, is undone and
 * retried once the CPU catches up. The DSP is run up to each `$F3` access
 * and to the end of each instruction. `cycles` is what the last step spent.
 * `cpu_now` is the CPU's time for its next port access; when no clock sets
 * it, port writes are seen at once.
 */
typedef struct SmxApu {
  SmxBus *bus;
  Uint8 ram[65536];
  SmxSpcCore core;
  Uint64 limit;
  Uint64 cpu_now;
  Uint64 dsp_cycle;
  Uint8 cycles;
  bool blocked;
  Uint8 undo_count;
  Uint16 undo_addr[8];
  Uint8 undo_value[8];
  SmxDsp dsp;
} SmxApu;

void smx_apu_init(SmxApu *apu, SmxBus *bus);
void smx_apu_write_port(SmxApu *apu, Uint8 index, Uint8 value);
Uint8 smx_apu_read_port(const SmxApu *apu, Uint8 index);
Uint8 smx_apu_ram(const SmxApu *apu, Uint16 addr);
bool smx_apu_core_running(const SmxApu *apu);

/** Execute one SPC700 instruction with no time limit. False when the core is stopped. */
bool smx_apu_step(SmxApu *apu);

/**
 * Run instructions that start before `limit`, stopping early at one that
 * would read a port after it. A stopped core still runs the DSP.
 */
void smx_apu_run(SmxApu *apu, Uint64 limit);

/** Set the CPU's time for a port access and publish the ports as of then. */
void smx_apu_cpu_sync(SmxApu *apu, Uint64 now);

/** Move up to `max` stereo frames of DSP output into `frames`. Returns the count. */
Uint32 smx_apu_drain(SmxApu *apu, Sint16 *frames, Uint32 max);

#endif /* SMX_APU_H_ */
