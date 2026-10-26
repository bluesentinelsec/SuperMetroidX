#include "smx/apu.h"
#include "smx/clock.h"

enum {
  SMX_SPC_C = 0x01,
  SMX_SPC_Z = 0x02,
  SMX_SPC_I = 0x04,
  SMX_SPC_H = 0x08,
  SMX_SPC_B = 0x10,
  SMX_SPC_P = 0x20,
  SMX_SPC_V = 0x40,
  SMX_SPC_N = 0x80
};

/** A time no port write can be at or after; untimed writes use 0. */
#define SMX_APU_NEVER 0xFFFFFFFFFFFFFFFFull

static const Uint8 smx_spc_ipl[64] = {
    0xCD, 0xEF, 0xBD, 0xE8, 0x00, 0xC6, 0x1D, 0xD0, 0xFC, 0x8F, 0xAA, 0xF4, 0x8F, 0xBB, 0xF5, 0x78,
    0xCC, 0xF4, 0xD0, 0xFB, 0x2F, 0x19, 0xEB, 0xF4, 0xD0, 0xFC, 0x7E, 0xF4, 0xD0, 0x0B, 0xE4, 0xF5,
    0xCB, 0xF4, 0xD7, 0x00, 0xFC, 0xD0, 0xF3, 0xAB, 0x01, 0x10, 0xEF, 0x7E, 0xF4, 0x10, 0xEB, 0xBA,
    0xF6, 0xDA, 0x00, 0xBA, 0xF4, 0xC4, 0xF4, 0xDD, 0x5D, 0xD0, 0xDB, 0x1F, 0x00, 0x00, 0xC0, 0xFF};

/*
 * Ports: the newest write at or before `time` wins. The SPC700 reads a CPU
 * write only after it (`strict`); the CPU reads an SPC700 write from its
 * time on. That is the order Mesen 2.1.1 settles a tie in.
 */
static Uint8 smx_port_at(const SmxApuPort *port, Uint64 time, bool strict) {
  Uint8 value = port->base;
  Uint8 i = 0;

  for (; i < port->count && (strict ? port->times[i] < time : port->times[i] <= time); i += 1) {
    value = port->values[i];
  }
  return value;
}

static void smx_port_push(SmxApuPort *port, Uint64 time, Uint8 value) {
  Uint8 at = 0;

  if (port->count == SMX_APU_PORT_HISTORY) {
    /* The oldest write is long past for both sides; fold it into the base. */
    port->base = port->values[0];
    SDL_memmove(&port->times[0], &port->times[1],
                sizeof(port->times[0]) * (SMX_APU_PORT_HISTORY - 1u));
    SDL_memmove(&port->values[0], &port->values[1], SMX_APU_PORT_HISTORY - 1u);
    port->count = (Uint8)(port->count - 1u);
  }
  at = port->count;
  while (at > 0 && port->times[at - 1u] > time) {
    port->times[at] = port->times[at - 1u];
    port->values[at] = port->values[at - 1u];
    at = (Uint8)(at - 1u);
  }
  port->times[at] = time;
  port->values[at] = value;
  port->count = (Uint8)(port->count + 1u);
}

static void smx_apu_publish(SmxApu *apu) {
  Uint8 index = 0;

  if (apu->bus == NULL) {
    return;
  }
  for (; index < 4; index += 1) {
    apu->bus->mmio[0x2140u + index] = smx_port_at(&apu->core.to_cpu[index], apu->cpu_now, false);
  }
}

static void smx_apu_cpu_write(void *user, Uint8 index, Uint8 value) {
  SmxApu *apu = user;
  Uint64 time = apu->cpu_now == SMX_APU_NEVER ? 0 : apu->cpu_now;

  apu->core.in[index] = value;
  smx_port_push(&apu->core.to_spc[index], time, value);
  smx_apu_publish(apu);
}

void smx_apu_init(SmxApu *apu, SmxBus *bus) {
  Uint8 index = 0;

  SDL_memset(apu, 0, sizeof(*apu));
  apu->bus = bus;
  apu->cpu_now = SMX_APU_NEVER;
  apu->limit = SMX_APU_NEVER;
  apu->core.out[0] = 0xAA;
  apu->core.out[1] = 0xBB;
  for (; index < 4; index += 1) {
    apu->core.to_cpu[index].base = apu->core.out[index];
  }
  /* Bit 7 maps the IPL ROM over $FFC0-$FFFF until the program clears it. */
  apu->core.control = 0x80;
  apu->core.pc = 0xFFC0;
  apu->core.running = true;
  smx_dsp_init(&apu->dsp, apu->ram);
  /* The DSP and the timer prescalers are two cycles in when the first instruction starts. */
  smx_dsp_run(&apu->dsp, 2);
  apu->core.timer_div[0] = 2;
  apu->core.timer_div[1] = 2;
  apu->core.timer_div[2] = 2;
  if (bus != NULL) {
    bus->apu_user = apu;
    bus->apu_write = smx_apu_cpu_write;
    smx_apu_publish(apu);
  }
}

void smx_apu_write_port(SmxApu *apu, Uint8 index, Uint8 value) {
  if (index > 3 || apu->bus == NULL) {
    return;
  }
  smx_bus_write(apu->bus, 0x002140u + index, value);
}

Uint8 smx_apu_read_port(const SmxApu *apu, Uint8 index) {
  if (index > 3) {
    return 0;
  }
  return apu->core.out[index];
}

