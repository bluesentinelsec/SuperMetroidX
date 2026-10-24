#ifndef SMX_APU_H_
#define SMX_APU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * APU as seen from the 65816, plus the SPC700 that runs an uploaded program.
 * Reset serves the IPL handshake on `$2140-$2143`. A zero-length block jumps
 * to the entry address. DSP writes are stored. `smx_apu_brr_decode` expands
 * one BRR block. Voice 0 renders those samples when `$4C` bit 0 is set.
 * `$02`/`$03` set its pitch, where `$1000` consumes one sample per output.
 * All eight voices mix into the left and right outputs. Envelopes are not applied.
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
  bool voice_on[8];
  Uint8 voice_i[8];
  Uint16 voice_addr[8];
  Uint16 voice_loop[8];
  Sint16 voice_p1[8];
  Sint16 voice_p2[8];
  Uint16 voice_phase[8];
  Sint16 voice_buf[8][16];
} SmxApu;

void smx_apu_init(SmxApu *apu, SmxBus *bus);
void smx_apu_write_port(SmxApu *apu, Uint8 index, Uint8 value);
Uint8 smx_apu_read_port(const SmxApu *apu, Uint8 index);
Uint8 smx_apu_ram(const SmxApu *apu, Uint16 addr);
bool smx_apu_core_running(const SmxApu *apu);

/** Execute one SPC700 instruction. False when the core is stopped. */
bool smx_apu_step(SmxApu *apu);

/** Expand one 9-byte BRR block into 16 samples. Updates the two history samples. */
void smx_apu_brr_decode(const Uint8 *block, Sint16 *out, Sint16 *prev1, Sint16 *prev2);

/** Write `count` mixed samples. `right` may be null. */
void smx_apu_render(SmxApu *apu, Sint16 *left, Sint16 *right, Uint32 count);

#endif /* SMX_APU_H_ */
