#include "smx/apu.h"

static void smx_apu_publish(SmxApu *apu) {
  Uint8 index = 0;

  if (apu->bus == NULL) {
    return;
  }
  for (; index < 4; index += 1) {
    apu->bus->mmio[0x2140u + index] = apu->out[index];
  }
}

static void smx_apu_cpu_write(void *user, Uint8 index, Uint8 value) {
  SmxApu *apu = user;

  apu->in[index] = value;
  smx_apu_publish(apu);
}

void smx_apu_init(SmxApu *apu, SmxBus *bus) {
  SDL_memset(apu, 0, sizeof(*apu));
  apu->bus = bus;
  apu->out[0] = 0xAA;
  apu->out[1] = 0xBB;
  /* Bit 7 maps the IPL ROM over $FFC0-$FFFF until the program clears it. */
  apu->control = 0x80;
  apu->pc = 0xFFC0;
  apu->running = true;
  smx_dsp_init(&apu->dsp, apu->ram);
  /* The reset sequence spends two cycles before the first instruction. */
  smx_dsp_run(&apu->dsp, 2);
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
  return apu->out[index];
}

Uint8 smx_apu_ram(const SmxApu *apu, Uint16 addr) {
  return apu->ram[addr];
}

bool smx_apu_core_running(const SmxApu *apu) {
  return apu->running;
}

static Uint16 smx_spc_page(const SmxApu *apu) {
  return (apu->psw & 0x20) != 0 ? 0x0100 : 0x0000;
}

static Uint8 smx_spc_read(SmxApu *apu, Uint16 addr);

/*
 * Run the DSP to the last cycle of the current instruction, where its
 * $F3 access lands, before the access happens.
 */
static void smx_apu_dsp_sync(SmxApu *apu) {
  Uint8 target = apu->op_cycles == 0 ? 0 : (Uint8)(apu->op_cycles - 1u);

  if (apu->dsp_done < target) {
    smx_dsp_run(&apu->dsp, (Uint32)(target - apu->dsp_done));
    apu->dsp_done = target;
  }
}

static Uint8 smx_spc_fetch(SmxApu *apu) {
  Uint8 value = smx_spc_read(apu, apu->pc);
  apu->pc = (Uint16)(apu->pc + 1);
  return value;
}

static Uint8 smx_spc_read(SmxApu *apu, Uint16 addr) {
  /* TEST, CONTROL, and the timer targets are write-only. */
  if (addr == 0x00F0 || addr == 0x00F1 || (addr >= 0x00FA && addr <= 0x00FC)) {
    return 0;
  }
  if (addr >= 0x00F4 && addr <= 0x00F7) {
    return apu->in[addr - 0x00F4];
  }
  if (addr == 0x00F2) {
    return apu->dsp_addr;
  }
  if (addr == 0x00F3) {
    smx_apu_dsp_sync(apu);
    return smx_dsp_read(&apu->dsp, apu->dsp_addr);
  }
  if (addr >= 0x00FD && addr <= 0x00FF) {
    Uint8 value = apu->timer_out[addr - 0x00FD];
    apu->timer_out[addr - 0x00FD] = 0;
    return value;
  }
  if (addr >= 0xFFC0 && (apu->control & 0x80) != 0) {
    static const Uint8 ipl[64] = {0xCD, 0xEF, 0xBD, 0xE8, 0x00, 0xC6, 0x1D, 0xD0, 0xFC, 0x8F, 0xAA,
                                  0xF4, 0x8F, 0xBB, 0xF5, 0x78, 0xCC, 0xF4, 0xD0, 0xFB, 0x2F, 0x19,
                                  0xEB, 0xF4, 0xD0, 0xFC, 0x7E, 0xF4, 0xD0, 0x0B, 0xE4, 0xF5, 0xCB,
                                  0xF4, 0xD7, 0x00, 0xFC, 0xD0, 0xF3, 0xAB, 0x01, 0x10, 0xEF, 0x7E,
                                  0xF4, 0x10, 0xEB, 0xBA, 0xF6, 0xDA, 0x00, 0xBA, 0xF4, 0xC4, 0xF4,
                                  0xDD, 0x5D, 0xD0, 0xDB, 0x1F, 0x00, 0x00, 0xC0, 0xFF};
    return ipl[addr - 0xFFC0];
  }
  return apu->ram[addr];
}