Uint8 smx_apu_ram(const SmxApu *apu, Uint16 addr) {
  return apu->ram[addr];
}

bool smx_apu_core_running(const SmxApu *apu) {
  return apu->core.running;
}

void smx_apu_cpu_sync(SmxApu *apu, Uint64 now) {
  apu->cpu_now = now;
  smx_apu_publish(apu);
}

Uint32 smx_apu_drain(SmxApu *apu, Sint16 *frames, Uint32 max) {
  return smx_dsp_drain(&apu->dsp, frames, max);
}

/*
 * When the current cycle's port read happens, in master clocks times
 * SMX_CLOCK_SPC_HZ: half a cycle into it. The SPC700 is two cycles into its
 * reset when the CPU starts, as in Mesen 2.1.1.
 */
static Uint64 smx_spc_now(const SmxApu *apu) {
  return (apu->core.cycle * 2u + 5u) * (SMX_CLOCK_MASTER_HZ / 2u);
}

/* Run the DSP up to the start of SPC700 cycle `cycle`. */
static void smx_spc_dsp_run_to(SmxApu *apu, Uint64 cycle) {
  if (apu->dsp_cycle < cycle) {
    smx_dsp_run(&apu->dsp, (Uint32)(cycle - apu->dsp_cycle));
    apu->dsp_cycle = cycle;
  }
}

static void smx_spc_dsp_catch_up(SmxApu *apu) {
  smx_spc_dsp_run_to(apu, apu->core.cycle);
}

/* The DSP step of the cycle that reaches `$F3` runs before the access. */
static void smx_spc_dsp_access(SmxApu *apu) {
  smx_spc_dsp_run_to(apu, apu->core.cycle + 1u);
}

/*
 * The timers advance at the start of a cycle, so an access sees this
 * cycle's tick. The prescalers run whether or not a timer is on.
 */
static void smx_spc_timers(SmxApu *apu) {
  static const Uint16 rate[3] = {128, 128, 16};
  SmxSpcCore *core = &apu->core;
  Uint8 index = 0;

  for (; index < 3; index += 1) {
    Uint16 limit = 0;
    core->timer_div[index] = (Uint16)(core->timer_div[index] + 1u);
    if (core->timer_div[index] < rate[index]) {
      continue;
    }
    core->timer_div[index] = 0;
    if ((core->control & (Uint8)(1u << index)) == 0) {
      continue;
    }
    core->timer_stage[index] = (Uint16)(core->timer_stage[index] + 1u);
    limit = core->timer_target[index] == 0 ? 256 : core->timer_target[index];
    if (core->timer_stage[index] >= limit) {
      core->timer_stage[index] = 0;
      core->timer_out[index] = (Uint8)((core->timer_out[index] + 1u) & 0x0F);
    }
  }
}

static void smx_spc_tick(SmxApu *apu) {
  apu->core.cycle += 1;
}

static Uint8 smx_spc_bus_read(SmxApu *apu, Uint16 addr) {
  SmxSpcCore *core = &apu->core;

  if (addr >= 0x00F0 && addr <= 0x00FF) {
    if (addr == 0x00F0 || addr == 0x00F1 || (addr >= 0x00FA && addr <= 0x00FC)) {
      return 0;
    }
    if (addr >= 0x00F4 && addr <= 0x00F7) {
      Uint64 now = smx_spc_now(apu);
      if (now > apu->limit) {
        apu->blocked = true;
        return 0;
      }
      return smx_port_at(&core->to_spc[addr - 0x00F4], now, true);
    }
    if (addr == 0x00F2) {
      return core->dsp_addr;
    }
    if (addr == 0x00F3) {
      if (!apu->blocked) {
        smx_spc_dsp_access(apu);
      }
      return smx_dsp_read(&apu->dsp, core->dsp_addr);
    }
    if (addr >= 0x00FD) {
      Uint8 value = core->timer_out[addr - 0x00FD];
      if (!apu->blocked) {
        core->timer_out[addr - 0x00FD] = 0;
      }
      return value;
    }
  }
  if (addr >= 0xFFC0 && (core->control & 0x80) != 0) {
    return smx_spc_ipl[addr - 0xFFC0];
  }
  return apu->ram[addr];
}

static void smx_spc_store(SmxApu *apu, Uint16 addr, Uint8 value) {
  if (apu->undo_count < 8) {
    apu->undo_addr[apu->undo_count] = addr;
    apu->undo_value[apu->undo_count] = apu->ram[addr];
    apu->undo_count = (Uint8)(apu->undo_count + 1u);
  }
  apu->ram[addr] = value;
}

