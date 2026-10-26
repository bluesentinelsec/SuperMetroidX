#include "smx/cpu.h"

enum {
  SMX_C = 0x01,
  SMX_Z = 0x02,
  SMX_I = 0x04,
  SMX_D = 0x08,
  SMX_X = 0x10,
  SMX_M = 0x20,
  SMX_V = 0x40,
  SMX_N = 0x80
};

static bool m8(const SmxCpu *cpu) {
  return cpu->e || (cpu->p & SMX_M) != 0;
}

static bool x8(const SmxCpu *cpu) {
  return cpu->e || (cpu->p & SMX_X) != 0;
}

static void nz8(SmxCpu *cpu, Uint8 value) {
  cpu->p =
      (Uint8)((cpu->p & (Uint8) ~(SMX_N | SMX_Z)) | (value & SMX_N) | (value == 0 ? SMX_Z : 0));
}

static void nz16(SmxCpu *cpu, Uint16 value) {
  cpu->p = (Uint8)((cpu->p & (Uint8) ~(SMX_N | SMX_Z)) | ((value >> 8) & SMX_N) |
                   (value == 0 ? SMX_Z : 0));
}

static void apply_p(SmxCpu *cpu, Uint8 p) {
  bool was_x8 = x8(cpu);

  cpu->p = p;
  if (cpu->e) {
    cpu->p = (Uint8)(cpu->p | SMX_M | SMX_X);
    cpu->s = (Uint16)(0x0100 | (cpu->s & 0x00FF));
  }
  if (!was_x8 && x8(cpu)) {
    cpu->x = (Uint16)(cpu->x & 0x00FF);
    cpu->y = (Uint16)(cpu->y & 0x00FF);
  }
}

/* Each cycle records where it starts before it is spent. */
static void cycle(SmxCpu *cpu, Uint32 clocks) {
  cpu->last_at = cpu->clocks;
  cpu->clocks += clocks;
}

static void io(SmxCpu *cpu) {
  cycle(cpu, 6u);
}

static Uint8 fetch8(SmxCpu *cpu) {
  Uint32 addr = ((Uint32)cpu->pb << 16) | cpu->pc;
  Uint8 value = smx_bus_read(cpu->bus, addr);
  cycle(cpu, smx_bus_speed(cpu->bus, addr));
  cpu->pc = (Uint16)(cpu->pc + 1);
  return value;
}

static Uint16 fetch16(SmxCpu *cpu) {
  Uint8 lo = fetch8(cpu);
  Uint8 hi = fetch8(cpu);
  return (Uint16)(lo | ((Uint16)hi << 8));
}

static Uint8 read8(SmxCpu *cpu, Uint32 addr) {
  cycle(cpu, smx_bus_speed(cpu->bus, addr & 0x00FFFFFFu));
  return smx_bus_read(cpu->bus, addr & 0x00FFFFFFu);
}

static void write8(SmxCpu *cpu, Uint32 addr, Uint8 value) {
  cycle(cpu, smx_bus_speed(cpu->bus, addr & 0x00FFFFFFu));
  smx_bus_write(cpu->bus, addr & 0x00FFFFFFu, value);
}

static Uint16 read16p(SmxCpu *cpu, Uint8 bank, Uint16 addr) {
  Uint8 lo = read8(cpu, ((Uint32)bank << 16) | addr);
  Uint8 hi = read8(cpu, ((Uint32)bank << 16) | (Uint16)(addr + 1));
  return (Uint16)(lo | ((Uint16)hi << 8));
}

static void write16p(SmxCpu *cpu, Uint8 bank, Uint16 addr, Uint16 value) {
  write8(cpu, ((Uint32)bank << 16) | addr, (Uint8)value);
  write8(cpu, ((Uint32)bank << 16) | (Uint16)(addr + 1), (Uint8)(value >> 8));
}

static Uint16 read16_24(SmxCpu *cpu, Uint32 addr) {
  Uint8 lo = read8(cpu, addr);
  Uint8 hi = read8(cpu, addr + 1);
  return (Uint16)(lo | ((Uint16)hi << 8));
}

static void write16_24(SmxCpu *cpu, Uint32 addr, Uint16 value) {
  write8(cpu, addr, (Uint8)value);
  write8(cpu, addr + 1, (Uint8)(value >> 8));
}

static Uint32 read24p(SmxCpu *cpu, Uint16 addr) {
  Uint8 lo = read8(cpu, addr);
  Uint8 mid = read8(cpu, (Uint16)(addr + 1));
  Uint8 hi = read8(cpu, (Uint16)(addr + 2));
  return (Uint32)lo | ((Uint32)mid << 8) | ((Uint32)hi << 16);
}

static void push8(SmxCpu *cpu, Uint8 value) {
  write8(cpu, cpu->s, value);
  if (cpu->e) {
    cpu->s = (Uint16)(0x0100 | ((cpu->s - 1) & 0x00FF));
  } else {
    cpu->s = (Uint16)(cpu->s - 1);
  }
}

static Uint8 pull8(SmxCpu *cpu) {
  if (cpu->e) {
    cpu->s = (Uint16)(0x0100 | ((cpu->s + 1) & 0x00FF));
  } else {
    cpu->s = (Uint16)(cpu->s + 1);
  }
  return read8(cpu, cpu->s);
}

static void push16(SmxCpu *cpu, Uint16 value) {
  push8(cpu, (Uint8)(value >> 8));
  push8(cpu, (Uint8)value);
}

static Uint16 pull16(SmxCpu *cpu) {
  Uint8 lo = pull8(cpu);
  Uint8 hi = pull8(cpu);
  return (Uint16)(lo | ((Uint16)hi << 8));
}

static Uint16 idx_x(const SmxCpu *cpu) {
  return x8(cpu) ? (Uint16)(cpu->x & 0x00FF) : cpu->x;
}

static Uint16 idx_y(const SmxCpu *cpu) {
  return x8(cpu) ? (Uint16)(cpu->y & 0x00FF) : cpu->y;
}

static Uint16 dp_wrap(const SmxCpu *cpu, Uint16 offset) {
  if (cpu->e && (cpu->d & 0x00FF) == 0) {
    return (Uint16)((cpu->d & 0xFF00) | ((cpu->d + offset) & 0x00FF));
  }
  return (Uint16)(cpu->d + offset);
}

