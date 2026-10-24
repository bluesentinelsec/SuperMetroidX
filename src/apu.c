#include "smx/apu.h"

void smx_apu_init(SmxApu *apu, SmxBus *bus) {
  SDL_memset(apu, 0, sizeof(*apu));
  apu->bus = bus;
}

void smx_apu_write_port(SmxApu *apu, Uint8 index, Uint8 value) {
  if (index > 3) {
    return;
  }
  apu->port[index] = value;
  smx_bus_write(apu->bus, 0x002140u + index, value);
}

Uint8 smx_apu_read_port(const SmxApu *apu, Uint8 index) {
  if (index > 3) {
    return 0;
  }
  return apu->port[index];
}

bool smx_apu_core_running(const SmxApu *apu) {
  (void)apu;
  return false;
}