static void smx_spc_bus_write(SmxApu *apu, Uint16 addr, Uint8 value) {
  SmxSpcCore *core = &apu->core;
  Uint8 index = 0;

  if (apu->blocked) {
    return;
  }
  /* Register writes also land in the RAM underneath. */
  smx_spc_store(apu, addr, value);
  if (addr < 0x00F0 || addr > 0x00FF) {
    return;
  }
  if (addr >= 0x00F4 && addr <= 0x00F7) {
    core->out[addr - 0x00F4] = value;
    /* A port write lands at the end of its cycle, half a cycle after a read would. */
    smx_port_push(&core->to_cpu[addr - 0x00F4], smx_spc_now(apu) + SMX_CLOCK_MASTER_HZ / 2u, value);
    smx_apu_publish(apu);
  } else if (addr == 0x00F1) {
    /* Turning a timer on clears its counter and output, not its prescaler. */
    for (; index < 3; index += 1) {
      Uint8 bit = (Uint8)(1u << index);
      if ((core->control & bit) == 0 && (value & bit) != 0) {
        core->timer_stage[index] = 0;
        core->timer_out[index] = 0;
      }
    }
    core->control = value;
    for (index = 0; index < 4; index += 1) {
      if ((value & (index < 2 ? 0x10 : 0x20)) != 0) {
        core->in[index] = 0;
        smx_port_push(&core->to_spc[index], smx_spc_now(apu), 0);
      }
    }
  } else if (addr == 0x00F2) {
    core->dsp_addr = value;
  } else if (addr == 0x00F3) {
    smx_spc_dsp_access(apu);
    if (core->dsp_addr < 0x80) {
      smx_dsp_write(&apu->dsp, core->dsp_addr, value);
    }
  } else if (addr >= 0x00FA && addr <= 0x00FC) {
    core->timer_target[addr - 0x00FA] = value;
  }
}

/* Cycle helpers. Each is exactly one SPC700 cycle. */

static Uint8 rd(SmxApu *apu, Uint16 addr) {
  Uint8 value = 0;

  smx_spc_timers(apu);
  value = smx_spc_bus_read(apu, addr);
  smx_spc_tick(apu);
  return value;
}

static void wr(SmxApu *apu, Uint16 addr, Uint8 value) {
  smx_spc_timers(apu);
  smx_spc_bus_write(apu, addr, value);
  smx_spc_tick(apu);
}

static void io(SmxApu *apu) {
  smx_spc_timers(apu);
  smx_spc_tick(apu);
}

static Uint8 fetch(SmxApu *apu) {
  Uint8 value = rd(apu, apu->core.pc);
  apu->core.pc = (Uint16)(apu->core.pc + 1u);
  return value;
}

static Uint16 fetch16(SmxApu *apu) {
  Uint8 lo = fetch(apu);
  return (Uint16)(lo | ((Uint16)fetch(apu) << 8));
}

static Uint16 dp(const SmxApu *apu, Uint8 offset) {
  return (Uint16)(((apu->core.psw & SMX_SPC_P) != 0 ? 0x0100u : 0u) | offset);
}

static void push(SmxApu *apu, Uint8 value) {
  wr(apu, (Uint16)(0x0100u + apu->core.sp), value);
  apu->core.sp = (Uint8)(apu->core.sp - 1u);
}

static Uint8 pull(SmxApu *apu) {
  apu->core.sp = (Uint8)(apu->core.sp + 1u);
  return rd(apu, (Uint16)(0x0100u + apu->core.sp));
}

static void set_flag(SmxApu *apu, Uint8 flag, bool on) {
  apu->core.psw = (Uint8)(on ? (apu->core.psw | flag) : (apu->core.psw & (Uint8)~flag));
}

static void nz(SmxApu *apu, Uint8 value) {
  set_flag(apu, SMX_SPC_N, (value & 0x80) != 0);
  set_flag(apu, SMX_SPC_Z, value == 0);
}

static void nz16(SmxApu *apu, Uint16 value) {
  set_flag(apu, SMX_SPC_N, (value & 0x8000) != 0);
  set_flag(apu, SMX_SPC_Z, value == 0);
}

static Uint8 adc(SmxApu *apu, Uint8 left, Uint8 right) {
  Uint16 sum = (Uint16)(left + right + (apu->core.psw & SMX_SPC_C));
  Uint8 result = (Uint8)sum;

  set_flag(apu, SMX_SPC_V, ((~(left ^ right)) & (left ^ result) & 0x80) != 0);
  set_flag(apu, SMX_SPC_H, ((left ^ right ^ result) & 0x10) != 0);
  set_flag(apu, SMX_SPC_C, sum > 0xFF);
  nz(apu, result);
  return result;
}

static Uint8 alu(SmxApu *apu, Uint8 row, Uint8 left, Uint8 right) {
  switch (row) {
  case 0x00:
    left = (Uint8)(left | right);
    break;
  case 0x20:
    left = (Uint8)(left & right);
    break;
  case 0x40:
    left = (Uint8)(left ^ right);
    break;
  case 0x60:
    set_flag(apu, SMX_SPC_C, left >= right);
    nz(apu, (Uint8)(left - right));
    return left;
  case 0x80:
    return adc(apu, left, right);
  default:
    return adc(apu, left, (Uint8)~right);
  }
  nz(apu, left);
  return left;
}

/* ASL, ROL, LSR, ROR by row $00, $20, $40, $60. */
static Uint8 shift(SmxApu *apu, Uint8 row, Uint8 value) {
  bool carry = (apu->core.psw & SMX_SPC_C) != 0;

  if (row == 0x00 || row == 0x20) {
    set_flag(apu, SMX_SPC_C, (value & 0x80) != 0);
    value = (Uint8)((value << 1) | (row == 0x20 && carry ? 1u : 0u));
  } else {
    set_flag(apu, SMX_SPC_C, (value & 0x01) != 0);
    value = (Uint8)((value >> 1) | (row == 0x60 && carry ? 0x80u : 0u));
  }
  nz(apu, value);
  return value;
}