static Uint32 ea_bank(const SmxCpu *cpu, Uint16 addr) {
  return ((Uint32)cpu->db << 16) | addr;
}

/* Direct page modes spend a cycle when the low byte of D is not zero. */
static void io_dp(SmxCpu *cpu) {
  if ((cpu->d & 0x00FF) != 0) {
    io(cpu);
  }
}

/* An indexed read spends a cycle for 16-bit indexes or a page crossing. A write always does. */
static void io_index(SmxCpu *cpu, Uint16 base, Uint16 indexed, bool write) {
  if (write || !x8(cpu) || (base & 0xFF00) != (indexed & 0xFF00)) {
    io(cpu);
  }
}

static bool writes_indexed(Uint8 op) {
  return op == 0x91 || op == 0x99 || op == 0x9D || op == 0x9E || op == 0x1E || op == 0x3E ||
         op == 0x5E || op == 0x7E || op == 0xDE || op == 0xFE;
}

/* Operand bytes after the opcode for the modes `smx_cpu_address` resolves. */
static Uint8 operand_bytes(Uint8 op) {
  switch (op & 0x1F) {
  case 0x0D:
  case 0x0E:
  case 0x0C:
  case 0x1C:
  case 0x19:
  case 0x1D:
  case 0x1E:
    return 2;
  case 0x0F:
  case 0x1F:
    return 3;
  default:
    return 1;
  }
}

Uint32 smx_cpu_address(SmxCpu *cpu, Uint8 op, Uint32 operand) {
  Uint8 mode = (Uint8)(op & 0x1F);
  Uint8 dp = (Uint8)operand;
  Uint16 abs = (Uint16)operand;
  Uint16 ptr = 0;
  bool dp_index = op == 0x34 || op == 0x74 || op == 0x94 || op == 0xB4;
  bool abs_index = op == 0x3C || op == 0xBC;

  switch (mode) {
  case 0x01: /* (dp,X) */
    io_dp(cpu);
    io(cpu);
    ptr = read16p(cpu, 0, dp_wrap(cpu, (Uint16)(dp + idx_x(cpu))));
    return ea_bank(cpu, ptr);
  case 0x11: /* (dp),Y */
    io_dp(cpu);
    ptr = read16p(cpu, 0, dp_wrap(cpu, dp));
    io_index(cpu, ptr, (Uint16)(ptr + idx_y(cpu)), writes_indexed(op));
    return ea_bank(cpu, (Uint16)(ptr + idx_y(cpu)));
  case 0x03: /* sr,S */
    io(cpu);
    return (Uint16)(cpu->s + dp);
  case 0x13: /* (sr,S),Y */
    io(cpu);
    io(cpu);
    ptr = read16p(cpu, 0, (Uint16)(cpu->s + dp));
    return ea_bank(cpu, (Uint16)(ptr + idx_y(cpu)));
  case 0x05: /* dp */
  case 0x06:
  case 0x04:
  case 0x14:
    io_dp(cpu);
    if (dp_index) {
      io(cpu);
      return dp_wrap(cpu, (Uint16)(dp + idx_x(cpu)));
    }
    return dp_wrap(cpu, dp);
  case 0x07: /* [dp] */
    io_dp(cpu);
    return read24p(cpu, dp_wrap(cpu, dp));
  case 0x12: /* (dp) */
    io_dp(cpu);
    ptr = read16p(cpu, 0, dp_wrap(cpu, dp));
    return ea_bank(cpu, ptr);
  case 0x15: /* dp,X */
  case 0x16:
    io_dp(cpu);
    io(cpu);
    if (op == 0x96 || op == 0xB6) {
      return dp_wrap(cpu, (Uint16)(dp + idx_y(cpu)));
    }
    return dp_wrap(cpu, (Uint16)(dp + idx_x(cpu)));
  case 0x17: /* [dp],Y */
    io_dp(cpu);
    return (read24p(cpu, dp_wrap(cpu, dp)) + idx_y(cpu)) & 0x00FFFFFFu;
  case 0x0D: /* abs */
  case 0x0E:
  case 0x0C:
  case 0x1C:
    if (abs_index) {
      io_index(cpu, abs, (Uint16)(abs + idx_x(cpu)), false);
      return ea_bank(cpu, (Uint16)(abs + idx_x(cpu)));
    }
    return ea_bank(cpu, abs);
  case 0x0F: /* long */
    return operand & 0x00FFFFFFu;
  case 0x19: /* abs,Y */
    io_index(cpu, abs, (Uint16)(abs + idx_y(cpu)), writes_indexed(op));
    return ea_bank(cpu, (Uint16)(abs + idx_y(cpu)));
  case 0x1D: /* abs,X */
  case 0x1E:
    if (op == 0xBE) {
      io_index(cpu, abs, (Uint16)(abs + idx_y(cpu)), false);
      return ea_bank(cpu, (Uint16)(abs + idx_y(cpu)));
    }
    io_index(cpu, abs, (Uint16)(abs + idx_x(cpu)), writes_indexed(op));
    return ea_bank(cpu, (Uint16)(abs + idx_x(cpu)));
  case 0x1F: /* long,X */
    return (operand + idx_x(cpu)) & 0x00FFFFFFu;
  default:
    return 0;
  }
}

/** Fetch the operand of a non-immediate mode, then resolve its effective address. */
static Uint32 address_for(SmxCpu *cpu, Uint8 op) {
  Uint8 bytes = operand_bytes(op);
  Uint32 operand = fetch8(cpu);

  if (bytes > 1) {
    operand |= (Uint32)fetch8(cpu) << 8;
  }
  if (bytes > 2) {
    operand |= (Uint32)fetch8(cpu) << 16;
  }
  return smx_cpu_address(cpu, op, operand);
}

static Uint16 read_ea(SmxCpu *cpu, Uint32 addr) {
  if (m8(cpu)) {
    return read8(cpu, addr);
  }
  if ((addr & 0x0000FFFFu) == 0x0000FFFFu && (addr & 0x00FF0000u) == ((Uint32)cpu->db << 16)) {
    return read16p(cpu, cpu->db, 0xFFFF);
  }
  return read16_24(cpu, addr);
}

static void write_ea(SmxCpu *cpu, Uint32 addr, Uint16 value) {
  if (m8(cpu)) {
    write8(cpu, addr, (Uint8)value);
    return;
  }
  if ((addr & 0x0000FFFFu) == 0x0000FFFFu && (addr & 0x00FF0000u) == ((Uint32)cpu->db << 16)) {
    write16p(cpu, cpu->db, 0xFFFF, value);
    return;
  }
  write16_24(cpu, addr, value);
}

