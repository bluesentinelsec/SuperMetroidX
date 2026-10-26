#include "smx/dma.h"

/* B-bus registers each transfer mode writes, in order. */
static const Uint8 smx_dma_offsets[8][4] = {
    {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
};
static const Uint8 smx_dma_unit[8] = {1, 2, 2, 4, 4, 4, 2, 4};

void smx_dma_init(SmxDma *dma, SmxBus *bus) {
  SDL_memset(dma, 0, sizeof(*dma));
  dma->bus = bus;
  dma->speed = 8;
}

static Uint8 smx_dma_reg(const SmxDma *dma, Uint8 channel, Uint8 offset) {
  return dma->bus->mmio[0x4300u + (Uint32)channel * 0x10u + offset];
}

static void smx_dma_set_reg(SmxDma *dma, Uint8 channel, Uint8 offset, Uint8 value) {
  dma->bus->mmio[0x4300u + (Uint32)channel * 0x10u + offset] = value;
}

static Uint16 smx_dma_reg16(const SmxDma *dma, Uint8 channel, Uint8 offset) {
  return (Uint16)(smx_dma_reg(dma, channel, offset) |
                  ((Uint16)smx_dma_reg(dma, channel, (Uint8)(offset + 1u)) << 8));
}

static void smx_dma_set_reg16(SmxDma *dma, Uint8 channel, Uint8 offset, Uint16 value) {
  smx_dma_set_reg(dma, channel, offset, (Uint8)value);
  smx_dma_set_reg(dma, channel, (Uint8)(offset + 1u), (Uint8)(value >> 8));
}

static void smx_dma_spend(SmxDma *dma, Uint32 clocks) {
  if (dma->advance != NULL) {
    dma->advance(dma->user, clocks);
  }
}

static Uint64 smx_dma_now(const SmxDma *dma) {
  return dma->now == NULL ? 0 : *dma->now;
}

static void smx_dma_put_log(SmxDma *dma, Uint8 value) {
  if (dma->log_count < sizeof(dma->log)) {
    dma->log[dma->log_count] = value;
    dma->log_count += 1;
  }
}

/* Through bus A the controller cannot reach the B bus or its own registers. */
static bool smx_dma_a_blocked(Uint32 addr) {
  Uint16 off = (Uint16)addr;

  if (((addr >> 16) & 0x7Fu) >= 0x40u) {
    return false;
  }
  return (off & 0xFF00u) == 0x2100u || off == 0x420B || off == 0x420C ||
         (off >= 0x4300 && off <= 0x437F);
}

static bool smx_dma_is_wram(Uint32 addr) {
  Uint8 bank = (Uint8)(addr >> 16);
  return bank == 0x7E || bank == 0x7F || ((bank & 0x7Fu) < 0x40u && (Uint16)addr < 0x2000u);
}

static Uint8 smx_dma_read_a(SmxDma *dma, Uint32 addr) {
  if (smx_dma_a_blocked(addr)) {
    return dma->bus->open_bus;
  }
  return smx_bus_read(dma->bus, addr);
}

static void smx_dma_write_a(SmxDma *dma, Uint32 addr, Uint8 value) {
  if (smx_dma_a_blocked(addr)) {
    dma->bus->open_bus = value;
    return;
  }
  smx_bus_write(dma->bus, addr, value);
}

/* One byte between bus A and B-bus register `$21xx`: 4 clocks to read, 4 to write. */
static void smx_dma_copy(SmxDma *dma, Uint32 a_addr, Uint8 b_reg, bool to_a) {
  Uint32 b_addr = 0x002100u | b_reg;
  Uint8 value = 0;

  if (b_reg == 0x80 && smx_dma_is_wram(a_addr)) {
    /* WRAM to WMDATA writes nothing; WMDATA to WRAM writes $FF without reading. */
    smx_dma_spend(dma, 8);
    if (to_a) {
      smx_dma_write_a(dma, a_addr, 0xFF);
    }
    return;
  }
  smx_dma_spend(dma, 4);
  value = to_a ? smx_bus_read(dma->bus, b_addr) : smx_dma_read_a(dma, a_addr);
  smx_dma_spend(dma, 4);
  if (to_a) {
    smx_dma_write_a(dma, a_addr, value);
  } else {
    smx_bus_write(dma->bus, b_addr, value);
  }
  smx_dma_put_log(dma, value);
}

/* Wait for the next multiple of 8 master clocks since reset; restart the count. */
static void smx_dma_sync_start(SmxDma *dma) {
  dma->counter = 8u - (Uint32)(smx_dma_now(dma) & 7u);
  smx_dma_spend(dma, dma->counter);
}

/* Pad the pause to a whole number of CPU cycles. */
static void smx_dma_sync_end(SmxDma *dma) {
  smx_dma_spend(dma, dma->speed - (dma->counter % dma->speed));
}

static bool smx_dma_service_inner(SmxDma *dma);

static void smx_dma_run_channel(SmxDma *dma, Uint8 channel) {
  Uint8 bit = (Uint8)(1u << channel);
  Uint8 control = smx_dma_reg(dma, channel, 0);
  Uint8 b_reg = smx_dma_reg(dma, channel, 1);
  Uint8 bank = smx_dma_reg(dma, channel, 4);
  bool to_a = (control & 0x80) != 0;
  bool fixed = (control & 0x08) != 0;
  Uint16 step = (control & 0x10) != 0 ? 0xFFFFu : 1u;
  /* The count of bytes moved wraps at 256 when padding is computed. */
  Uint8 moved = 0;

  dma->counter += 8;
  smx_dma_spend(dma, 8);
  smx_dma_service_inner(dma);
  do {
    Uint16 src = smx_dma_reg16(dma, channel, 2);
    Uint16 size = smx_dma_reg16(dma, channel, 5);
    smx_dma_copy(dma, ((Uint32)bank << 16) | src,
                 (Uint8)(b_reg + smx_dma_offsets[control & 7u][moved & 3u]), to_a);
    if (!fixed) {
      smx_dma_set_reg16(dma, channel, 2, (Uint16)(src + step));
    }
    smx_dma_set_reg16(dma, channel, 5, (Uint16)(size - 1u));
    moved = (Uint8)(moved + 1u);
    smx_dma_service_inner(dma);
  } while (smx_dma_reg16(dma, channel, 5) != 0 && (dma->active & bit) != 0);
  dma->counter += 8u * moved;
  dma->active = (Uint8)(dma->active & ~bit);
}

static bool smx_dma_run_gp(SmxDma *dma) {
  Uint8 channel = 0;

  dma->gp = false;
  dma->ran = true;
  dma->log_count = 0;
  smx_dma_sync_start(dma);
  smx_dma_spend(dma, 8);
  dma->counter += 8;
  smx_dma_service_inner(dma);
  for (; channel < 8; channel += 1) {
    if ((dma->active & (1u << channel)) != 0) {
      smx_dma_run_channel(dma, channel);
    }
  }
  smx_dma_sync_end(dma);
  return true;
}

/* One table byte for HDMA: 4 clocks to read it and 4 more. */
static Uint8 smx_dma_table_byte(SmxDma *dma, Uint8 channel, Uint16 *table) {
  Uint8 value = 0;

  smx_dma_spend(dma, 4);
  value = smx_dma_read_a(dma, ((Uint32)smx_dma_reg(dma, channel, 4) << 16) | *table);
  smx_dma_spend(dma, 4);
  *table = (Uint16)(*table + 1u);
  dma->counter += 8;
  return value;
}

static bool smx_dma_run_hdma_init(SmxDma *dma) {
  Uint8 enable = dma->bus->mmio[0x420C];
  bool sync = dma->active == 0;
  Uint8 channel = 0;

  dma->hdma_init = false;
  dma->hdma_done = 0;
  dma->hdma_do = 0;
  if (enable == 0) {
    return false;
  }
  if (sync) {
    smx_dma_sync_start(dma);
  }
  dma->counter += 8;
  smx_dma_spend(dma, 8);
  /* Every channel transfers on the first line once any HDMA channel is on. */
  dma->hdma_do = 0xFF;
  for (; channel < 8; channel += 1) {
    Uint8 bit = (Uint8)(1u << channel);
    Uint16 table = smx_dma_reg16(dma, channel, 2);
    Uint8 count = 0;
    if ((enable & bit) == 0) {
      continue;
    }
    dma->active = (Uint8)(dma->active & ~bit);
    count = smx_dma_table_byte(dma, channel, &table);
    if (count == 0) {
      dma->hdma_done = (Uint8)(dma->hdma_done | bit);
    }
    smx_dma_set_reg(dma, channel, 0x0A, count);
    if ((smx_dma_reg(dma, channel, 0) & 0x40) != 0) {
      Uint8 low = smx_dma_table_byte(dma, channel, &table);
      if ((dma->hdma_done & bit) == 0) {
        Uint8 high = smx_dma_table_byte(dma, channel, &table);
        smx_dma_set_reg16(dma, channel, 5, (Uint16)(low | ((Uint16)high << 8)));
      } else {
        smx_dma_set_reg16(dma, channel, 5, (Uint16)((Uint16)low << 8));
      }
    }
    smx_dma_set_reg16(dma, channel, 8, table);
  }
  if (sync) {
    smx_dma_sync_end(dma);
  }
  return true;
}

/* True when no enabled HDMA channel after `channel` is still running. */
static bool smx_dma_last_hdma(const SmxDma *dma, Uint8 channel, Uint8 enable) {
  Uint8 later = (Uint8)(enable & ~dma->hdma_done & (Uint8)(0xFFu << (channel + 1u)));
  return channel == 7 || later == 0;
}

static bool smx_dma_run_hdma(SmxDma *dma) {
  Uint8 enable = dma->bus->mmio[0x420C];
  bool sync = dma->active == 0;
  Uint8 channel = 0;

  dma->hdma = false;
  if (enable == 0) {
    return false;
  }
  if (sync) {
    smx_dma_sync_start(dma);
  }
  dma->counter += 8;
  smx_dma_spend(dma, 8);
  /* Every transfer for this line first, then each channel's table. */
  for (channel = 0; channel < 8; channel += 1) {
    Uint8 bit = (Uint8)(1u << channel);
    Uint8 control = smx_dma_reg(dma, channel, 0);
    Uint8 unit = 0;
    Uint8 index = 0;
    if ((enable & bit) == 0) {
      continue;
    }
    dma->active = (Uint8)(dma->active & ~bit);
    if ((dma->hdma_done & bit) != 0 || (dma->hdma_do & bit) == 0) {
      continue;
    }
    unit = smx_dma_unit[control & 7u];
    for (; index < unit; index += 1) {
      Uint8 b_reg = (Uint8)(smx_dma_reg(dma, channel, 1) + smx_dma_offsets[control & 7u][index]);
      if ((control & 0x40) != 0) {
        Uint16 at = smx_dma_reg16(dma, channel, 5);
        smx_dma_copy(dma, ((Uint32)smx_dma_reg(dma, channel, 7) << 16) | at, b_reg,
                     (control & 0x80) != 0);
        smx_dma_set_reg16(dma, channel, 5, (Uint16)(at + 1u));
      } else {
        Uint16 at = smx_dma_reg16(dma, channel, 8);
        smx_dma_copy(dma, ((Uint32)smx_dma_reg(dma, channel, 4) << 16) | at, b_reg,
                     (control & 0x80) != 0);
        smx_dma_set_reg16(dma, channel, 8, (Uint16)(at + 1u));
      }
    }
    dma->counter += 8u * unit;
  }
  for (channel = 0; channel < 8; channel += 1) {
    Uint8 bit = (Uint8)(1u << channel);
    Uint16 table = smx_dma_reg16(dma, channel, 8);
    Uint8 count = 0;
    Uint8 next = 0;
    if ((enable & bit) == 0 || (dma->hdma_done & bit) != 0) {
      continue;
    }
    count = (Uint8)(smx_dma_reg(dma, channel, 0x0A) - 1u);
    dma->hdma_do = (count & 0x80) != 0 ? (Uint8)(dma->hdma_do | bit) : (Uint8)(dma->hdma_do & ~bit);
    /* The next table byte is read every line and kept only when the count runs out. */
    next = smx_dma_table_byte(dma, channel, &table);
    table = (Uint16)(table - 1u);
    if ((count & 0x7F) == 0) {
      count = next;
      table = (Uint16)(table + 1u);
      if ((smx_dma_reg(dma, channel, 0) & 0x40) != 0) {
        if (count == 0 && smx_dma_last_hdma(dma, channel, enable)) {
          /* A last channel ending its table loads only the high byte of the address. */
          Uint8 high = smx_dma_table_byte(dma, channel, &table);
          smx_dma_set_reg16(dma, channel, 5, (Uint16)((Uint16)high << 8));
        } else {
          Uint8 low = smx_dma_table_byte(dma, channel, &table);
          Uint8 high = smx_dma_table_byte(dma, channel, &table);
          smx_dma_set_reg16(dma, channel, 5, (Uint16)(low | ((Uint16)high << 8)));
        }
      }
      if (count == 0) {
        dma->hdma_done = (Uint8)(dma->hdma_done | bit);
      }
      dma->hdma_do = (Uint8)(dma->hdma_do | bit);
    }
    smx_dma_set_reg(dma, channel, 0x0A, count);
    smx_dma_set_reg16(dma, channel, 8, table);
  }
  if (sync) {
    smx_dma_sync_end(dma);
  }
  return true;
}

static bool smx_dma_service_inner(SmxDma *dma) {
  if (!smx_dma_pending(dma)) {
    return false;
  }
  if (dma->delay) {
    dma->delay = false;
    return false;
  }
  if (dma->hdma) {
    return smx_dma_run_hdma(dma);
  }
  if (dma->hdma_init) {
    return smx_dma_run_hdma_init(dma);
  }
  if (dma->gp) {
    return smx_dma_run_gp(dma);
  }
  return false;
}

bool smx_dma_service(SmxDma *dma) {
  return smx_dma_service_inner(dma);
}

bool smx_dma_pending(const SmxDma *dma) {
  return dma->delay || dma->gp || dma->hdma || dma->hdma_init;
}

void smx_dma_request(SmxDma *dma, Uint8 channels) {
  dma->active = (Uint8)(dma->active | channels);
  if (channels != 0) {
    dma->gp = true;
    dma->delay = true;
  }
}

void smx_dma_request_hdma_init(SmxDma *dma) {
  dma->delay = true;
  dma->hdma_init = true;
}

void smx_dma_request_hdma(SmxDma *dma) {
  if (dma->bus->mmio[0x420C] != 0) {
    dma->hdma = true;
    dma->delay = true;
  }
}