/* A word through the direct page. The second byte wraps within the page. */
static Uint16 rd_dp_word(SmxApu *apu, Uint8 offset) {
  Uint8 lo = rd(apu, dp(apu, offset));
  return (Uint16)(lo | ((Uint16)rd(apu, dp(apu, (Uint8)(offset + 1u))) << 8));
}

static void branch(SmxApu *apu, Uint8 rel, bool take) {
  if (take) {
    io(apu);
    io(apu);
    apu->core.pc = (Uint16)(apu->core.pc + (Sint8)rel);
  }
}

/*
 * Address for the ALU and MOV A columns of `op`. The operand fetches and
 * internal cycles happen here; the caller does the data access.
 */
static Uint16 smx_spc_address(SmxApu *apu, Uint8 op) {
  Uint8 offset = 0;
  Uint16 base = 0;

  switch (op & 0x1F) {
  case 0x04: /* dp */
    return dp(apu, fetch(apu));
  case 0x05: /* !abs */
    return fetch16(apu);
  case 0x06: /* (X) */
    io(apu);
    return dp(apu, apu->core.x);
  case 0x07: /* [dp+X] */
    offset = (Uint8)(fetch(apu) + apu->core.x);
    io(apu);
    return rd_dp_word(apu, offset);
  case 0x14: /* dp+X */
    offset = fetch(apu);
    io(apu);
    return dp(apu, (Uint8)(offset + apu->core.x));
  case 0x15: /* !abs+X */
    base = fetch16(apu);
    io(apu);
    return (Uint16)(base + apu->core.x);
  case 0x16: /* !abs+Y */
    base = fetch16(apu);
    io(apu);
    return (Uint16)(base + apu->core.y);
  default: /* [dp]+Y */
    base = rd_dp_word(apu, fetch(apu));
    io(apu);
    return (Uint16)(base + apu->core.y);
  }
}

static bool smx_spc_memory_column(Uint8 col) {
  return col == 0x04 || col == 0x05 || col == 0x06 || col == 0x07 || col == 0x14 || col == 0x15 ||
         col == 0x16 || col == 0x17;
}

/* The ALU result goes back to memory, except for CMP, which idles instead. */
static void smx_spc_alu_store(SmxApu *apu, Uint8 row, Uint16 addr, Uint8 left, Uint8 right) {
  Uint8 result = alu(apu, row, left, right);

  if (row == 0x60) {
    io(apu);
  } else {
    wr(apu, addr, result);
  }
}