static void smx_spc_write(SmxApu *apu, Uint16 addr, Uint8 value) {
  /* Register writes also land in the RAM underneath. */
  if (addr >= 0x00F0 && addr <= 0x00FF) {
    apu->ram[addr] = value;
  }
  if (addr >= 0x00F4 && addr <= 0x00F7) {
    apu->out[addr - 0x00F4] = value;
    smx_apu_publish(apu);
    return;
  }
  if (addr == 0x00F1) {
    Uint8 index = 0;
    /* Turning a timer on clears its counter and output, not its prescaler. */
    for (; index < 3; index += 1) {
      Uint8 bit = (Uint8)(1u << index);
      if ((apu->control & bit) == 0 && (value & bit) != 0) {
        apu->timer_stage[index] = 0;
        apu->timer_out[index] = 0;
      }
    }
    apu->control = value;
    if ((value & 0x10) != 0) {
      apu->in[0] = 0;
      apu->in[1] = 0;
    }
    if ((value & 0x20) != 0) {
      apu->in[2] = 0;
      apu->in[3] = 0;
    }
    return;
  }
  if (addr == 0x00F2) {
    apu->dsp_addr = value;
    return;
  }
  if (addr == 0x00F3) {
    smx_apu_dsp_sync(apu);
    if (apu->dsp_addr < 0x80) {
      smx_dsp_write(&apu->dsp, apu->dsp_addr, value);
    }
    return;
  }
  if (addr >= 0x00FA && addr <= 0x00FC) {
    apu->timer_target[addr - 0x00FA] = value;
    return;
  }
  apu->ram[addr] = value;
}

static void smx_spc_nz(SmxApu *apu, Uint8 value) {
  apu->psw =
      (Uint8)((apu->psw & (Uint8) ~(0x80 | 0x02)) | (value & 0x80) | (value == 0 ? 0x02 : 0));
}

static void smx_spc_push(SmxApu *apu, Uint8 value) {
  apu->ram[0x0100u + apu->sp] = value;
  apu->sp = (Uint8)(apu->sp - 1);
}

static Uint8 smx_spc_pull(SmxApu *apu) {
  apu->sp = (Uint8)(apu->sp + 1);
  return apu->ram[0x0100u + apu->sp];
}

static Uint16 smx_spc_ea(SmxApu *apu, Uint8 kind) {
  Uint8 direct = 0;
  Uint16 addr = 0;

  if (kind == 0) {
    direct = smx_spc_fetch(apu);
    return (Uint16)(smx_spc_page(apu) | direct);
  }
  if (kind == 1) {
    direct = (Uint8)(smx_spc_fetch(apu) + apu->x);
    return (Uint16)(smx_spc_page(apu) | direct);
  }
  if (kind == 2) {
    addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    return addr;
  }
  if (kind == 3) {
    addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    return (Uint16)(addr + apu->x);
  }
  if (kind == 4) {
    addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    return (Uint16)(addr + apu->y);
  }
  if (kind == 5) {
    return apu->x;
  }
  if (kind == 6) {
    direct = (Uint8)(smx_spc_fetch(apu) + apu->x);
    addr = (Uint16)(smx_spc_page(apu) | direct);
    return (Uint16)(smx_spc_read(apu, addr) | ((Uint16)smx_spc_read(apu, (Uint16)(addr + 1)) << 8));
  }
  direct = smx_spc_fetch(apu);
  addr = (Uint16)(smx_spc_page(apu) | direct);
  addr = (Uint16)(smx_spc_read(apu, addr) | ((Uint16)smx_spc_read(apu, (Uint16)(addr + 1)) << 8));
  return (Uint16)(addr + apu->y);
}

static Uint8 smx_spc_logic(SmxApu *apu, Uint8 op, Uint8 left, Uint8 right) {
  Uint16 sum = 0;
  Uint8 result = 0;
  Uint8 carry = (Uint8)(apu->psw & 0x01);

  if (op < 0x20) {
    result = (Uint8)(left | right);
  } else if (op < 0x40) {
    result = (Uint8)(left & right);
  } else if (op < 0x60) {
    result = (Uint8)(left ^ right);
  } else if (op < 0x80) {
    result = (Uint8)(left - right);
    apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | (left >= right ? 0x01 : 0));
    smx_spc_nz(apu, result);
    return left;
  } else if (op < 0xA0) {
    sum = (Uint16)left + right + carry;
    result = (Uint8)sum;
    apu->psw = (Uint8)(apu->psw & (Uint8) ~(0x40 | 0x08 | 0x01));
    if (((left & 0x0F) + (right & 0x0F) + carry) > 0x0F) {
      apu->psw = (Uint8)(apu->psw | 0x08);
    }
    if (((~(left ^ right)) & (left ^ result) & 0x80) != 0) {
      apu->psw = (Uint8)(apu->psw | 0x40);
    }
    if (sum > 0xFF) {
      apu->psw = (Uint8)(apu->psw | 0x01);
    }
  } else {
    Uint8 borrow = (Uint8)(carry ^ 0x01);
    sum = (Uint16)left - right - borrow;
    result = (Uint8)sum;
    apu->psw = (Uint8)(apu->psw & (Uint8) ~(0x40 | 0x08 | 0x01));
    if ((left & 0x0F) >= (Uint8)((right & 0x0F) + borrow)) {
      apu->psw = (Uint8)(apu->psw | 0x08);
    }
    if (((left ^ right) & (left ^ result) & 0x80) != 0) {
      apu->psw = (Uint8)(apu->psw | 0x40);
    }
    if (left >= (Uint16)right + borrow) {
      apu->psw = (Uint8)(apu->psw | 0x01);
    }
  }
  smx_spc_nz(apu, result);
  return result;
}

