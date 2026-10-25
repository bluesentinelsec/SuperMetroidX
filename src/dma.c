#include "smx/dma.h"

void smx_dma_init(SmxDma *dma, SmxBus *bus) {
  SDL_memset(dma, 0, sizeof(*dma));
  dma->bus = bus;
}

static Uint8 channel_reg(const SmxDma *dma, Uint8 channel, Uint16 offset) {
  return smx_bus_mmio(dma->bus, (Uint16)(0x4300 + (Uint16)channel * 0x10 + offset));
}

static void put_log(SmxDma *dma, Uint8 value) {
  if (dma->log_count < sizeof(dma->log)) {
    dma->log[dma->log_count] = value;
    dma->log_count += 1;
  }
}

static Uint8 pattern_len(Uint8 pattern) {
  static const Uint8 len[8] = {1, 2, 2, 4, 4, 4, 2, 4};
  return len[pattern & 7];
}

static Uint8 pattern_off(Uint8 pattern, Uint8 step) {
  static const Uint8 off[8][4] = {
      {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
      {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
  };
  return off[pattern & 7][step & 3];
}

static void transfer_unit(SmxDma *dma, Uint8 pattern, Uint8 b_addr, Uint32 *a_addr, bool to_a,
                          bool fixed, Sint32 a_step) {
  Uint8 step = 0;
  Uint8 len = pattern_len(pattern);

  for (; step < len; step += 1) {
    Uint32 b_bus = 0x002100u + b_addr + pattern_off(pattern, step);
    Uint8 value = 0;

    if (to_a) {
      value = smx_bus_read(dma->bus, b_bus);
      smx_bus_write(dma->bus, *a_addr, value);
    } else {
      value = smx_bus_read(dma->bus, *a_addr);
      smx_bus_write(dma->bus, b_bus, value);
    }
    put_log(dma, value);
    if (!fixed) {
      *a_addr = (*a_addr + (Uint32)a_step) & 0x00FFFFFFu;
    }
  }
}

Uint32 smx_dma_run(SmxDma *dma, Uint8 channels) {
  Uint8 channel = 0;
  Uint32 clocks = 8;

  dma->ran = true;
  dma->log_count = 0;
  for (; channel < 8; channel += 1) {
    Uint8 control = 0;
    Uint8 b_addr = 0;
    Uint32 a_addr = 0;
    Uint16 count = 0;
    bool to_a = false;
    bool fixed = false;
    Sint32 a_step = 0;
    Uint32 remaining = 0;

    if ((channels & (Uint8)(1u << channel)) == 0) {
      continue;
    }
    control = channel_reg(dma, channel, 0);
    b_addr = channel_reg(dma, channel, 1);
    a_addr = (Uint32)channel_reg(dma, channel, 2);
    a_addr |= (Uint32)channel_reg(dma, channel, 3) << 8;
    a_addr |= (Uint32)channel_reg(dma, channel, 4) << 16;
    count = (Uint16)channel_reg(dma, channel, 5);
    count = (Uint16)(count | ((Uint16)channel_reg(dma, channel, 6) << 8));
    to_a = (control & 0x80) != 0;
    fixed = (control & 0x08) != 0;
    a_step = (control & 0x10) != 0 ? -1 : 1;
    remaining = count == 0 ? 65536u : count;
    clocks += 8u + remaining * 8u;
    while (remaining != 0) {
      transfer_unit(dma, (Uint8)(control & 0x07), b_addr, &a_addr, to_a, fixed, a_step);
      remaining -= pattern_len((Uint8)(control & 0x07));
      if (remaining > 65536u) {
        break;
      }
    }
  }
  return clocks;
}

/* Returns the master clocks spent reading the table entry. */
static Uint32 hdma_load(SmxDma *dma, Uint8 channel) {
  Uint8 control = channel_reg(dma, channel, 0);
  Uint8 count = smx_bus_read(dma->bus, dma->hdma_addr[channel]);

  dma->hdma_addr[channel] = (dma->hdma_addr[channel] + 1u) & 0x00FFFFFFu;
  if (count == 0) {
    dma->hdma_done[channel] = true;
    dma->hdma_do[channel] = false;
    return 8;
  }
  dma->hdma_lines[channel] = (Uint8)(count & 0x7F);
  dma->hdma_repeat[channel] = (count & 0x80) != 0;
  dma->hdma_do[channel] = true;
  if ((control & 0x40) != 0) {
    Uint8 low = smx_bus_read(dma->bus, dma->hdma_addr[channel]);
    dma->hdma_addr[channel] = (dma->hdma_addr[channel] + 1u) & 0x00FFFFFFu;
    dma->hdma_indirect[channel] =
        (Uint16)(low | ((Uint16)smx_bus_read(dma->bus, dma->hdma_addr[channel]) << 8));
    dma->hdma_addr[channel] = (dma->hdma_addr[channel] + 1u) & 0x00FFFFFFu;
    return 24;
  }
  return 8;
}

Uint32 smx_dma_hdma_init(SmxDma *dma) {
  Uint8 enable = smx_bus_mmio(dma->bus, 0x420C);
  Uint8 channel = 0;
  Uint32 clocks = 0;

  for (; channel < 8; channel += 1) {
    dma->hdma_done[channel] = true;
    dma->hdma_do[channel] = false;
    if ((enable & (Uint8)(1u << channel)) == 0) {
      continue;
    }
    dma->hdma_addr[channel] = (Uint32)channel_reg(dma, channel, 2);
    dma->hdma_addr[channel] |= (Uint32)channel_reg(dma, channel, 3) << 8;
    dma->hdma_addr[channel] |= (Uint32)channel_reg(dma, channel, 4) << 16;
    dma->hdma_done[channel] = false;
    clocks += hdma_load(dma, channel);
  }
  return clocks == 0 ? 0 : clocks + 18u;
}

Uint32 smx_dma_hdma_line(SmxDma *dma) {
  Uint8 enable = smx_bus_mmio(dma->bus, 0x420C);
  Uint8 channel = 0;
  Uint32 clocks = 0;

  for (; channel < 8; channel += 1) {
    Uint8 control = 0;
    Uint32 source = 0;
    bool indirect = false;

    if ((enable & (Uint8)(1u << channel)) == 0 || dma->hdma_done[channel]) {
      continue;
    }
    control = channel_reg(dma, channel, 0);
    indirect = (control & 0x40) != 0;
    clocks += 8u;
    if (dma->hdma_do[channel]) {
      clocks += 8u * pattern_len((Uint8)(control & 0x07));
      if (indirect) {
        source = ((Uint32)channel_reg(dma, channel, 7) << 16) | dma->hdma_indirect[channel];
      } else {
        source = dma->hdma_addr[channel];
      }
      transfer_unit(dma, (Uint8)(control & 0x07), channel_reg(dma, channel, 1), &source,
                    (control & 0x80) != 0, false, 1);
      if (indirect) {
        dma->hdma_indirect[channel] = (Uint16)source;
      } else {
        dma->hdma_addr[channel] = source;
      }
    }
    if (dma->hdma_lines[channel] > 1) {
      dma->hdma_lines[channel] = (Uint8)(dma->hdma_lines[channel] - 1);
      dma->hdma_do[channel] = dma->hdma_repeat[channel];
    } else {
      clocks += hdma_load(dma, channel);
    }
  }
  return clocks == 0 ? 0 : clocks + 18u;
}