static Uint16 fetch_imm(SmxCpu *cpu, bool wide) {
  if (!wide) {
    return fetch8(cpu);
  }
  return fetch16(cpu);
}

static void set_a(SmxCpu *cpu, Uint16 value) {
  if (m8(cpu)) {
    cpu->a = (Uint16)((cpu->a & 0xFF00) | (value & 0x00FF));
    nz8(cpu, (Uint8)value);
  } else {
    cpu->a = value;
    nz16(cpu, value);
  }
}

static void set_carry(SmxCpu *cpu, bool carry) {
  cpu->p = (Uint8)(carry ? (cpu->p | SMX_C) : (cpu->p & (Uint8)~SMX_C));
}

static void set_overflow(SmxCpu *cpu, bool overflow) {
  cpu->p = (Uint8)(overflow ? (cpu->p | SMX_V) : (cpu->p & (Uint8)~SMX_V));
}

static void adc(SmxCpu *cpu, Uint16 operand) {
  bool carry_in = (cpu->p & SMX_C) != 0;

  if (m8(cpu)) {
    Uint8 a = (Uint8)cpu->a;
    Uint8 b = (Uint8)operand;
    if ((cpu->p & SMX_D) != 0) {
      Uint16 lo = (Uint16)((a & 0x0F) + (b & 0x0F) + (carry_in ? 1 : 0));
      Uint16 hi = (Uint16)((a >> 4) + (b >> 4));
      if (lo > 9) {
        lo = (Uint16)(lo + 6);
        hi = (Uint16)(hi + 1);
      }
      set_overflow(cpu, ((a ^ (lo | (hi << 4))) & (b ^ (lo | (hi << 4))) & 0x80) != 0);
      if (hi > 9) {
        hi = (Uint16)(hi + 6);
        set_carry(cpu, true);
      } else {
        set_carry(cpu, false);
      }
      set_a(cpu, (Uint16)(((hi & 0x0F) << 4) | (lo & 0x0F)));
      return;
    }
    {
      Uint16 sum = (Uint16)(a + b + (carry_in ? 1 : 0));
      set_overflow(cpu, (~(a ^ b) & (a ^ sum) & 0x80) != 0);
      set_carry(cpu, sum > 0xFF);
      set_a(cpu, (Uint8)sum);
    }
    return;
  }

  if ((cpu->p & SMX_D) != 0) {
    Uint8 carry = carry_in ? 1 : 0;
    Uint16 lo = (Uint16)((cpu->a & 0x000F) + (operand & 0x000F) + carry);
    Uint16 d1 = (Uint16)(((cpu->a >> 4) & 0x0F) + ((operand >> 4) & 0x0F));
    Uint16 d2 = (Uint16)(((cpu->a >> 8) & 0x0F) + ((operand >> 8) & 0x0F));
    Uint16 d3 = (Uint16)(((cpu->a >> 12) & 0x0F) + ((operand >> 12) & 0x0F));
    if (lo > 9) {
      lo = (Uint16)(lo + 6);
      d1 = (Uint16)(d1 + 1);
    }
    if (d1 > 9) {
      d1 = (Uint16)(d1 + 6);
      d2 = (Uint16)(d2 + 1);
    }
    if (d2 > 9) {
      d2 = (Uint16)(d2 + 6);
      d3 = (Uint16)(d3 + 1);
    }
    set_carry(cpu, d3 > 9);
    if (d3 > 9) {
      d3 = (Uint16)(d3 + 6);
    }
    set_a(cpu,
          (Uint16)((lo & 0x0F) | ((d1 & 0x0F) << 4) | ((d2 & 0x0F) << 8) | ((d3 & 0x0F) << 12)));
    return;
  }

  {
    Uint32 sum = (Uint32)cpu->a + operand + (carry_in ? 1u : 0u);
    set_overflow(cpu, (~(cpu->a ^ operand) & (cpu->a ^ sum) & 0x8000) != 0);
    set_carry(cpu, sum > 0xFFFF);
    set_a(cpu, (Uint16)sum);
  }
}

static void sbc(SmxCpu *cpu, Uint16 operand) {
  bool borrow = (cpu->p & SMX_C) == 0;

  if (m8(cpu)) {
    Uint8 a = (Uint8)cpu->a;
    Uint8 b = (Uint8)operand;
    if ((cpu->p & SMX_D) != 0) {
      Sint16 lo = (Sint16)((a & 0x0F) - (b & 0x0F) - (borrow ? 1 : 0));
      Sint16 hi = (Sint16)((a >> 4) - (b >> 4));
      if (lo < 0) {
        lo = (Sint16)(lo + 10);
        hi = (Sint16)(hi - 1);
      }
      set_carry(cpu, hi >= 0);
      if (hi < 0) {
        hi = (Sint16)(hi + 10);
      }
      set_a(cpu, (Uint16)(((hi & 0x0F) << 4) | (lo & 0x0F)));
      return;
    }
    {
      Uint16 sum = (Uint16)(a - b - (borrow ? 1 : 0));
      set_overflow(cpu, ((a ^ b) & (a ^ sum) & 0x80) != 0);
      set_carry(cpu, a >= (Uint16)b + (borrow ? 1 : 0));
      set_a(cpu, (Uint8)sum);
    }
    return;
  }

  if ((cpu->p & SMX_D) != 0) {
    adc(cpu, (Uint16)~operand);
    return;
  }
  {
    Uint32 sum = (Uint32)cpu->a - operand - (borrow ? 1u : 0u);
    set_overflow(cpu, ((cpu->a ^ operand) & (cpu->a ^ sum) & 0x8000) != 0);
    set_carry(cpu, cpu->a >= (Uint32)operand + (borrow ? 1u : 0u));
    set_a(cpu, (Uint16)sum);
  }
}

static void cmp_value(SmxCpu *cpu, Uint16 left, Uint16 right, bool eight) {
  if (eight) {
    Uint8 result = (Uint8)((left & 0xFF) - (right & 0xFF));
    set_carry(cpu, (left & 0xFF) >= (right & 0xFF));
    nz8(cpu, result);
    return;
  }
  set_carry(cpu, left >= right);
  nz16(cpu, (Uint16)(left - right));
}

