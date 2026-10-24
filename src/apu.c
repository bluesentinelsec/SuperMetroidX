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

static void smx_apu_begin_block(SmxApu *apu) {
  apu->block = (Uint16)(apu->in[2] | ((Uint16)apu->in[3] << 8));
  apu->out[0] = apu->in[0];
  if (apu->in[1] == 0) {
    apu->entry = apu->block;
    apu->program_ready = true;
    apu->receiving = false;
    return;
  }
  apu->receiving = true;
  apu->wait_zero = true;
  apu->index = 0;
}

static void smx_apu_on_write(SmxApu *apu, Uint8 index) {
  if (apu->program_ready) {
    return;
  }
  if (!apu->receiving) {
    if (index != 0) {
      return;
    }
    if (!apu->started && apu->in[0] != 0xCC) {
      return;
    }
    apu->started = true;
    smx_apu_begin_block(apu);
    return;
  }
  if (apu->wait_zero) {
    if (index == 0 && apu->in[0] == 0) {
      apu->wait_zero = false;
    }
    return;
  }
  if (index == 1 && apu->in[0] == apu->index) {
    Uint16 at = (Uint16)(apu->block + apu->index);
    apu->ram[at] = apu->in[1];
    apu->out[0] = apu->index;
    apu->index = (Uint8)(apu->index + 1);
    if (apu->index == 0) {
      apu->block = (Uint16)(apu->block + 0x100);
    }
    return;
  }
  if (index == 0 && apu->in[0] != apu->index) {
    Sint8 gap = (Sint8)(apu->index - apu->in[0]);
    if (gap < 0) {
      apu->receiving = false;
      smx_apu_begin_block(apu);
    }
  }
}

static void smx_apu_cpu_write(void *user, Uint8 index, Uint8 value) {
  SmxApu *apu = user;

  apu->in[index] = value;
  smx_apu_on_write(apu, index);
  smx_apu_publish(apu);
}

void smx_apu_init(SmxApu *apu, SmxBus *bus) {
  SDL_memset(apu, 0, sizeof(*apu));
  apu->bus = bus;
  apu->out[0] = 0xAA;
  apu->out[1] = 0xBB;
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
  (void)apu;
  return false;
}
