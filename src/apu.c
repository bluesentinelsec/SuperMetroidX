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
  apu->noise = 0x4000;
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

static Uint8 smx_spc_fetch(SmxApu *apu) {
  Uint8 value = smx_spc_read(apu, apu->pc);
  apu->pc = (Uint16)(apu->pc + 1);
  return value;
}

static Uint8 smx_spc_read(SmxApu *apu, Uint16 addr) {
  if (addr >= 0x00F4 && addr <= 0x00F7) {
    return apu->in[addr - 0x00F4];
  }
  if (addr == 0x00F2) {
    return apu->dsp_addr;
  }
  if (addr == 0x00F3) {
    return apu->dsp[apu->dsp_addr & 0x7F];
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
  if (addr >= 0x00F4 && addr <= 0x00F7) {
    apu->out[addr - 0x00F4] = value;
    smx_apu_publish(apu);
    return;
  }
  if (addr == 0x00F1) {
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
    apu->dsp[apu->dsp_addr & 0x7F] = value;
    if ((apu->dsp_addr & 0x7F) == 0x4C) {
      Uint8 voice = 0;
      Uint16 dir = (Uint16)((Uint16)apu->dsp[0x5D] << 8);
      for (; voice < 8; voice += 1) {
        Uint16 entry = 0;
        if ((value & (Uint8)(1u << voice)) == 0) {
          continue;
        }
        entry = (Uint16)(dir + (Uint16)apu->dsp[(Uint8)(voice * 0x10u + 0x04u)] * 4u);
        apu->voice_addr[voice] = (Uint16)(apu->ram[entry] | ((Uint16)apu->ram[entry + 1u] << 8));
        apu->voice_loop[voice] =
            (Uint16)(apu->ram[entry + 2u] | ((Uint16)apu->ram[entry + 3u] << 8));
        apu->voice_p1[voice] = 0;
        apu->voice_p2[voice] = 0;
        apu->voice_phase[voice] = 0;
        if ((apu->dsp[(Uint8)(voice * 0x10u + 0x05u)] & 0x80) != 0) {
          apu->voice_env[voice] = 0;
          apu->voice_state[voice] = 1;
        } else {
          apu->voice_env[voice] = 0x07FF;
          apu->voice_state[voice] = 0;
        }
        apu->voice_i[voice] = 16;
        apu->voice_on[voice] = true;
      }
    }
    if ((apu->dsp_addr & 0x7F) == 0x5C) {
      Uint8 voice = 0;
      for (; voice < 8; voice += 1) {
        if ((value & (Uint8)(1u << voice)) == 0) {
          continue;
        }
        if ((apu->dsp[(Uint8)(voice * 0x10u + 0x05u)] & 0x80) != 0 &&
            apu->voice_state[voice] != 0) {
          apu->voice_state[voice] = 4;
        } else {
          apu->voice_on[voice] = false;
          apu->voice_i[voice] = 16;
        }
      }
    }
    if ((apu->dsp_addr & 0x0F) == 0x07 && (apu->dsp_addr & 0x70) < 0x80 && (value & 0x80) == 0) {
      apu->voice_env[apu->dsp_addr >> 4] = (Uint16)((value & 0x7F) << 4);
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

static void smx_spc_timers(SmxApu *apu) {
  static const Uint16 rate[3] = {128, 128, 16};
  Uint8 index = 0;

  for (; index < 3; index += 1) {
    Uint16 limit = 0;
    if ((apu->control & (Uint8)(1u << index)) == 0) {
      continue;
    }
    apu->timer_div[index] = (Uint16)(apu->timer_div[index] + 4);
    if (apu->timer_div[index] < rate[index]) {
      continue;
    }
    apu->timer_div[index] = (Uint16)(apu->timer_div[index] - rate[index]);
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
  if ((op & 0x1F) == 0x18 && op < 0xC0) {
    right = smx_spc_fetch(apu);
    addr = (Uint16)(smx_spc_page(apu) | smx_spc_fetch(apu));
    left = smx_spc_read(apu, addr);
    left = smx_spc_logic(apu, op, left, right);
    if ((op & 0xE0) != 0x60) {
      smx_spc_write(apu, addr, left);
    }
    smx_spc_timers(apu);
    return true;
  }
  if (smx_spc_move_a(apu, op)) {
    smx_spc_timers(apu);
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
      smx_spc_timers(apu);
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
      smx_spc_timers(apu);
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
    smx_spc_timers(apu);
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
  smx_spc_timers(apu);
  if (!apu->program_ready && apu->pc < 0xFFC0) {
    apu->program_ready = true;
    apu->entry = apu->pc;
  }
  return true;
}

static void smx_apu_brr_decode_impl(const Uint8 *block, Sint16 *out, Sint16 *prev1, Sint16 *prev2);

static void smx_apu_echo(SmxApu *apu, Sint32 input_l, Sint32 input_r, Sint32 *mixed_l,
                         Sint32 *mixed_r) {
  Uint8 delay = (Uint8)(apu->dsp[0x7D] & 0x0F);
  Uint16 length = 0;
  Sint32 fir_l = 0;
  Sint32 fir_r = 0;
  Sint32 write_l = 0;
  Sint32 write_r = 0;
  Uint8 tap = 0;

  if (delay == 0) {
    return;
  }
  length = (Uint16)((Uint16)delay * 512u);
  if (apu->echo_pos >= length) {
    apu->echo_pos = 0;
  }
  for (; tap < 8; tap += 1) {
    Uint16 index = (Uint16)((apu->echo_pos + length - tap) % length);
    Sint8 coeff = (Sint8)apu->dsp[(Uint8)(0x0Fu + (Uint8)(tap * 0x10u))];
    fir_l += (Sint32)apu->echo_l[index] * coeff;
    fir_r += (Sint32)apu->echo_r[index] * coeff;
  }
  fir_l >>= 7;
  fir_r >>= 7;
  *mixed_l += (fir_l * (Sint8)apu->dsp[0x2C]) >> 7;
  *mixed_r += (fir_r * (Sint8)apu->dsp[0x3C]) >> 7;
  write_l = input_l + ((fir_l * (Sint8)apu->dsp[0x0D]) >> 7);
  write_r = input_r + ((fir_r * (Sint8)apu->dsp[0x0D]) >> 7);
  if (write_l > 32767) {
    write_l = 32767;
  }
  if (write_l < -32768) {
    write_l = -32768;
  }
  if (write_r > 32767) {
    write_r = 32767;
  }
  if (write_r < -32768) {
    write_r = -32768;
  }
  apu->echo_l[apu->echo_pos] = (Sint16)write_l;
  apu->echo_r[apu->echo_pos] = (Sint16)write_r;
  apu->echo_pos = (Uint16)((apu->echo_pos + 1u) % length);
}

static void smx_apu_envelope(SmxApu *apu, Uint8 voice) {
  Uint8 base = (Uint8)(voice * 0x10u);
  Uint8 adsr1 = apu->dsp[base + 5u];
  Uint8 adsr2 = apu->dsp[base + 6u];
  Sint32 env = apu->voice_env[voice];
  Uint16 sustain = (Uint16)((((adsr2 >> 5) + 1u) * 0x100u) - 1u);

  if ((adsr1 & 0x80) == 0 || apu->voice_state[voice] == 0) {
    return;
  }
  if (apu->voice_state[voice] == 1) {
    if ((adsr1 & 0x0F) == 15) {
      env = 0x07FF;
    } else {
      env += (Sint32)(1u << (adsr1 & 0x0F));
    }
    if (env >= 0x07FF) {
      env = 0x07FF;
      apu->voice_state[voice] = 2;
    }
  }
  if (apu->voice_state[voice] == 2) {
    env -= 32;
    if (env <= (Sint32)sustain) {
      env = sustain;
      apu->voice_state[voice] = 3;
    }
  } else if (apu->voice_state[voice] == 3) {
    if ((adsr2 & 0x1F) != 0) {
      env -= 1;
    }
  } else if (apu->voice_state[voice] == 4) {
    env -= 0x40;
    if (env <= 0) {
      env = 0;
      apu->voice_on[voice] = false;
      apu->voice_i[voice] = 16;
    }
  }
  if (env < 0) {
    env = 0;
  }
  if (env > 0x07FF) {
    env = 0x07FF;
  }
  apu->voice_env[voice] = (Uint16)env;
}

void smx_apu_render(SmxApu *apu, Sint16 *left, Sint16 *right, Uint32 count) {
  Uint32 i = 0;

  for (; i < count; i += 1) {
    Sint32 mixed_l = 0;
    Sint32 echo_l = 0;
    Sint32 echo_r = 0;
    Uint16 noise_bit = (Uint16)((apu->noise ^ (Uint16)(apu->noise >> 1)) & 1u);
    apu->noise = (Uint16)((apu->noise >> 1) | (noise_bit << 14));
    Sint32 mixed_r = 0;
    Uint8 voice = 0;
    for (; voice < 8; voice += 1) {
      Uint8 base = (Uint8)(voice * 0x10u);
      Sint32 sample = 0;
      Sint8 volume_l = (Sint8)apu->dsp[base];
      Sint8 volume_r = (Sint8)apu->dsp[base + 1u];
      if (apu->voice_on[voice]) {
        smx_apu_envelope(apu, voice);
      }
      if (apu->voice_on[voice] && apu->voice_i[voice] >= 16) {
        Uint8 header = apu->ram[apu->voice_addr[voice]];
        smx_apu_brr_decode_impl(&apu->ram[apu->voice_addr[voice]], apu->voice_buf[voice],
                                &apu->voice_p1[voice], &apu->voice_p2[voice]);
        apu->voice_i[voice] = 0;
        if ((header & 0x01) != 0) {
          if ((header & 0x02) != 0) {
            apu->voice_addr[voice] = apu->voice_loop[voice];
          } else {
            apu->voice_on[voice] = false;
          }
        } else {
          apu->voice_addr[voice] = (Uint16)(apu->voice_addr[voice] + 9u);
        }
      }
      if (apu->voice_i[voice] < 16) {
        Uint16 pitch = (Uint16)(apu->dsp[base + 2u] | ((Uint16)apu->dsp[base + 3u] << 8));
        sample = apu->voice_buf[voice][apu->voice_i[voice]];
        if ((apu->dsp[0x3D] & (Uint8)(1u << voice)) != 0) {
          sample = (Sint16)apu->noise;
        }
        sample = (sample * (Sint32)apu->voice_env[voice]) >> 11;
        apu->voice_phase[voice] = (Uint16)(apu->voice_phase[voice] + pitch);
        while (apu->voice_phase[voice] >= 0x1000) {
          apu->voice_phase[voice] = (Uint16)(apu->voice_phase[voice] - 0x1000);
          apu->voice_i[voice] = (Uint8)(apu->voice_i[voice] + 1u);
          if (apu->voice_i[voice] >= 16) {
            break;
          }
        }
      }
      mixed_l += (sample * volume_l) >> 7;
      mixed_r += (sample * volume_r) >> 7;
      if ((apu->dsp[0x4D] & (Uint8)(1u << voice)) != 0) {
        echo_l += (sample * volume_l) >> 7;
        echo_r += (sample * volume_r) >> 7;
      }
    }
    mixed_l = (mixed_l * (Sint8)apu->dsp[0x0C]) >> 7;
    mixed_r = (mixed_r * (Sint8)apu->dsp[0x1C]) >> 7;
    smx_apu_echo(apu, echo_l, echo_r, &mixed_l, &mixed_r);
    if ((apu->dsp[0x6C] & 0x80) != 0) {
      mixed_l = 0;
      mixed_r = 0;
    }
    if (mixed_l > 32767) {
      mixed_l = 32767;
    }
    if (mixed_l < -32768) {
      mixed_l = -32768;
    }
    if (mixed_r > 32767) {
      mixed_r = 32767;
    }
    if (mixed_r < -32768) {
      mixed_r = -32768;
    }
    left[i] = (Sint16)mixed_l;
    if (right != NULL) {
      right[i] = (Sint16)mixed_r;
    }
  }
}

static void smx_apu_brr_decode_impl(const Uint8 *block, Sint16 *out, Sint16 *prev1, Sint16 *prev2) {
  Uint8 header = block[0];
  Uint8 shift = (Uint8)(header >> 4);
  Uint8 filter = (Uint8)((header >> 2) & 0x03);
  Sint16 older = *prev2;
  Sint16 newer = *prev1;
  Uint8 i = 0;

  for (; i < 16; i += 1) {
    Uint8 packed = block[1u + (i >> 1)];
    Sint8 nibble = (Sint8)((Sint8)(((i & 1u) == 0 ? (packed >> 4) : (packed & 0x0F)) << 4) >> 4);
    Sint32 sample = ((Sint32)nibble << shift) >> 1;
    if (filter == 1) {
      sample += (Sint32)newer - ((Sint32)newer >> 4);
    } else if (filter == 2) {
      sample += ((Sint32)newer * 61 >> 5) - ((Sint32)older * 15 >> 4);
    } else if (filter == 3) {
      sample += ((Sint32)newer * 115 >> 6) - ((Sint32)older * 13 >> 4);
    }
    if (sample > 32767) {
      sample = 32767;
    }
    if (sample < -32768) {
      sample = -32768;
    }
    out[i] = (Sint16)sample;
    older = newer;
    newer = (Sint16)sample;
  }
  *prev1 = newer;
  *prev2 = older;
}

void smx_apu_brr_decode(const Uint8 *block, Sint16 *out, Sint16 *prev1, Sint16 *prev2) {
  smx_apu_brr_decode_impl(block, out, prev1, prev2);
}