static Uint16 logic(SmxCpu *cpu, Uint8 op, Uint16 value) {
  Uint16 a = m8(cpu) ? (Uint16)(cpu->a & 0x00FF) : cpu->a;
  Uint16 mask = m8(cpu) ? 0x00FF : 0xFFFF;

  if ((op & 0xE0) == 0x00) {
    return (Uint16)((a | value) & mask);
  }
  if ((op & 0xE0) == 0x20) {
    return (Uint16)((a & value) & mask);
  }
  if ((op & 0xE0) == 0x40) {
    return (Uint16)((a ^ value) & mask);
  }
  if ((op & 0xE0) == 0xC0) {
    cmp_value(cpu, a, value, m8(cpu));
    return a;
  }
  return value;
}

static void branch8(SmxCpu *cpu, bool take) {
  Sint8 offset = (Sint8)fetch8(cpu);
  if (take) {
    Uint16 target = (Uint16)(cpu->pc + offset);
    io(cpu);
    if (cpu->e && (target & 0xFF00) != (cpu->pc & 0xFF00)) {
      io(cpu);
    }
    cpu->pc = target;
  }
}

static void set_x(SmxCpu *cpu, Uint16 value) {
  if (x8(cpu)) {
    cpu->x = (Uint16)(value & 0x00FF);
    nz8(cpu, (Uint8)cpu->x);
  } else {
    cpu->x = value;
    nz16(cpu, cpu->x);
  }
}

static void set_y(SmxCpu *cpu, Uint16 value) {
  if (x8(cpu)) {
    cpu->y = (Uint16)(value & 0x00FF);
    nz8(cpu, (Uint8)cpu->y);
  } else {
    cpu->y = value;
    nz16(cpu, cpu->y);
  }
}

static void shift_mem(SmxCpu *cpu, Uint8 op, Uint32 addr) {
  bool left = (op & 0xE0) == 0x00 || (op & 0xE0) == 0x20;
  bool rotate = (op & 0xE0) == 0x20 || (op & 0xE0) == 0x60;
  bool eight = m8(cpu);
  Uint16 value = eight ? read8(cpu, addr) : read16_24(cpu, addr);
  Uint16 result = value;

  io(cpu);
  Uint16 sign = eight ? 0x80 : 0x8000;
  Uint16 mask = eight ? 0xFF : 0xFFFF;
  bool carry = (cpu->p & SMX_C) != 0;

  if ((op & 0xE0) == 0x00 || (op & 0xE0) == 0x20) {
    set_carry(cpu, (value & sign) != 0);
    result = (Uint16)((value << 1) & mask);
    if (rotate && carry) {
      result = (Uint16)(result | 0x0001);
    }
  } else if ((op & 0xE0) == 0x40 || (op & 0xE0) == 0x60) {
    set_carry(cpu, (value & 0x0001) != 0);
    result = (Uint16)((value >> 1) & mask);
    if (rotate && carry) {
      result = (Uint16)(result | sign);
    }
  } else if ((op & 0xE0) == 0xE0 || op == 0x1A) {
    result = (Uint16)((value + 1) & mask);
  } else {
    result = (Uint16)((value - 1) & mask);
  }
  (void)left;
  if (eight) {
    write8(cpu, addr, (Uint8)result);
    nz8(cpu, (Uint8)result);
  } else {
    write16_24(cpu, addr, result);
    nz16(cpu, result);
  }
}

static void interrupt(SmxCpu *cpu, Uint16 native_vector, Uint16 emu_vector, bool software) {
  Uint16 vector = cpu->e ? emu_vector : native_vector;
  if (software) {
    fetch8(cpu);
  }
  if (!cpu->e) {
    push8(cpu, cpu->pb);
  }
  push16(cpu, cpu->pc);
  push8(cpu, cpu->p);
  cpu->p = (Uint8)(cpu->p | SMX_I);
  cpu->pb = 0;
  cpu->pc = read16p(cpu, 0, vector);
  cpu->waiting = false;
}

void smx_cpu_nmi(SmxCpu *cpu) {
  if (cpu->stopped) {
    return;
  }
  io(cpu);
  io(cpu);
  interrupt(cpu, 0xFFEA, 0xFFFA, false);
}

void smx_cpu_irq(SmxCpu *cpu) {
  if (cpu->stopped || (cpu->p & SMX_I) != 0) {
    return;
  }
  io(cpu);
  io(cpu);
  interrupt(cpu, 0xFFEE, 0xFFFE, false);
}

void smx_cpu_init(SmxCpu *cpu, SmxBus *bus) {
  SDL_memset(cpu, 0, sizeof(*cpu));
  cpu->bus = bus;
}

void smx_cpu_reset(SmxCpu *cpu) {
  cpu->e = true;
  cpu->p = (Uint8)(SMX_I | SMX_M | SMX_X);
  cpu->d = 0;
  cpu->db = 0;
  cpu->pb = 0;
  cpu->s = 0x01FF;
  cpu->a = 0;
  cpu->x = 0;
  cpu->y = 0;
  cpu->stopped = false;
  cpu->waiting = false;
  /* Mesen 2.1.1 fetches the first opcode 186 master clocks after power-on. */
  cpu->clocks = 170u;
  cpu->pc = read16p(cpu, 0, 0xFFFC);
}

/* Internal cycles of the one-byte, stack, and flag instructions in `run_implied`. */
static Uint8 implied_io(Uint8 op) {
  switch (op) {
  case 0x42:
  case 0xD4:
  case 0xF4:
    return 0;
  case 0x28:
  case 0x2B:
  case 0x68:
  case 0x7A:
  case 0xAB:
  case 0xCB:
  case 0xDB:
  case 0xEB:
  case 0xFA:
    return 2;
  default:
    return 1;
  }
}