static void smx_spc_execute(SmxApu *apu) {
  SmxSpcCore *core = &apu->core;
  Uint8 op = fetch(apu);
  Uint8 row = (Uint8)(op & 0xE0);
  Uint8 col = (Uint8)(op & 0x1F);
  Uint8 value = 0;
  Uint8 other = 0;
  Uint8 bit = 0;
  Uint16 addr = 0;
  Uint16 word = 0;

  /* OR, AND, EOR, CMP, ADC, SBC. */
  if (row <= 0xA0 && (smx_spc_memory_column(col) || col == 0x08)) {
    value = col == 0x08 ? fetch(apu) : rd(apu, smx_spc_address(apu, op));
    core->a = alu(apu, row, core->a, value);
    return;
  }
  if (row <= 0xA0 && col == 0x09) { /* dp,dp */
    value = rd(apu, dp(apu, fetch(apu)));
    addr = dp(apu, fetch(apu));
    smx_spc_alu_store(apu, row, addr, rd(apu, addr), value);
    return;
  }
  if (row <= 0xA0 && col == 0x18) { /* dp,#imm */
    value = fetch(apu);
    addr = dp(apu, fetch(apu));
    smx_spc_alu_store(apu, row, addr, rd(apu, addr), value);
    return;
  }
  if (row <= 0xA0 && col == 0x19) { /* (X),(Y) */
    io(apu);
    value = rd(apu, dp(apu, core->y));
    addr = dp(apu, core->x);
    smx_spc_alu_store(apu, row, addr, rd(apu, addr), value);
    return;
  }
  /* MOV A,mem and MOV mem,A. A store reads its address first. */
  if ((row == 0xE0 || row == 0xC0) && smx_spc_memory_column(col)) {
    addr = smx_spc_address(apu, op);
    if (row == 0xE0) {
      core->a = rd(apu, addr);
      nz(apu, core->a);
    } else {
      rd(apu, addr);
      wr(apu, addr, core->a);
    }
    return;
  }
  /* SET1, CLR1, BBS, BBC on direct page bits. */
  if ((op & 0x0F) == 0x02 || (op & 0x0F) == 0x03) {
    bit = (Uint8)(1u << (op >> 5));
    addr = dp(apu, fetch(apu));
    value = rd(apu, addr);
    if ((op & 0x0F) == 0x02) {
      wr(apu, addr, (op & 0x10) != 0 ? (Uint8)(value & (Uint8)~bit) : (Uint8)(value | bit));
    } else {
      other = fetch(apu);
      io(apu);
      branch(apu, other, ((value & bit) != 0) == ((op & 0x10) == 0));
    }
    return;
  }
  if ((op & 0x0F) == 0x01) { /* TCALL n */
    addr = (Uint16)(0xFFDEu - (Uint16)((op >> 4) * 2u));
    io(apu);
    push(apu, (Uint8)(core->pc >> 8));
    push(apu, (Uint8)core->pc);
    io(apu);
    value = rd(apu, addr);
    core->pc = (Uint16)(value | ((Uint16)rd(apu, (Uint16)(addr + 1u)) << 8));
    io(apu);
    return;
  }

  switch (op) {
  case 0x00: /* NOP */
    io(apu);
    break;
  case 0xEF: /* SLEEP */
  case 0xFF: /* STOP */
    io(apu);
    io(apu);
    core->running = false;
    break;
  case 0x20: /* CLRP */
  case 0x40: /* SETP */
    io(apu);
    set_flag(apu, SMX_SPC_P, op == 0x40);
    break;
  case 0x60: /* CLRC */
  case 0x80: /* SETC */
    io(apu);
    set_flag(apu, SMX_SPC_C, op == 0x80);
    break;
  case 0xE0: /* CLRV */
    io(apu);
    set_flag(apu, (Uint8)(SMX_SPC_V | SMX_SPC_H), false);
    break;
  case 0xED: /* NOTC */
    io(apu);
    io(apu);
    core->psw = (Uint8)(core->psw ^ SMX_SPC_C);
    break;
  case 0xA0: /* EI */
  case 0xC0: /* DI */
    io(apu);
    io(apu);
    set_flag(apu, SMX_SPC_I, op == 0xA0);
    break;
  case 0x10: /* BPL */
  case 0x30: /* BMI */
  case 0x50: /* BVC */
  case 0x70: /* BVS */
  case 0x90: /* BCC */
  case 0xB0: /* BCS */
  case 0xD0: /* BNE */
  case 0xF0: /* BEQ */
  case 0x2F: /* BRA */ {
    static const Uint8 flag[8] = {SMX_SPC_N, SMX_SPC_N, SMX_SPC_V, SMX_SPC_V,
                                  SMX_SPC_C, SMX_SPC_C, SMX_SPC_Z, SMX_SPC_Z};
    bool take = true;
    value = fetch(apu);
    if (op != 0x2F) {
      bool set = (core->psw & flag[op >> 5]) != 0;
      take = ((op >> 5) & 1u) != 0 ? set : !set;
    }
    branch(apu, value, take);
    break;
  }
  case 0x2E: /* CBNE dp,rel */
  case 0xDE: /* CBNE dp+X,rel */
    value = fetch(apu);
    if (op == 0xDE) {
      io(apu);
      value = (Uint8)(value + core->x);
    }
    value = rd(apu, dp(apu, value));
    other = fetch(apu);
    io(apu);
    branch(apu, other, value != core->a);
    break;
  case 0x6E: /* DBNZ dp,rel */
    addr = dp(apu, fetch(apu));
    value = (Uint8)(rd(apu, addr) - 1u);
    wr(apu, addr, value);
    other = fetch(apu);
    branch(apu, other, value != 0);
    break;
  case 0xFE: /* DBNZ Y,rel */
    other = fetch(apu);
    io(apu);
    io(apu);
    core->y = (Uint8)(core->y - 1u);
    branch(apu, other, core->y != 0);
    break;
  case 0x5F: /* JMP !abs */
    core->pc = fetch16(apu);
    break;
  case 0x1F: /* JMP [!abs+X] */
    addr = (Uint16)(fetch16(apu) + core->x);
    io(apu);
    value = rd(apu, addr);
    core->pc = (Uint16)(value | ((Uint16)rd(apu, (Uint16)(addr + 1u)) << 8));
    break;
  case 0x3F: /* CALL !abs */
    addr = fetch16(apu);
    io(apu);
    push(apu, (Uint8)(core->pc >> 8));
    push(apu, (Uint8)core->pc);
    io(apu);
    io(apu);
    core->pc = addr;
    break;
  case 0x4F: /* PCALL up */
    value = fetch(apu);
    io(apu);
    push(apu, (Uint8)(core->pc >> 8));
    push(apu, (Uint8)core->pc);
    io(apu);
    core->pc = (Uint16)(0xFF00u | value);
    break;
  case 0x0F: /* BRK */
    io(apu);
    push(apu, (Uint8)(core->pc >> 8));
    push(apu, (Uint8)core->pc);
    push(apu, core->psw);
    io(apu);
    value = rd(apu, 0xFFDE);
    core->pc = (Uint16)(value | ((Uint16)rd(apu, 0xFFDF) << 8));
    set_flag(apu, SMX_SPC_B, true);
    set_flag(apu, SMX_SPC_I, false);
    break;
  case 0x6F: /* RET */
    io(apu);
    io(apu);
    value = pull(apu);
    core->pc = (Uint16)(value | ((Uint16)pull(apu) << 8));
    break;
  case 0x7F: /* RETI */
    io(apu);
    io(apu);
    core->psw = pull(apu);
    value = pull(apu);
    core->pc = (Uint16)(value | ((Uint16)pull(apu) << 8));
    break;
  case 0x2D: /* PUSH A */
  case 0x4D: /* PUSH X */
  case 0x6D: /* PUSH Y */
  case 0x0D: /* PUSH PSW */
    io(apu);
    push(apu, op == 0x2D ? core->a : op == 0x4D ? core->x : op == 0x6D ? core->y : core->psw);
    io(apu);
    break;
  case 0xAE: /* POP A */
  case 0xCE: /* POP X */
  case 0xEE: /* POP Y */
  case 0x8E: /* POP PSW */
    io(apu);
    io(apu);
    value = pull(apu);
    if (op == 0xAE) {
      core->a = value;
    } else if (op == 0xCE) {
      core->x = value;
    } else if (op == 0xEE) {
      core->y = value;
    } else {
      core->psw = value;
    }
    break;
  case 0xE8: /* MOV A,#imm */
    core->a = fetch(apu);
    nz(apu, core->a);
    break;
  case 0xBF: /* MOV A,(X)+ */
    io(apu);
    core->a = rd(apu, dp(apu, core->x));
    io(apu);
    core->x = (Uint8)(core->x + 1u);
    nz(apu, core->a);
    break;
  case 0xAF: /* MOV (X)+,A */
    io(apu);
    io(apu);
    wr(apu, dp(apu, core->x), core->a);
    core->x = (Uint8)(core->x + 1u);
    break;
  case 0xCD: /* MOV X,#imm */
  case 0x8D: /* MOV Y,#imm */
    value = fetch(apu);
    if (op == 0xCD) {
      core->x = value;
    } else {
      core->y = value;
    }
    nz(apu, value);
    break;
  case 0xF8: /* MOV X,dp */
  case 0xF9: /* MOV X,dp+Y */
  case 0xE9: /* MOV X,!abs */
  case 0xEB: /* MOV Y,dp */
  case 0xFB: /* MOV Y,dp+X */
  case 0xEC: /* MOV Y,!abs */
    if (op == 0xE9 || op == 0xEC) {
      addr = fetch16(apu);
    } else {
      value = fetch(apu);
      if (op == 0xF9 || op == 0xFB) {
        io(apu);
        value = (Uint8)(value + (op == 0xF9 ? core->y : core->x));
      }
      addr = dp(apu, value);
    }
    value = rd(apu, addr);
    if (op == 0xF8 || op == 0xF9 || op == 0xE9) {
      core->x = value;
    } else {
      core->y = value;
    }
    nz(apu, value);
    break;
  case 0xD8: /* MOV dp,X */
  case 0xD9: /* MOV dp+Y,X */
  case 0xC9: /* MOV !abs,X */
  case 0xCB: /* MOV dp,Y */
  case 0xDB: /* MOV dp+X,Y */
  case 0xCC: /* MOV !abs,Y */
    if (op == 0xC9 || op == 0xCC) {
      addr = fetch16(apu);
    } else {
      value = fetch(apu);
      if (op == 0xD9 || op == 0xDB) {
        io(apu);
        value = (Uint8)(value + (op == 0xD9 ? core->y : core->x));
      }
      addr = dp(apu, value);
    }
    rd(apu, addr);
    wr(apu, addr, (op == 0xD8 || op == 0xD9 || op == 0xC9) ? core->x : core->y);
    break;
  case 0xC8: /* CMP X,#imm */
  case 0x3E: /* CMP X,dp */
  case 0x1E: /* CMP X,!abs */
  case 0xAD: /* CMP Y,#imm */
  case 0x7E: /* CMP Y,dp */
  case 0x5E: /* CMP Y,!abs */
    if (op == 0xC8 || op == 0xAD) {
      value = fetch(apu);
    } else if (op == 0x3E || op == 0x7E) {
      value = rd(apu, dp(apu, fetch(apu)));
    } else {
      value = rd(apu, fetch16(apu));
    }
    alu(apu, 0x60, (op == 0xC8 || op == 0x3E || op == 0x1E) ? core->x : core->y, value);
    break;
  case 0xFA: /* MOV dp,dp */
    value = rd(apu, dp(apu, fetch(apu)));
    wr(apu, dp(apu, fetch(apu)), value);
    break;
  case 0x8F: /* MOV dp,#imm */
    value = fetch(apu);
    addr = dp(apu, fetch(apu));
    rd(apu, addr);
    wr(apu, addr, value);
    break;
  case 0x7D: /* MOV A,X */
  case 0xDD: /* MOV A,Y */
  case 0x5D: /* MOV X,A */
  case 0xFD: /* MOV Y,A */
  case 0x9D: /* MOV X,SP */
  case 0xBD: /* MOV SP,X */
    io(apu);
    if (op == 0x7D) {
      core->a = core->x;
      nz(apu, core->a);
    } else if (op == 0xDD) {
      core->a = core->y;
      nz(apu, core->a);
    } else if (op == 0x5D) {
      core->x = core->a;
      nz(apu, core->x);
    } else if (op == 0xFD) {
      core->y = core->a;
      nz(apu, core->y);
    } else if (op == 0x9D) {
      core->x = core->sp;
      nz(apu, core->x);
    } else {
      core->sp = core->x;
    }
    break;
  case 0xBC: /* INC A */
  case 0x3D: /* INC X */
  case 0xFC: /* INC Y */
  case 0x9C: /* DEC A */
  case 0x1D: /* DEC X */
  case 0xDC: /* DEC Y */ {
    Uint8 *reg = (op == 0xBC || op == 0x9C)   ? &core->a
                 : (op == 0x3D || op == 0x1D) ? &core->x
                                              : &core->y;
    io(apu);
    *reg = (Uint8)(*reg + ((op == 0xBC || op == 0x3D || op == 0xFC) ? 1u : 0xFFu));
    nz(apu, *reg);
    break;
  }
  case 0xAB: /* INC dp */
  case 0x8B: /* DEC dp */
  case 0xBB: /* INC dp+X */
  case 0x9B: /* DEC dp+X */
  case 0xAC: /* INC !abs */
  case 0x8C: /* DEC !abs */
    if (op == 0xAC || op == 0x8C) {
      addr = fetch16(apu);
    } else {
      value = fetch(apu);
      if (op == 0xBB || op == 0x9B) {
        io(apu);
        value = (Uint8)(value + core->x);
      }
      addr = dp(apu, value);
    }
    value = (Uint8)(rd(apu, addr) + ((op == 0xAB || op == 0xBB || op == 0xAC) ? 1u : 0xFFu));
    wr(apu, addr, value);
    nz(apu, value);
    break;
  case 0x1C: /* ASL A */
  case 0x3C: /* ROL A */
  case 0x5C: /* LSR A */
  case 0x7C: /* ROR A */
    io(apu);
    core->a = shift(apu, row, core->a);
    break;
  case 0x0B: /* ASL dp */
  case 0x2B: /* ROL dp */
  case 0x4B: /* LSR dp */
  case 0x6B: /* ROR dp */
  case 0x1B: /* ASL dp+X */
  case 0x3B: /* ROL dp+X */
  case 0x5B: /* LSR dp+X */
  case 0x7B: /* ROR dp+X */
  case 0x0C: /* ASL !abs */
  case 0x2C: /* ROL !abs */
  case 0x4C: /* LSR !abs */
  case 0x6C: /* ROR !abs */
    if ((op & 0x0F) == 0x0C) {
      addr = fetch16(apu);
    } else {
      value = fetch(apu);
      if ((op & 0x10) != 0) {
        io(apu);
        value = (Uint8)(value + core->x);
      }
      addr = dp(apu, value);
    }
    value = shift(apu, (Uint8)(row & 0x60), rd(apu, addr));
    wr(apu, addr, value);
    break;
  case 0x0E: /* TSET1 !abs */
  case 0x4E: /* TCLR1 !abs */
    addr = fetch16(apu);
    value = rd(apu, addr);
    nz(apu, (Uint8)(core->a - value));
    rd(apu, addr);
    wr(apu, addr, op == 0x0E ? (Uint8)(value | core->a) : (Uint8)(value & (Uint8)~core->a));
    break;
  case 0x0A: /* OR1 C,mem.bit */
  case 0x2A: /* OR1 C,/mem.bit */
  case 0x4A: /* AND1 C,mem.bit */
  case 0x6A: /* AND1 C,/mem.bit */
  case 0x8A: /* EOR1 C,mem.bit */
  case 0xAA: /* MOV1 C,mem.bit */
  case 0xCA: /* MOV1 mem.bit,C */
  case 0xEA: /* NOT1 mem.bit */ {
    bool carry = (core->psw & SMX_SPC_C) != 0;
    bool set = false;
    word = fetch16(apu);
    bit = (Uint8)(word >> 13);
    addr = (Uint16)(word & 0x1FFF);
    value = rd(apu, addr);
    set = ((value >> bit) & 1u) != 0;
    if (op == 0x2A || op == 0x6A) {
      set = !set;
    }
    if (op == 0x0A || op == 0x2A) {
      io(apu);
      set_flag(apu, SMX_SPC_C, carry || set);
    } else if (op == 0x4A || op == 0x6A) {
      set_flag(apu, SMX_SPC_C, carry && set);
    } else if (op == 0x8A) {
      io(apu);
      set_flag(apu, SMX_SPC_C, carry != set);
    } else if (op == 0xAA) {
      set_flag(apu, SMX_SPC_C, set);
    } else if (op == 0xCA) {
      io(apu);
      value = carry ? (Uint8)(value | (1u << bit)) : (Uint8)(value & (Uint8) ~(1u << bit));
      wr(apu, addr, value);
    } else {
      wr(apu, addr, (Uint8)(value ^ (1u << bit)));
    }
    break;
  }
  case 0xBA: /* MOVW YA,dp */
    value = fetch(apu);
    other = rd(apu, dp(apu, value));
    io(apu);
    core->y = rd(apu, dp(apu, (Uint8)(value + 1u)));
    core->a = other;
    nz16(apu, (Uint16)(core->a | ((Uint16)core->y << 8)));
    break;
  case 0xDA: /* MOVW dp,YA */
    value = fetch(apu);
    rd(apu, dp(apu, value));
    wr(apu, dp(apu, value), core->a);
    wr(apu, dp(apu, (Uint8)(value + 1u)), core->y);
    break;
  case 0x3A: /* INCW dp */
  case 0x1A: /* DECW dp */
    value = fetch(apu);
    word = rd(apu, dp(apu, value));
    word = (Uint16)(word + (op == 0x3A ? 1u : 0xFFFFu));
    wr(apu, dp(apu, value), (Uint8)word);
    /* The low byte's carry or borrow reaches the high byte read after it. */
    word = (Uint16)(word + ((Uint16)rd(apu, dp(apu, (Uint8)(value + 1u))) << 8));
    wr(apu, dp(apu, (Uint8)(value + 1u)), (Uint8)(word >> 8));
    nz16(apu, word);
    break;
  case 0x7A: /* ADDW YA,dp */
  case 0x9A: /* SUBW YA,dp */
  case 0x5A: /* CMPW YA,dp */ {
    Uint16 ya = (Uint16)(core->a | ((Uint16)core->y << 8));
    Uint32 result = 0;
    value = fetch(apu);
    word = rd(apu, dp(apu, value));
    if (op != 0x5A) {
      io(apu);
    }
    word = (Uint16)(word | ((Uint16)rd(apu, dp(apu, (Uint8)(value + 1u))) << 8));
    if (op == 0x5A) {
      set_flag(apu, SMX_SPC_C, ya >= word);
      nz16(apu, (Uint16)(ya - word));
      break;
    }
    if (op == 0x9A) {
      word = (Uint16)~word;
      result = (Uint32)ya + word + 1u;
    } else {
      result = (Uint32)ya + word;
    }
    set_flag(apu, SMX_SPC_C, result > 0xFFFF);
    set_flag(apu, SMX_SPC_V, ((~(ya ^ word)) & (ya ^ (Uint16)result) & 0x8000) != 0);
    set_flag(apu, SMX_SPC_H, ((ya ^ word ^ (Uint16)result) & 0x1000) != 0);
    core->a = (Uint8)result;
    core->y = (Uint8)(result >> 8);
    nz16(apu, (Uint16)result);
    break;
  }
  case 0xCF: /* MUL YA */ {
    Uint8 i = 0;
    for (; i < 8; i += 1) {
      io(apu);
    }
    word = (Uint16)((Uint16)core->y * core->a);
    core->a = (Uint8)word;
    core->y = (Uint8)(word >> 8);
    nz(apu, core->y);
    break;
  }
  case 0x9E: /* DIV YA,X */ {
    Uint16 ya = (Uint16)(core->a | ((Uint16)core->y << 8));
    Uint8 i = 0;
    for (; i < 11; i += 1) {
      io(apu);
    }
    set_flag(apu, SMX_SPC_H, (core->y & 0x0F) >= (core->x & 0x0F));
    set_flag(apu, SMX_SPC_V, core->y >= core->x);
    /* The divider runs nine steps, so a quotient that overflows comes out this way. */
    if ((Uint16)core->y < ((Uint16)core->x << 1)) {
      core->a = (Uint8)(ya / core->x);
      core->y = (Uint8)(ya % core->x);
    } else {
      Uint32 rest = (Uint32)ya - ((Uint32)core->x << 9);
      core->a = (Uint8)(255u - rest / (256u - core->x));
      core->y = (Uint8)(core->x + rest % (256u - core->x));
    }
    nz(apu, core->a);
    break;
  }
  case 0xDF: /* DAA */
    io(apu);
    io(apu);
    if ((core->psw & SMX_SPC_C) != 0 || core->a > 0x99) {
      core->a = (Uint8)(core->a + 0x60u);
      set_flag(apu, SMX_SPC_C, true);
    }
    if ((core->psw & SMX_SPC_H) != 0 || (core->a & 0x0F) > 9) {
      core->a = (Uint8)(core->a + 6u);
    }
    nz(apu, core->a);
    break;
  case 0xBE: /* DAS */
    io(apu);
    io(apu);
    if ((core->psw & SMX_SPC_C) == 0 || core->a > 0x99) {
      core->a = (Uint8)(core->a - 0x60u);
      set_flag(apu, SMX_SPC_C, false);
    }
    if ((core->psw & SMX_SPC_H) == 0 || (core->a & 0x0F) > 9) {
      core->a = (Uint8)(core->a - 6u);
    }
    nz(apu, core->a);
    break;
  case 0x9F: /* XCN A */
    io(apu);
    io(apu);
    io(apu);
    io(apu);
    core->a = (Uint8)((core->a << 4) | (core->a >> 4));
    nz(apu, core->a);
    break;
  default:
    SDL_Log("spc: unimplemented opcode %u at %u", (unsigned)op, (unsigned)(core->pc - 1u));
    core->running = false;
    break;
  }
}

