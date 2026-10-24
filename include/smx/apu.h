#ifndef SMX_APU_H_
#define SMX_APU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * APU as seen from the 65816, plus the SPC700 that runs an uploaded program.
 * Reset serves the IPL handshake on `$2140-$2143`. A zero-length block jumps
 * to the entry address. DSP writes are stored and do not produce sound.
 * One `smx_apu_step` is one SPC700 instruction, not a cycle-accurate slice.
 */
typedef struct SmxApu {
  SmxBus *bus;
  Uint8 ram[65536];
  Uint8 in[4];
  Uint8 out[4];
  Uint8 dsp[128];
  Uint8 dsp_addr;
  Uint8 control;
  Uint8 timer_out[3];
  Uint8 timer_target[3];
  Uint16 timer_stage[3];
  Uint16 timer_div[3];
  Uint16 block;
  Uint16 entry;
  Uint16 pc;
  Uint8 a;
  Uint8 x;
  Uint8 y;
  Uint8 sp;
  Uint8 psw;
  Uint8 index;
  bool started;
  bool receiving;
  bool wait_zero;
  bool program_ready;
  bool running;
} SmxApu;

void smx_apu_init(SmxApu *apu, SmxBus *bus);
void smx_apu_write_port(SmxApu *apu, Uint8 index, Uint8 value);
Uint8 smx_apu_read_port(const SmxApu *apu, Uint8 index);
Uint8 smx_apu_ram(const SmxApu *apu, Uint16 addr);
bool smx_apu_core_running(const SmxApu *apu);

/** Execute one SPC700 instruction. False when the core is stopped. */
bool smx_apu_step(SmxApu *apu);

#endif /* SMX_APU_H_ */