/* Flags, transfers, stack, and accumulator ops. False when `op` is not one of them. */
static bool run_implied(SmxCpu *cpu, Uint8 op) {
  Uint16 value = 0;

  switch (op) {
  case 0x18:
    set_carry(cpu, false);
    return true;
  case 0x38:
    set_carry(cpu, true);
    return true;
  case 0x58:
    cpu->p = (Uint8)(cpu->p & (Uint8)~SMX_I);
    return true;
  case 0x78:
    cpu->p = (Uint8)(cpu->p | SMX_I);
    return true;
  case 0xB8:
    set_overflow(cpu, false);
    return true;
  case 0xD8:
    cpu->p = (Uint8)(cpu->p & (Uint8)~SMX_D);
    return true;
  case 0xF8:
    cpu->p = (Uint8)(cpu->p | SMX_D);
    return true;
  case 0xC2:
    apply_p(cpu, (Uint8)(cpu->p & (Uint8)~fetch8(cpu)));
    return true;
  case 0xE2:
    apply_p(cpu, (Uint8)(cpu->p | fetch8(cpu)));
    return true;
  case 0xEB:
    cpu->a = (Uint16)((cpu->a >> 8) | (cpu->a << 8));
    nz8(cpu, (Uint8)cpu->a);
    return true;
  case 0xFB: {
    bool carry = (cpu->p & SMX_C) != 0;
    bool emu = cpu->e;
    cpu->e = carry;
    set_carry(cpu, emu);
    apply_p(cpu, cpu->p);
    return true;
  }
  case 0xEA:
  case 0x42:
    if (op == 0x42) {
      fetch8(cpu);
    }
    return true;
  case 0xCB:
    cpu->waiting = true;
    return true;
  case 0xDB:
    cpu->stopped = true;
    return true;
  case 0x08:
    push8(cpu, cpu->p);
    return true;
  case 0x28:
    apply_p(cpu, pull8(cpu));
    return true;
  case 0x48:
    if (m8(cpu)) {
      push8(cpu, (Uint8)cpu->a);
    } else {
      push16(cpu, cpu->a);
    }
    return true;
  case 0x68:
    set_a(cpu, m8(cpu) ? pull8(cpu) : pull16(cpu));
    return true;
  case 0x5A:
    if (x8(cpu)) {
      push8(cpu, (Uint8)cpu->y);
    } else {
      push16(cpu, cpu->y);
    }
    return true;
  case 0x7A:
    set_y(cpu, x8(cpu) ? pull8(cpu) : pull16(cpu));
    return true;
  case 0xDA:
    if (x8(cpu)) {
      push8(cpu, (Uint8)cpu->x);
    } else {
      push16(cpu, cpu->x);
    }
    return true;
  case 0xFA:
    set_x(cpu, x8(cpu) ? pull8(cpu) : pull16(cpu));
    return true;
  case 0x0B:
    push16(cpu, cpu->d);
    return true;
  case 0x2B:
    cpu->d = pull16(cpu);
    nz16(cpu, cpu->d);
    return true;
  case 0x4B:
    push8(cpu, cpu->pb);
    return true;
  case 0x8B:
    push8(cpu, cpu->db);
    return true;
  case 0xAB:
    cpu->db = pull8(cpu);
    nz8(cpu, cpu->db);
    return true;
  case 0xF4:
    push16(cpu, fetch16(cpu));
    return true;
  case 0xD4:
    value = fetch8(cpu);
    io_dp(cpu);
    push16(cpu, read16p(cpu, 0, dp_wrap(cpu, value)));
    return true;
  case 0x62:
    value = fetch16(cpu);
    push16(cpu, (Uint16)(cpu->pc + value));
    return true;
  case 0xAA:
    set_x(cpu, cpu->a);
    return true;
  case 0xA8:
    set_y(cpu, cpu->a);
    return true;
  case 0x8A:
    set_a(cpu, x8(cpu) ? (Uint16)(cpu->x & 0xFF) : cpu->x);
    return true;
  case 0x98:
    set_a(cpu, x8(cpu) ? (Uint16)(cpu->y & 0xFF) : cpu->y);
    return true;
  case 0x9B:
    cpu->y = x8(cpu) ? (Uint16)(cpu->x & 0xFF) : cpu->x;
    if (x8(cpu)) {
      nz8(cpu, (Uint8)cpu->y);
    } else {
      nz16(cpu, cpu->y);
    }
    return true;
  case 0xBB:
    cpu->x = x8(cpu) ? (Uint16)(cpu->y & 0xFF) : cpu->y;
    if (x8(cpu)) {
      nz8(cpu, (Uint8)cpu->x);
    } else {
      nz16(cpu, cpu->x);
    }
    return true;
  case 0x9A:
    if (cpu->e) {
      cpu->s = (Uint16)(0x0100 | (cpu->x & 0x00FF));
    } else if (x8(cpu)) {
      cpu->s = (Uint16)((cpu->s & 0xFF00) | (cpu->x & 0x00FF));
    } else {
      cpu->s = cpu->x;
    }
    return true;
  case 0xBA:
    set_x(cpu, cpu->s);
    return true;
  case 0x1B:
    if (cpu->e) {
      cpu->s = (Uint16)(0x0100 | (cpu->a & 0x00FF));
    } else {
      cpu->s = cpu->a;
    }
    return true;
  case 0x3B:
    cpu->a = cpu->s;
    nz16(cpu, cpu->a);
    return true;
  case 0x5B:
    cpu->d = cpu->a;
    nz16(cpu, cpu->d);
    return true;
  case 0x7B:
    cpu->a = cpu->d;
    nz16(cpu, cpu->a);
    return true;
  case 0x0A:
  case 0x2A:
  case 0x4A:
  case 0x6A:
  case 0x1A:
  case 0x3A: {
    bool eight = m8(cpu);
    Uint16 current = eight ? (Uint16)(cpu->a & 0xFF) : cpu->a;
    Uint16 sign = eight ? 0x80 : 0x8000;
    Uint16 mask = eight ? 0xFF : 0xFFFF;
    Uint16 result = current;
    bool carry = (cpu->p & SMX_C) != 0;
    if (op == 0x0A || op == 0x2A) {
      set_carry(cpu, (current & sign) != 0);
      result = (Uint16)((current << 1) & mask);
      if (op == 0x2A && carry) {
        result = (Uint16)(result | 1);
      }
    } else if (op == 0x4A || op == 0x6A) {
      set_carry(cpu, (current & 1) != 0);
      result = (Uint16)(current >> 1);
      if (op == 0x6A && carry) {
        result = (Uint16)(result | sign);
      }
    } else if (op == 0x1A) {
      result = (Uint16)((current + 1) & mask);
    } else {
      result = (Uint16)((current - 1) & mask);
    }
    set_a(cpu, result);
    return true;
  }
  case 0x88:
    set_y(cpu, (Uint16)(idx_y(cpu) - 1));
    return true;
  case 0xC8:
    set_y(cpu, (Uint16)(idx_y(cpu) + 1));
    return true;
  case 0xCA:
    set_x(cpu, (Uint16)(idx_x(cpu) - 1));
    return true;
  case 0xE8:
    set_x(cpu, (Uint16)(idx_x(cpu) + 1));
    return true;
  default:
    return false;
  }
}

