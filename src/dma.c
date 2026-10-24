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

void smx_dma_run(SmxDma *dma, Uint8 channels) {
  Uint8 channel = 0;

  dma->ran = true;
  dma->log_count = 0;
  for (; channel < 8; channel += 1) {
    Uint8 control = 0;
    Uint8 b_addr = 0;
    Uint32 a_addr = 0;
    Uint16 count = 0;
    Uint8 pattern = 0;
    bool to_a = false;
    bool fixed = false;
    Sint32 a_step = 0;
    Uint16 step = 0;

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
    pattern = (Uint8)(control & 0x07);
    to_a = (control & 0x80) != 0;
    fixed = (control & 0x08) != 0;
    a_step = (control & 0x10) != 0 ? -1 : 1;
    /* A count of 0 asks for 65536 bytes. */
    {
      Uint32 remaining = count == 0 ? 65536u : count;

      while (remaining != 0) {
        Uint8 b_off = 0;
        Uint32 b_bus = 0;
        Uint8 value = 0;

        if (pattern == 1 || pattern == 2) {
          b_off = (Uint8)(step & 1);
        } else if (pattern == 3 || pattern == 4) {
          b_off = (Uint8)(step & 3);
          if (b_off == 3) {
            b_off = 1;
          }
        }
        b_bus = 0x002100u + b_addr + b_off;
        if (to_a) {
          value = smx_bus_read(dma->bus, b_bus);
          smx_bus_write(dma->bus, a_addr, value);
        } else {
          value = smx_bus_read(dma->bus, a_addr);
          smx_bus_write(dma->bus, b_bus, value);
        }
        put_log(dma, value);
        if (!fixed) {
          a_addr = (a_addr + (Uint32)a_step) & 0x00FFFFFFu;
        }
        step = (Uint16)(step + 1);
        remaining -= 1;
      }
    }
  }
}
