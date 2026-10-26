#ifndef SMX_CPU_H_
#define SMX_CPU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

typedef struct SmxCpu SmxCpu;

/** Translated code for one entry point. It runs until control leaves straight-line code. */
typedef void (*SmxCpuBlock)(SmxCpu *cpu);

/** Translated blocks, sorted by `keys`. A key is `SMX_CPU_KEY` of the block's entry. */
typedef struct SmxCpuCode {
  const Uint32 *keys;
  const SmxCpuBlock *blocks;
  Uint32 count;
} SmxCpuCode;

/** Entry key: 24-bit address, then the E, M, and X flags the code was translated for. */
#define SMX_CPU_KEY(addr, e, m, x)                                                                 \
  ((((Uint32)(addr) & 0x00FFFFFFu) << 3) | ((e) ? 4u : 0u) | ((m) ? 2u : 0u) | ((x) ? 1u : 0u))

/**
 * 65816. `bus` is borrowed.
 * `clocks` gains the master clocks of every access at the bus speed of its
 * address, plus 6 for each internal cycle. The caller drains it.
 * `last_at` is the value `clocks` had when the last cycle so far began.
 * `code`, when set, holds translated blocks that `smx_cpu_run` prefers over
 * the interpreter. Between two translated instructions a block calls `next`
 * with `next_user`; false ends the block there. `translated` and
 * `interpreted` count instructions run each way.
 */
struct SmxCpu {
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
  Uint32 last_at;
  const SmxCpuCode *code;
  bool (*next)(void *user);
  void *next_user;
  Uint64 translated;
  Uint64 interpreted;
};

void smx_cpu_init(SmxCpu *cpu, SmxBus *bus);

/** Load the emulation-mode reset vector at $00:FFFC. */
void smx_cpu_reset(SmxCpu *cpu);

/**
 * Execute one instruction.
 * Returns false when the processor is stopped or the opcode is not implemented.
 */
bool smx_cpu_step(SmxCpu *cpu);

/** Run the translated block at the current key, or one interpreted instruction. */
bool smx_cpu_run(SmxCpu *cpu);

/** Key of the current address and flags. */
Uint32 smx_cpu_key(const SmxCpu *cpu);

/** Edge-triggered non-maskable interrupt. `I` does not block it. */
void smx_cpu_nmi(SmxCpu *cpu);

/** Maskable interrupt. `I` blocks it. */
void smx_cpu_irq(SmxCpu *cpu);

/*
 * Operations translated code is built from. Each spends exactly the cycles
 * the interpreter spends for the same step, and reads the M, X, and E flags
 * at run time the same way.
 */

/** Called by a block between instructions. False means return now. */
bool smx_cpu_next(SmxCpu *cpu);

/** Read the byte at PB:PC, spend its cycle, and advance PC. */
Uint8 smx_cpu_fetch(SmxCpu *cpu);

/** One internal cycle. */
void smx_cpu_idle(SmxCpu *cpu);

Uint8 smx_cpu_read8(SmxCpu *cpu, Uint32 addr);
void smx_cpu_write8(SmxCpu *cpu, Uint32 addr, Uint8 value);
/** Two bytes at `addr` and `addr + 1`, carrying into the bank. */
Uint16 smx_cpu_read16(SmxCpu *cpu, Uint32 addr);
void smx_cpu_write16(SmxCpu *cpu, Uint32 addr, Uint16 value);
/** Two bytes in `bank`, wrapping within it. */
Uint16 smx_cpu_read_bank16(SmxCpu *cpu, Uint8 bank, Uint16 addr);
/** An accumulator-sized operand. */
Uint16 smx_cpu_read_m(SmxCpu *cpu, Uint32 addr);
void smx_cpu_write_m(SmxCpu *cpu, Uint32 addr, Uint16 value);
/** An index-sized operand. */
Uint16 smx_cpu_read_x(SmxCpu *cpu, Uint32 addr);
void smx_cpu_push8(SmxCpu *cpu, Uint8 value);
void smx_cpu_push16(SmxCpu *cpu, Uint16 value);
Uint8 smx_cpu_pull8(SmxCpu *cpu);
Uint16 smx_cpu_pull16(SmxCpu *cpu);

/**
 * Effective address of `op` once its operand bytes have been fetched,
 * spending the mode's internal cycles and pointer reads.
 */
Uint32 smx_cpu_address(SmxCpu *cpu, Uint8 op, Uint32 operand);

/** Load a register and set N and Z at its current width. */
void smx_cpu_set_a(SmxCpu *cpu, Uint16 value);
void smx_cpu_set_x(SmxCpu *cpu, Uint16 value);
void smx_cpu_set_y(SmxCpu *cpu, Uint16 value);
/** Replace P. Setting X clears the index high bytes; E forces M and X. */
void smx_cpu_set_p(SmxCpu *cpu, Uint8 p);
void smx_cpu_adc(SmxCpu *cpu, Uint16 value);
void smx_cpu_sbc(SmxCpu *cpu, Uint16 value);
void smx_cpu_compare(SmxCpu *cpu, Uint16 left, Uint16 right, bool eight);
/** ORA, AND, EOR, or CMP by `op`'s row. Returns the result; CMP returns A. */
Uint16 smx_cpu_logic(SmxCpu *cpu, Uint8 op, Uint16 value);
/** BIT. The immediate form (`$89`) sets only Z. */
void smx_cpu_bit(SmxCpu *cpu, Uint8 op, Uint16 value);
/** TSB when `set`, else TRB, on `mem` read from `addr`. */
void smx_cpu_test_bits(SmxCpu *cpu, Uint32 addr, Uint16 mem, bool set);
/** ASL, ROL, LSR, ROR, INC, or DEC on memory by `op`. */
void smx_cpu_shift(SmxCpu *cpu, Uint8 op, Uint32 addr);
/** A one-byte or stack instruction after its opcode, with its internal cycles. */
bool smx_cpu_implied(SmxCpu *cpu, Uint8 op);
/** BRK or COP after its opcode. */
void smx_cpu_interrupt(SmxCpu *cpu, Uint16 native_vector, Uint16 emu_vector);
/** A relative branch after its opcode. */
void smx_cpu_branch(SmxCpu *cpu, bool take);
/** One byte of MVN (`$54`) or MVP (`$44`) after its opcode. */
void smx_cpu_move(SmxCpu *cpu, Uint8 op);

#endif /* SMX_CPU_H_ */