void smx_cpu_bit(SmxCpu *cpu, Uint8 op, Uint16 value) {
  Uint16 a = m8(cpu) ? (Uint16)(cpu->a & 0xFF) : cpu->a;
  Uint16 tested = (Uint16)(a & value);

  if (m8(cpu)) {
    nz8(cpu, (Uint8)tested);
    if (op != 0x89) {
      cpu->p = (Uint8)((cpu->p & (Uint8) ~(SMX_N | SMX_V)) | (value & (SMX_N | SMX_V)));
      if (tested == 0) {
        cpu->p = (Uint8)(cpu->p | SMX_Z);
      }
    }
  } else {
    nz16(cpu, tested);
    if (op != 0x89) {
      set_overflow(cpu, (value & 0x4000) != 0);
      cpu->p = (Uint8)((cpu->p & (Uint8)~SMX_N) | ((value >> 8) & SMX_N));
      if (tested == 0) {
        cpu->p = (Uint8)(cpu->p | SMX_Z);
      }
    }
  }
}

void smx_cpu_test_bits(SmxCpu *cpu, Uint32 addr, Uint16 mem, bool set) {
  Uint16 a = m8(cpu) ? (Uint16)(cpu->a & 0xFF) : cpu->a;

  if ((a & mem) == 0) {
    cpu->p = (Uint8)(cpu->p | SMX_Z);
  } else {
    cpu->p = (Uint8)(cpu->p & (Uint8)~SMX_Z);
  }
  mem = set ? (Uint16)(mem | a) : (Uint16)(mem & (Uint16)~a);
  io(cpu);
  if (m8(cpu)) {
    write8(cpu, addr, (Uint8)mem);
  } else {
    write16_24(cpu, addr, mem);
  }
}