/* One instruction. False when it read a port past `limit` and was undone. */
static bool smx_spc_instruction(SmxApu *apu) {
  SmxSpcCore saved;
  Uint64 start = apu->core.cycle;

  SDL_memcpy(&saved, &apu->core, sizeof(saved));
  apu->blocked = false;
  apu->undo_count = 0;
  smx_spc_execute(apu);
  if (apu->blocked) {
    while (apu->undo_count > 0) {
      apu->undo_count = (Uint8)(apu->undo_count - 1u);
      apu->ram[apu->undo_addr[apu->undo_count]] = apu->undo_value[apu->undo_count];
    }
    SDL_memcpy(&apu->core, &saved, sizeof(saved));
    apu->blocked = false;
    return false;
  }
  apu->cycles = (Uint8)(apu->core.cycle - start);
  smx_spc_dsp_catch_up(apu);
  if (!apu->core.program_ready && apu->core.pc < 0xFFC0) {
    apu->core.program_ready = true;
    apu->core.entry = apu->core.pc;
  }
  return true;
}

bool smx_apu_step(SmxApu *apu) {
  Uint64 limit = apu->limit;

  if (!apu->core.running) {
    return false;
  }
  apu->limit = SMX_APU_NEVER;
  smx_spc_instruction(apu);
  apu->limit = limit;
  return apu->core.running;
}

void smx_apu_run(SmxApu *apu, Uint64 limit) {
  apu->limit = limit;
  while (smx_spc_now(apu) < limit) {
    if (!apu->core.running) {
      /* A stopped core still clocks the timers and the DSP. */
      smx_spc_timers(apu);
      smx_spc_tick(apu);
      smx_spc_dsp_catch_up(apu);
      continue;
    }
    if (!smx_spc_instruction(apu)) {
      break;
    }
  }
}