static const Uint8 smx_spc_cycles[256] = {
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 4, 6, 8, 2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 6, 5, 2, 2, 4,  6,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 4, 5, 4, 2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 6, 5, 2, 2, 3,  8,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 4, 6, 6, 2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 4, 5, 2, 2, 4,  3,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 4, 5, 5, 2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 3,  6,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 2, 4, 5, 2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 12, 5,
    3, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 2, 4, 4, 2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 3,  4,
    3, 8, 4, 5, 4, 5, 4, 7, 2, 5, 6, 4, 5, 2, 4, 9, 2, 8, 4, 5, 5, 6, 6, 7, 4, 5, 5, 5, 2, 2, 6,  3,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 4, 5, 3, 4, 3, 4, 3, 2, 8, 4, 5, 4, 5, 5, 6, 3, 4, 5, 4, 2, 2, 4,  3,
};

/*
 * A taken branch spends two more cycles than the table entry. The timer
 * prescalers run from reset whether or not a timer is enabled.
 */
static void smx_spc_spend(SmxApu *apu, Uint8 op, bool take) {
  static const Uint16 rate[3] = {128, 128, 16};
  Uint8 index = 0;

  apu->cycles = (Uint8)(smx_spc_cycles[op] + (take ? 2u : 0u));
  smx_dsp_run(&apu->dsp, (Uint32)(apu->cycles - apu->dsp_done));
  apu->dsp_done = 0;
  apu->op_cycles = 0;
  for (; index < 3; index += 1) {
    Uint16 limit = 0;
    apu->timer_div[index] = (Uint16)(apu->timer_div[index] + apu->cycles);
    if (apu->timer_div[index] < rate[index]) {
      continue;
    }
    apu->timer_div[index] = (Uint16)(apu->timer_div[index] - rate[index]);
    if ((apu->control & (Uint8)(1u << index)) == 0) {
      continue;
    }
    apu->timer_stage[index] = (Uint16)(apu->timer_stage[index] + 1);
    limit = apu->timer_target[index] == 0 ? 256 : apu->timer_target[index];
    if (apu->timer_stage[index] >= limit) {
      apu->timer_stage[index] = 0;
      apu->timer_out[index] = (Uint8)((apu->timer_out[index] + 1) & 0x0F);
    }
  }
}

static bool smx_spc_move_a(SmxApu *apu, Uint8 op) {
  Uint8 kind = 0;
  Uint8 value = 0;

  if (op == 0xE8) {
    value = smx_spc_fetch(apu);
    apu->a = value;
    smx_spc_nz(apu, value);
    return true;
  }
  if (op >= 0xE4 && op <= 0xE7) {
    kind = (Uint8)(op - 0xE4);
    if (kind == 1) {
      kind = 2;
    } else if (kind == 2) {
      kind = 5;
    } else if (kind == 3) {
      kind = 6;
    }
    value = smx_spc_read(apu, smx_spc_ea(apu, kind));
    apu->a = value;
    smx_spc_nz(apu, value);
    return true;
  }
  if (op >= 0xF4 && op <= 0xF7) {
    kind = (Uint8)(op == 0xF4 ? 1 : op == 0xF5 ? 3 : op == 0xF6 ? 4 : 7);
    value = smx_spc_read(apu, smx_spc_ea(apu, kind));
    apu->a = value;
    smx_spc_nz(apu, value);
    return true;
  }
  if ((op >= 0xC4 && op <= 0xC7) || (op >= 0xD4 && op <= 0xD7)) {
    if (op <= 0xC7) {
      kind = (Uint8)(op - 0xC4);
      if (kind == 1) {
        kind = 2;
      } else if (kind == 2) {
        kind = 5;
      } else if (kind == 3) {
        kind = 6;
      }
    } else {
      kind = (Uint8)(op == 0xD4 ? 1 : op == 0xD5 ? 3 : op == 0xD6 ? 4 : 7);
    }
    smx_spc_write(apu, smx_spc_ea(apu, kind), apu->a);
    return true;
  }
  return false;
}