bool smx_cpu_step(SmxCpu *cpu) {
  Uint8 op = 0;
  Uint32 addr = 0;
  Uint16 value = 0;

  if (cpu->stopped) {
    return false;
  }
  if (cpu->waiting) {
    io(cpu);
    return true;
  }

  op = fetch8(cpu);

  if (op == 0x00) {
    interrupt(cpu, 0xFFE6, 0xFFFE, true);
    return true;
  }
  if (op == 0x02) {
    interrupt(cpu, 0xFFE4, 0xFFF4, true);
    return true;
  }
  if (op == 0x40) {
    io(cpu);
    io(cpu);
    apply_p(cpu, pull8(cpu));
    cpu->pc = pull16(cpu);
    if (!cpu->e) {
      cpu->pb = pull8(cpu);
    }
    return true;
  }
  if (op == 0x60) {
    io(cpu);
    io(cpu);
    io(cpu);
    cpu->pc = (Uint16)(pull16(cpu) + 1);
    return true;
  }
  if (op == 0x6B) {
    io(cpu);
    io(cpu);
    cpu->pc = (Uint16)(pull16(cpu) + 1);
    cpu->pb = pull8(cpu);
    return true;
  }
  if (op == 0x20) {
    addr = fetch16(cpu);
    io(cpu);
    push16(cpu, (Uint16)(cpu->pc - 1));
    cpu->pc = (Uint16)addr;
    return true;
  }
  if (op == 0x22) {
    addr = fetch16(cpu);
    io(cpu);
    addr |= (Uint32)fetch8(cpu) << 16;
    push8(cpu, cpu->pb);
    push16(cpu, (Uint16)(cpu->pc - 1));
    cpu->pb = (Uint8)(addr >> 16);
    cpu->pc = (Uint16)addr;
    return true;
  }
  if (op == 0x4C) {
    cpu->pc = fetch16(cpu);
    return true;
  }
  if (op == 0x5C) {
    addr = fetch16(cpu);
    addr |= (Uint32)fetch8(cpu) << 16;
    cpu->pb = (Uint8)(addr >> 16);
    cpu->pc = (Uint16)addr;
    return true;
  }
  if (op == 0x6C) {
    addr = fetch16(cpu);
    cpu->pc = read16p(cpu, 0, (Uint16)addr);
    return true;
  }
  if (op == 0x7C) {
    addr = fetch16(cpu);
    io(cpu);
    cpu->pc = read16p(cpu, cpu->pb, (Uint16)(addr + idx_x(cpu)));
    return true;
  }
  if (op == 0xDC) {
    addr = fetch16(cpu);
    value = read16p(cpu, 0, (Uint16)addr);
    cpu->pb = read8(cpu, (Uint16)(addr + 2));
    cpu->pc = value;
    return true;
  }
  if (op == 0xFC) {
    addr = fetch16(cpu);
    io(cpu);
    addr = (Uint16)(addr + idx_x(cpu));
    push16(cpu, (Uint16)(cpu->pc - 1));
    cpu->pc = read16p(cpu, cpu->pb, (Uint16)addr);
    return true;
  }

  if ((op & 0x1F) == 0x10 || op == 0x80) {
    bool take = false;
    switch (op) {
    case 0x10:
      take = (cpu->p & SMX_N) == 0;
      break;
    case 0x30:
      take = (cpu->p & SMX_N) != 0;
      break;
    case 0x50:
      take = (cpu->p & SMX_V) == 0;
      break;
    case 0x70:
      take = (cpu->p & SMX_V) != 0;
      break;
    case 0x80:
      take = true;
      break;
    case 0x90:
      take = (cpu->p & SMX_C) == 0;
      break;
    case 0xB0:
      take = (cpu->p & SMX_C) != 0;
      break;
    case 0xD0:
      take = (cpu->p & SMX_Z) == 0;
      break;
    default:
      take = (cpu->p & SMX_Z) != 0;
      break;
    }
    branch8(cpu, take);
    return true;
  }

  if (op == 0x82) {
    value = fetch16(cpu);
    io(cpu);
    cpu->pc = (Uint16)(cpu->pc + (Sint16)value);
    return true;
  }

  if (run_implied(cpu, op)) {
    Uint8 cycles = implied_io(op);
    for (; cycles != 0; cycles -= 1) {
      io(cpu);
    }
    return !cpu->stopped;
  }

  if (op == 0x54 || op == 0x44) {
    smx_cpu_move(cpu, op);
    return true;
  }

  /* ALU, stores, and read-modify-write. */
  if ((op & 0x1F) == 0x09 || op == 0x89 || op == 0xA0 || op == 0xA2 || op == 0xC0 || op == 0xE0) {
    bool wide = m8(cpu);
    if (op == 0xA2 || op == 0xE0 || op == 0xA0 || op == 0xC0) {
      wide = !x8(cpu);
    } else if (op == 0x89) {
      wide = !m8(cpu);
    } else {
      wide = !m8(cpu);
    }
    value = fetch_imm(cpu, wide);
  } else if (op == 0x1C || op == 0x0C || op == 0x14 || op == 0x04) {
    addr = address_for(cpu, op);
    value = m8(cpu) ? read8(cpu, addr) : read16_24(cpu, addr);
  } else if (op == 0x06 || op == 0x0E || op == 0x16 || op == 0x1E || op == 0x26 || op == 0x2E ||
             op == 0x36 || op == 0x3E || op == 0x46 || op == 0x4E || op == 0x56 || op == 0x5E ||
             op == 0x66 || op == 0x6E || op == 0x76 || op == 0x7E || op == 0xC6 || op == 0xCE ||
             op == 0xD6 || op == 0xDE || op == 0xE6 || op == 0xEE || op == 0xF6 || op == 0xFE) {
    addr = address_for(cpu, op);
    shift_mem(cpu, op, addr);
    return true;
  } else {
    addr = address_for(cpu, op);
    if (((op & 0xE0) != 0x80 && op != 0x64 && op != 0x74) || op == 0x84 || op == 0x86 ||
        op == 0x94 || op == 0x96 || op == 0x8C || op == 0x8E || op == 0x9C || op == 0x9E) {
      if ((op & 0xE0) == 0x80 && op != 0x89) {
        value = 0;
      } else if (op == 0xA0 || op == 0xA4 || op == 0xAC || op == 0xB4 || op == 0xBC || op == 0xA2 ||
                 op == 0xA6 || op == 0xAE || op == 0xB6 || op == 0xBE || op == 0xC0 || op == 0xC4 ||
                 op == 0xCC || op == 0xE0 || op == 0xE4 || op == 0xEC) {
        value = x8(cpu) ? read8(cpu, addr) : read16_24(cpu, addr);
      } else {
        value = read_ea(cpu, addr);
      }
    }
  }

  if ((op & 0xE0) == 0x80 && op != 0x89) {
    Uint16 stored = m8(cpu) ? (Uint16)(cpu->a & 0xFF) : cpu->a;
    if (op == 0x84 || op == 0x94 || op == 0x8C) {
      stored = x8(cpu) ? (Uint16)(cpu->y & 0xFF) : cpu->y;
      if (x8(cpu)) {
        write8(cpu, addr, (Uint8)stored);
      } else {
        write16_24(cpu, addr, stored);
      }
      return true;
    }
    if (op == 0x86 || op == 0x96 || op == 0x8E) {
      stored = x8(cpu) ? (Uint16)(cpu->x & 0xFF) : cpu->x;
      if (x8(cpu)) {
        write8(cpu, addr, (Uint8)stored);
      } else {
        write16_24(cpu, addr, stored);
      }
      return true;
    }
    if (op == 0x64 || op == 0x74 || op == 0x9C || op == 0x9E) {
      if (m8(cpu)) {
        write8(cpu, addr, 0);
      } else {
        write16_24(cpu, addr, 0);
      }
      return true;
    }
    write_ea(cpu, addr, stored);
    return true;
  }

  if (op == 0xA1 || op == 0xA3 || op == 0xA5 || op == 0xA7 || op == 0xA9 || op == 0xAD ||
      op == 0xAF || op == 0xB1 || op == 0xB2 || op == 0xB3 || op == 0xB5 || op == 0xB7 ||
      op == 0xB9 || op == 0xBD || op == 0xBF) {
    set_a(cpu, value);
    return true;
  }
  if (op == 0xA0 || op == 0xA4 || op == 0xAC || op == 0xB4 || op == 0xBC) {
    set_y(cpu, value);
    return true;
  }
  if (op == 0xA2 || op == 0xA6 || op == 0xAE || op == 0xB6 || op == 0xBE) {
    set_x(cpu, value);
    return true;
  }
  if (op == 0xC0 || op == 0xC4 || op == 0xCC) {
    cmp_value(cpu, idx_y(cpu), value, x8(cpu));
    return true;
  }
  if (op == 0xE0 || op == 0xE4 || op == 0xEC) {
    cmp_value(cpu, idx_x(cpu), value, x8(cpu));
    return true;
  }
  if (op == 0x64 || op == 0x74) {
    if (m8(cpu)) {
      write8(cpu, addr, 0);
    } else {
      write16_24(cpu, addr, 0);
    }
    return true;
  }
  if ((op & 0xE0) == 0x60) {
    adc(cpu, value);
    return true;
  }
  if ((op & 0xE0) == 0xE0 && op != 0xE0 && op != 0xE2 && op != 0xE8 && op != 0xEA && op != 0xEB) {
    sbc(cpu, value);
    return true;
  }
  if (op == 0x24 || op == 0x2C || op == 0x34 || op == 0x3C || op == 0x89) {
    smx_cpu_bit(cpu, op, value);
    return true;
  }
  if (op == 0x04 || op == 0x0C) {
    smx_cpu_test_bits(cpu, addr, value, true);
    return true;
  }
  if (op == 0x14 || op == 0x1C) {
    smx_cpu_test_bits(cpu, addr, value, false);
    return true;
  }
  if ((op & 0xE0) == 0x00 || (op & 0xE0) == 0x20 || (op & 0xE0) == 0x40 || (op & 0xE0) == 0xC0) {
    if ((op & 0xE0) == 0xC0) {
      logic(cpu, op, value);
    } else {
      set_a(cpu, logic(cpu, op, value));
    }
    return true;
  }

  SDL_Log("cpu: unimplemented opcode %u at %u:%u", (unsigned)op, (unsigned)cpu->pb,
          (unsigned)cpu->pc);
  cpu->stopped = true;
  return false;
}

