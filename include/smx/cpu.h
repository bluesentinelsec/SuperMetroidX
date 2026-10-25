#ifndef SMX_CPU_H_
#define SMX_CPU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * 65816. `bus` is borrowed.
 * `clocks` gains the master clocks of every access at the bus speed of its
 * address, plus 6 for each internal cycle. The caller drains it.
 */
typedef struct SmxCpu {
  SmxBus *bus;
  Uint16 a;
  Uint16 x;
  Uint16 y;
  Uint16 s;
  Uint16 d;
  Uint16 pc;
  Uint8 db;
  Uint8 pb;
  Uint8 p;
  bool e;
  bool stopped;
  bool waiting;
  Uint32 clocks;
} SmxCpu;

void smx_cpu_init(SmxCpu *cpu, SmxBus *bus);

/** Load the emulation-mode reset vector at $00:FFFC. */
void smx_cpu_reset(SmxCpu *cpu);

/**
 * Execute one instruction.
 * Returns false when the processor is stopped or the opcode is not implemented.
 */
bool smx_cpu_step(SmxCpu *cpu);

/** Edge-triggered non-maskable interrupt. `I` does not block it. */
void smx_cpu_nmi(SmxCpu *cpu);

/** Maskable interrupt. `I` blocks it. */
void smx_cpu_irq(SmxCpu *cpu);

#endif /* SMX_CPU_H_ */