Uint8 smx_apu_next_cycles(const SmxApu *apu) {
  Uint8 op = apu->ram[apu->pc];

  if (apu->pc >= 0xFFC0 && (apu->control & 0x80) != 0) {
    op = smx_spc_read((SmxApu *)apu, apu->pc);
  }
  return smx_spc_cycles[op];
}

bool smx_apu_step(SmxApu *apu) {
  Uint8 op = 0;
  Uint8 left = 0;
  Uint8 right = 0;
  Uint8 rel = 0;
  Uint16 addr = 0;
  Uint16 target = 0;
  bool take = false;

  if (!apu->running) {
    return false;
  }
  op = smx_spc_fetch(apu);
  apu->op_cycles = smx_spc_cycles[op];
  apu->dsp_done = 0;
  if ((op & 0x1F) == 0x18 && op < 0xC0) {
    right = smx_spc_fetch(apu);
    addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    left = smx_spc_read(apu, addr);
    left = smx_spc_logic(apu, op, left, right);
    if ((op & 0xE0) != 0x60) {
      smx_spc_write(apu, addr, left);
    }
    smx_spc_spend(apu, op, take);
    return true;
  }
  if (smx_spc_move_a(apu, op)) {
    smx_spc_spend(apu, op, take);
    return true;
  }
  if ((op & 0x0F) >= 4 && (op & 0x0F) <= 9 && (op < 0xC0) &&
      ((op & 0x10) == 0 || (op & 0x0F) <= 7) && op != 0x8F && op != 0x9F) {
    Uint8 row = (Uint8)(op & 0xE0);
    Uint8 col = (Uint8)(op & 0x0F);
    Uint8 kind = 0;
    if (col == 8) {
      right = smx_spc_fetch(apu);
      apu->a = smx_spc_logic(apu, row, apu->a, right);
      smx_spc_spend(apu, op, take);
      return true;
    }
    if (col == 9) {
      Uint8 src = smx_spc_fetch(apu);
      Uint8 dest = smx_spc_fetch(apu);
      addr = (Uint16)(smx_spc_page(apu) | dest);
      left = smx_spc_read(apu, addr);
      right = smx_spc_read(apu, (Uint16)(smx_spc_page(apu) | src));
      if (row != 0x60) {
        smx_spc_write(apu, addr, smx_spc_logic(apu, row, left, right));
      } else {
        smx_spc_logic(apu, row, left, right);
      }
      smx_spc_spend(apu, op, take);
      return true;
    }
    kind = (Uint8)(col - 4);
    if ((op & 0x10) != 0) {
      kind = (Uint8)(kind == 0 ? 1 : kind == 1 ? 3 : kind == 2 ? 4 : 7);
    } else if (kind == 1) {
      kind = 2;
    } else if (kind == 2) {
      kind = 5;
    } else if (kind == 3) {
      kind = 6;
    }
    addr = smx_spc_ea(apu, kind);
    right = smx_spc_read(apu, addr);
    if (row == 0x60) {
      smx_spc_logic(apu, row, apu->a, right);
    } else {
      apu->a = smx_spc_logic(apu, row, apu->a, right);
    }
    smx_spc_spend(apu, op, take);
    return true;
  }

  switch (op) {
  case 0x00:
    break;
  case 0x20:
    apu->psw = (Uint8)(apu->psw & (Uint8)~0x20);
    break;
  case 0x40:
    apu->psw = (Uint8)(apu->psw | 0x20);
    break;
  case 0x60:
    apu->psw = (Uint8)(apu->psw & (Uint8)~0x01);
    break;
  case 0x80:
    apu->psw = (Uint8)(apu->psw | 0x01);
    break;
  case 0xE0:
    apu->psw = (Uint8)(apu->psw & (Uint8) ~(0x40 | 0x08));
    break;
  case 0xED:
    apu->psw = (Uint8)(apu->psw ^ 0x01);
    break;
  case 0xA0:
    apu->psw = (Uint8)(apu->psw | 0x04);
    break;
  case 0xC0:
    apu->psw = (Uint8)(apu->psw & (Uint8)~0x04);
    break;
  case 0x2F:
  case 0x10:
  case 0x30:
  case 0x50:
  case 0x70:
  case 0x90:
  case 0xB0:
  case 0xD0:
  case 0xF0:
    rel = smx_spc_fetch(apu);
    take = op == 0x2F || (op == 0x10 && (apu->psw & 0x80) == 0) ||
           (op == 0x30 && (apu->psw & 0x80) != 0) || (op == 0x50 && (apu->psw & 0x40) == 0) ||
           (op == 0x70 && (apu->psw & 0x40) != 0) || (op == 0x90 && (apu->psw & 0x01) == 0) ||
           (op == 0xB0 && (apu->psw & 0x01) != 0) || (op == 0xD0 && (apu->psw & 0x02) == 0) ||
           (op == 0xF0 && (apu->psw & 0x02) != 0);
    if (take) {
      apu->pc = (Uint16)(apu->pc + (Sint8)rel);
    }
    break;
  case 0x3F:
    target = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    smx_spc_push(apu, (Uint8)(apu->pc >> 8));
    smx_spc_push(apu, (Uint8)apu->pc);
    apu->pc = target;
    break;
  case 0x6F:
    left = smx_spc_pull(apu);
    apu->pc = (Uint16)(left | ((Uint16)smx_spc_pull(apu) << 8));
    break;
  case 0x7F:
    apu->psw = smx_spc_pull(apu);
    left = smx_spc_pull(apu);
    apu->pc = (Uint16)(left | ((Uint16)smx_spc_pull(apu) << 8));
    break;
  case 0x5F:
    apu->pc = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    break;
  case 0x1F:
    addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    addr = (Uint16)(addr + apu->x);
    apu->pc =
        (Uint16)(smx_spc_read(apu, addr) | ((Uint16)smx_spc_read(apu, (Uint16)(addr + 1)) << 8));
    break;
  case 0x8F:
    right = smx_spc_fetch(apu);
    left = smx_spc_fetch(apu);
    smx_spc_write(apu, (Uint16)(smx_spc_page(apu) | left), right);
    break;
  case 0xFA:
    right = smx_spc_fetch(apu);
    left = smx_spc_fetch(apu);
    smx_spc_write(apu, (Uint16)(smx_spc_page(apu) | left),
                  smx_spc_read(apu, (Uint16)(smx_spc_page(apu) | right)));
    break;
  case 0xCD:
    apu->x = smx_spc_fetch(apu);
    smx_spc_nz(apu, apu->x);
    break;
  case 0x8D:
    apu->y = smx_spc_fetch(apu);
    smx_spc_nz(apu, apu->y);
    break;
  case 0x5D:
    apu->x = apu->a;
    smx_spc_nz(apu, apu->x);
    break;
  case 0xFD:
    apu->y = apu->a;
    smx_spc_nz(apu, apu->y);
    break;
  case 0x7D:
    apu->a = apu->x;
    smx_spc_nz(apu, apu->a);
    break;
  case 0xDD:
    apu->a = apu->y;
    smx_spc_nz(apu, apu->a);
    break;
  case 0xBD:
    apu->sp = apu->x;
    break;
  case 0x9D:
    apu->x = apu->sp;
    smx_spc_nz(apu, apu->x);
    break;
  case 0xD8:
  case 0xD9:
  case 0xC9:
  case 0xCB:
  case 0xDB:
  case 0xCC:
    if (op == 0xC9 || op == 0xCC) {
      addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    } else if (op == 0xD9) {
      addr = (Uint16)(smx_spc_page(apu) | (Uint8)(smx_spc_fetch(apu) + apu->y));
    } else if (op == 0xDB) {
      addr = (Uint16)(smx_spc_page(apu) | (Uint8)(smx_spc_fetch(apu) + apu->x));
    } else {
      addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    }
    smx_spc_write(apu, addr, (op == 0xCB || op == 0xDB || op == 0xCC) ? apu->y : apu->x);
    break;
  case 0xF8:
  case 0xF9:
  case 0xE9:
  case 0xEB:
  case 0xFB:
  case 0xEC:
    if (op == 0xE9 || op == 0xEC) {
      addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    } else if (op == 0xF9) {
      addr = (Uint16)(smx_spc_page(apu) | (Uint8)(smx_spc_fetch(apu) + apu->y));
    } else if (op == 0xFB) {
      addr = (Uint16)(smx_spc_page(apu) | (Uint8)(smx_spc_fetch(apu) + apu->x));
    } else {
      addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    }
    right = smx_spc_read(apu, addr);
    if (op == 0xEB || op == 0xFB || op == 0xEC) {
      apu->y = right;
      smx_spc_nz(apu, apu->y);
    } else {
      apu->x = right;
      smx_spc_nz(apu, apu->x);
    }
    break;
  case 0xAF:
    smx_spc_write(apu, apu->x, apu->a);
    apu->x = (Uint8)(apu->x + 1);
    break;
  case 0xC6:
    smx_spc_write(apu, apu->x, apu->a);
    break;
  case 0xBF:
    apu->a = smx_spc_read(apu, apu->x);
    apu->x = (Uint8)(apu->x + 1);
    smx_spc_nz(apu, apu->a);
    break;
  case 0xE6:
    apu->a = smx_spc_read(apu, apu->x);
    smx_spc_nz(apu, apu->a);
    break;
  case 0xC8:
  case 0xAD:
  case 0x3E:
  case 0x1E:
  case 0x7E:
  case 0x5E:
    if (op == 0xC8 || op == 0xAD) {
      right = smx_spc_fetch(apu);
    } else if (op == 0x1E || op == 0x5E) {
      right = smx_spc_read(apu, (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8)));
    } else {
      right = smx_spc_read(apu, (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu)));
    }
    left = (op == 0xAD || op == 0x7E || op == 0x5E) ? apu->y : apu->x;
    smx_spc_logic(apu, 0x60, left, right);
    break;
  case 0xAB:
  case 0xBB:
  case 0xAC:
  case 0x8B:
  case 0x9B:
  case 0x8C:
  case 0xBC:
  case 0x3D:
  case 0xFC:
  case 0x9C:
  case 0x1D:
  case 0xDC:
    if (op == 0xBC || op == 0x3D || op == 0xFC || op == 0x9C || op == 0x1D || op == 0xDC) {
      if (op == 0xBC || op == 0x3D) {
        left = (Uint8)(op == 0xBC ? apu->a + 1 : apu->x + 1);
        if (op == 0xBC) {
          apu->a = left;
        } else {
          apu->x = left;
        }
      } else if (op == 0xFC) {
        apu->y = (Uint8)(apu->y + 1);
        left = apu->y;
      } else if (op == 0x9C) {
        apu->a = (Uint8)(apu->a - 1);
        left = apu->a;
      } else if (op == 0x1D) {
        apu->x = (Uint8)(apu->x - 1);
        left = apu->x;
      } else {
        apu->y = (Uint8)(apu->y - 1);
        left = apu->y;
      }
      smx_spc_nz(apu, left);
      break;
    }
    if (op == 0xAB || op == 0x8B) {
      addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    } else if (op == 0xBB || op == 0x9B) {
      addr = (Uint16)(smx_spc_page(apu) | (Uint8)(smx_spc_fetch(apu) + apu->x));
    } else {
      addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    }
    left = smx_spc_read(apu, addr);
    left = (Uint8)(left + ((op == 0x8B || op == 0x9B || op == 0x8C) ? 0xFF : 1));
    smx_spc_write(apu, addr, left);
    smx_spc_nz(apu, left);
    break;
  case 0x2D:
    smx_spc_push(apu, apu->a);
    break;
  case 0x4D:
    smx_spc_push(apu, apu->x);
    break;
  case 0x6D:
    smx_spc_push(apu, apu->y);
    break;
  case 0x0D:
    smx_spc_push(apu, apu->psw);
    break;
  case 0xAE:
    apu->a = smx_spc_pull(apu);
    smx_spc_nz(apu, apu->a);
    break;
  case 0xCE:
    apu->x = smx_spc_pull(apu);
    smx_spc_nz(apu, apu->x);
    break;
  case 0xEE:
    apu->y = smx_spc_pull(apu);
    smx_spc_nz(apu, apu->y);
    break;
  case 0x8E:
    apu->psw = smx_spc_pull(apu);
    break;
  case 0xBA:
    addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    apu->a = smx_spc_read(apu, addr);
    apu->y = smx_spc_read(apu, (Uint16)(addr + 1));
    smx_spc_nz(apu, apu->y);
    break;
  case 0xDA:
    addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    smx_spc_write(apu, addr, apu->a);
    smx_spc_write(apu, (Uint16)(addr + 1), apu->y);
    break;
  case 0xCF:
    addr = (Uint16)apu->a * apu->y;
    apu->a = (Uint8)addr;
    apu->y = (Uint8)(addr >> 8);
    smx_spc_nz(apu, apu->y);
    break;
  case 0x2E:
  case 0xDE:
  case 0x6E:
  case 0xFE:
    if (op == 0xFE) {
      left = apu->y;
    } else if (op == 0xDE) {
      addr = (Uint16)(smx_spc_page(apu) | (Uint8)(smx_spc_fetch(apu) + apu->x));
      left = smx_spc_read(apu, addr);
    } else {
      addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
      left = smx_spc_read(apu, addr);
    }
    rel = smx_spc_fetch(apu);
    if (op == 0x6E || op == 0xFE) {
      left = (Uint8)(left - 1);
      if (op == 0xFE) {
        apu->y = left;
      } else {
        smx_spc_write(apu, addr, left);
      }
      take = left != 0;
    } else {
      take = left != apu->a;
    }
    if (take) {
      apu->pc = (Uint16)(apu->pc + (Sint8)rel);
    }
    break;
  case 0x1C:
  case 0x5C:
  case 0x3C:
  case 0x7C:
  case 0x0B:
  case 0x1B:
  case 0x0C:
  case 0x4B:
  case 0x5B:
  case 0x4C:
  case 0x2B:
  case 0x3B:
  case 0x2C:
  case 0x6B:
  case 0x7B:
  case 0x6C: {
    Uint8 carry = (Uint8)(apu->psw & 0x01);
    Uint8 kind = (Uint8)(op & 0xF0);
    bool memory = op != 0x1C && op != 0x5C && op != 0x3C && op != 0x7C;
    if (op == 0x0B || op == 0x4B || op == 0x2B || op == 0x6B) {
      addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    } else if (op == 0x1B || op == 0x5B || op == 0x3B || op == 0x7B) {
      addr = (Uint16)(smx_spc_page(apu) | (Uint8)(smx_spc_fetch(apu) + apu->x));
    } else if (memory) {
      addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    }
    left = memory ? smx_spc_read(apu, addr) : apu->a;
    if (kind == 0x10 || kind == 0x00) {
      apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | ((left & 0x80) != 0 ? 0x01 : 0));
      left = (Uint8)(left << 1);
    } else if (kind == 0x50 || kind == 0x40) {
      apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | (left & 0x01));
      left = (Uint8)(left >> 1);
    } else if (kind == 0x30 || kind == 0x20) {
      right = (Uint8)((left << 1) | carry);
      apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | ((left & 0x80) != 0 ? 0x01 : 0));
      left = right;
    } else {
      right = (Uint8)((left >> 1) | (carry << 7));
      apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | (left & 0x01));
      left = right;
    }
    smx_spc_nz(apu, left);
    if (memory) {
      smx_spc_write(apu, addr, left);
    } else {
      apu->a = left;
    }
    break;
  }
  case 0x1A:
  case 0x3A:
  case 0x5A:
  case 0x7A:
  case 0x9A: {
    Uint16 ya = (Uint16)(apu->a | ((Uint16)apu->y << 8));
    addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    target =
        (Uint16)(smx_spc_read(apu, addr) | ((Uint16)smx_spc_read(apu, (Uint16)(addr + 1)) << 8));
    if (op == 0x3A) {
      target = (Uint16)(target + 1);
    } else if (op == 0x1A) {
      target = (Uint16)(target - 1);
    } else if (op == 0x7A) {
      Uint32 sum = (Uint32)ya + target;
      apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | (sum > 0xFFFF ? 0x01 : 0));
      target = (Uint16)sum;
      ya = target;
    } else if (op == 0x9A) {
      apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | (ya >= target ? 0x01 : 0));
      ya = (Uint16)(ya - target);
      target = ya;
    } else {
      apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | (ya >= target ? 0x01 : 0));
      smx_spc_nz(apu, (Uint8)((ya - target) >> 8));
      break;
    }
    if (op == 0x1A || op == 0x3A) {
      smx_spc_write(apu, addr, (Uint8)target);
      smx_spc_write(apu, (Uint16)(addr + 1), (Uint8)(target >> 8));
      smx_spc_nz(apu, (Uint8)(target >> 8));
    } else {
      apu->a = (Uint8)ya;
      apu->y = (Uint8)(ya >> 8);
      smx_spc_nz(apu, apu->y);
    }
    break;
  }
  case 0x0E:
  case 0x4E:
    addr = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
    left = smx_spc_read(apu, addr);
    smx_spc_nz(apu, (Uint8)(apu->a - left));
    smx_spc_write(apu, addr, op == 0x0E ? (Uint8)(left | apu->a) : (Uint8)(left & (Uint8)~apu->a));
    break;
  case 0x4F:
    rel = smx_spc_fetch(apu);
    smx_spc_push(apu, (Uint8)(apu->pc >> 8));
    smx_spc_push(apu, (Uint8)apu->pc);
    apu->pc = (Uint16)(0xFF00u + rel);
    break;
  case 0x9E: {
    Uint16 ya = (Uint16)(apu->a | ((Uint16)apu->y << 8));
    apu->psw = (Uint8)(apu->psw & (Uint8)~0x40);
    if (apu->x == 0 || (ya / apu->x) > 0xFF) {
      apu->psw = (Uint8)(apu->psw | 0x40);
    } else {
      apu->a = (Uint8)(ya / apu->x);
      apu->y = (Uint8)(ya % apu->x);
    }
    smx_spc_nz(apu, apu->a);
    break;
  }
  case 0xDF:
  case 0xBE: {
    Uint16 adjust = apu->a;
    if (op == 0xDF) {
      if ((apu->psw & 0x08) != 0 || (adjust & 0x0F) > 9) {
        adjust = (Uint16)(adjust + 6);
      }
      if ((apu->psw & 0x01) != 0 || adjust > 0x9F) {
        adjust = (Uint16)(adjust + 0x60);
        apu->psw = (Uint8)(apu->psw | 0x01);
      }
    } else {
      if ((apu->psw & 0x08) == 0 || (adjust & 0x0F) > 9) {
        adjust = (Uint16)(adjust - 6);
      }
      if ((apu->psw & 0x01) == 0 || adjust > 0x9F) {
        adjust = (Uint16)(adjust - 0x60);
        apu->psw = (Uint8)(apu->psw & (Uint8)~0x01);
      }
    }
    apu->a = (Uint8)adjust;
    smx_spc_nz(apu, apu->a);
    break;
  }
  case 0xEF:
  case 0xFF:
    apu->running = false;
    return false;
  case 0x9F:
    apu->a = (Uint8)((apu->a << 4) | (apu->a >> 4));
    smx_spc_nz(apu, apu->a);
    break;
  default:
    if ((op & 0x0F) == 0x0A) {
      Uint16 raw = (Uint16)(smx_spc_fetch(apu) | ((Uint16)smx_spc_fetch(apu) << 8));
      Uint8 bit = (Uint8)(1u << (raw >> 13));
      Uint8 mem = smx_spc_read(apu, (Uint16)(raw & 0x1FFF));
      bool set = (mem & bit) != 0;
      if (op == 0x2A || op == 0x6A) {
        set = !set;
      }
      if (op == 0x0A) {
        if (set) {
          apu->psw = (Uint8)(apu->psw | 0x01);
        }
      } else if (op == 0x2A) {
        if (set) {
          apu->psw = (Uint8)(apu->psw | 0x01);
        }
      } else if (op == 0x4A || op == 0x6A) {
        if (!set) {
          apu->psw = (Uint8)(apu->psw & (Uint8)~0x01);
        }
      } else if (op == 0x8A) {
        if (set) {
          apu->psw = (Uint8)(apu->psw ^ 0x01);
        }
      } else if (op == 0xAA) {
        apu->psw = (Uint8)((apu->psw & (Uint8)~0x01) | (set ? 0x01 : 0));
      } else if (op == 0xCA) {
        if ((apu->psw & 0x01) != 0) {
          mem = (Uint8)(mem | bit);
        } else {
          mem = (Uint8)(mem & (Uint8)~bit);
        }
        smx_spc_write(apu, (Uint16)(raw & 0x1FFF), mem);
      } else {
        smx_spc_write(apu, (Uint16)(raw & 0x1FFF), (Uint8)(mem ^ bit));
      }
      break;
    }
    if ((op & 0x0F) == 0x01) {
      Uint16 slot = (Uint16)(0xFFDE - (Uint16)((op >> 4) * 2));
      smx_spc_push(apu, (Uint8)(apu->pc >> 8));
      smx_spc_push(apu, (Uint8)apu->pc);
      apu->pc =
          (Uint16)(smx_spc_read(apu, slot) | ((Uint16)smx_spc_read(apu, (Uint16)(slot + 1)) << 8));
      break;
    }
    if ((op & 0x0F) == 0x02 || (op & 0x0F) == 0x03) {
      Uint8 bit = (Uint8)(1u << (op >> 5));
      addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
      left = smx_spc_read(apu, addr);
      if ((op & 0x0F) == 0x02) {
        if ((op & 0x10) != 0) {
          left = (Uint8)(left & (Uint8)~bit);
        } else {
          left = (Uint8)(left | bit);
        }
        smx_spc_write(apu, addr, left);
      } else {
        rel = smx_spc_fetch(apu);
        take = (op & 0x10) != 0 ? (left & bit) == 0 : (left & bit) != 0;
        if (take) {
          apu->pc = (Uint16)(apu->pc + (Sint8)rel);
        }
      }
      break;
    }
    SDL_Log("spc: unimplemented opcode %u at %u", (unsigned)op, (unsigned)(apu->pc - 1));
    apu->running = false;
    return false;
  }
  smx_spc_spend(apu, op, take);
  if (!apu->program_ready && apu->pc < 0xFFC0) {
    apu->program_ready = true;
    apu->entry = apu->pc;
  }
  return true;
}

Uint32 smx_apu_drain(SmxApu *apu, Sint16 *frames, Uint32 max) {
  return smx_dsp_drain(&apu->dsp, frames, max);
}