Uint32 smx_cpu_key(const SmxCpu *cpu) {
  Uint32 addr = ((Uint32)cpu->pb << 16) | cpu->pc;

  return SMX_CPU_KEY(addr, cpu->e, (cpu->p & SMX_M) != 0, (cpu->p & SMX_X) != 0);
}

static SmxCpuBlock smx_cpu_find(const SmxCpuCode *code, Uint32 key) {
  Uint32 lo = 0;
  Uint32 hi = code->count;

  while (lo < hi) {
    Uint32 mid = lo + (hi - lo) / 2u;
    if (code->keys[mid] < key) {
      lo = mid + 1u;
    } else {
      hi = mid;
    }
  }
  if (lo < code->count && code->keys[lo] == key) {
    return code->blocks[lo];
  }
  return NULL;
}

bool smx_cpu_run(SmxCpu *cpu) {
  SmxCpuBlock block = NULL;

  if (cpu->code != NULL && !cpu->stopped && !cpu->waiting) {
    Uint32 key = smx_cpu_key(cpu);
    block = smx_cpu_find(cpu->code, key);
    if (block == NULL && cpu->miss != NULL) {
      cpu->miss(cpu->miss_user, key);
    }
  }
  if (block == NULL) {
    cpu->interpreted += 1;
    return smx_cpu_step(cpu);
  }
  block(cpu);
  cpu->translated += 1;
  return !cpu->stopped;
}

bool smx_cpu_next(SmxCpu *cpu) {
  if (cpu->next == NULL || !cpu->next(cpu->next_user)) {
    return false;
  }
  cpu->translated += 1;
  return true;
}

Uint8 smx_cpu_fetch(SmxCpu *cpu) {
  return fetch8(cpu);
}

void smx_cpu_idle(SmxCpu *cpu) {
  io(cpu);
}

Uint8 smx_cpu_read8(SmxCpu *cpu, Uint32 addr) {
  return read8(cpu, addr);
}

void smx_cpu_write8(SmxCpu *cpu, Uint32 addr, Uint8 value) {
  write8(cpu, addr, value);
}

Uint16 smx_cpu_read16(SmxCpu *cpu, Uint32 addr) {
  return read16_24(cpu, addr);
}

void smx_cpu_write16(SmxCpu *cpu, Uint32 addr, Uint16 value) {
  write16_24(cpu, addr, value);
}

Uint16 smx_cpu_read_bank16(SmxCpu *cpu, Uint8 bank, Uint16 addr) {
  return read16p(cpu, bank, addr);
}

Uint16 smx_cpu_read_m(SmxCpu *cpu, Uint32 addr) {
  return read_ea(cpu, addr);
}

void smx_cpu_write_m(SmxCpu *cpu, Uint32 addr, Uint16 value) {
  write_ea(cpu, addr, value);
}

Uint16 smx_cpu_read_x(SmxCpu *cpu, Uint32 addr) {
  return x8(cpu) ? read8(cpu, addr) : read16_24(cpu, addr);
}

void smx_cpu_push8(SmxCpu *cpu, Uint8 value) {
  push8(cpu, value);
}

void smx_cpu_push16(SmxCpu *cpu, Uint16 value) {
  push16(cpu, value);
}

Uint8 smx_cpu_pull8(SmxCpu *cpu) {
  return pull8(cpu);
}

Uint16 smx_cpu_pull16(SmxCpu *cpu) {
  return pull16(cpu);
}

void smx_cpu_set_a(SmxCpu *cpu, Uint16 value) {
  set_a(cpu, value);
}

void smx_cpu_set_x(SmxCpu *cpu, Uint16 value) {
  set_x(cpu, value);
}

void smx_cpu_set_y(SmxCpu *cpu, Uint16 value) {
  set_y(cpu, value);
}

void smx_cpu_set_p(SmxCpu *cpu, Uint8 p) {
  apply_p(cpu, p);
}

void smx_cpu_adc(SmxCpu *cpu, Uint16 value) {
  adc(cpu, value);
}

void smx_cpu_sbc(SmxCpu *cpu, Uint16 value) {
  sbc(cpu, value);
}

void smx_cpu_compare(SmxCpu *cpu, Uint16 left, Uint16 right, bool eight) {
  cmp_value(cpu, left, right, eight);
}

Uint16 smx_cpu_logic(SmxCpu *cpu, Uint8 op, Uint16 value) {
  return logic(cpu, op, value);
}

void smx_cpu_shift(SmxCpu *cpu, Uint8 op, Uint32 addr) {
  shift_mem(cpu, op, addr);
}

bool smx_cpu_implied(SmxCpu *cpu, Uint8 op) {
  Uint8 cycles = 0;

  if (!run_implied(cpu, op)) {
    return false;
  }
  for (cycles = implied_io(op); cycles != 0; cycles -= 1) {
    io(cpu);
  }
  return true;
}

void smx_cpu_interrupt(SmxCpu *cpu, Uint16 native_vector, Uint16 emu_vector) {
  interrupt(cpu, native_vector, emu_vector, true);
}

void smx_cpu_branch(SmxCpu *cpu, bool take) {
  branch8(cpu, take);
}

void smx_cpu_move(SmxCpu *cpu, Uint8 op) {
  Uint8 dest_bank = fetch8(cpu);
  Uint8 src_bank = fetch8(cpu);
  Uint8 value = 0;

  io(cpu);
  io(cpu);
  value = read8(cpu, ((Uint32)src_bank << 16) | cpu->x);
  write8(cpu, ((Uint32)dest_bank << 16) | cpu->y, value);
  if (x8(cpu)) {
    if (op == 0x54) {
      cpu->x = (Uint16)((cpu->x + 1) & 0x00FF);
      cpu->y = (Uint16)((cpu->y + 1) & 0x00FF);
    } else {
      cpu->x = (Uint16)((cpu->x - 1) & 0x00FF);
      cpu->y = (Uint16)((cpu->y - 1) & 0x00FF);
    }
  } else if (op == 0x54) {
    cpu->x = (Uint16)(cpu->x + 1);
    cpu->y = (Uint16)(cpu->y + 1);
  } else {
    cpu->x = (Uint16)(cpu->x - 1);
    cpu->y = (Uint16)(cpu->y - 1);
  }
  cpu->a = (Uint16)(cpu->a - 1);
  cpu->db = dest_bank;
  if (cpu->a != 0xFFFF) {
    cpu->pc = (Uint16)(cpu->pc - 3);
  }
}
