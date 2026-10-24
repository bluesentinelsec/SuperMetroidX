#ifndef SMX_APU_H_
#define SMX_APU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * APU as seen from the 65816.
 * Reset runs the SPC700 IPL handshake on `$2140-$2143` and copies the boot
 * upload into APU RAM. The SPC700 program is not executed.
 * `smx_apu_core_running` stays false until that core exists.
 */
typedef struct SmxApu {
  SmxBus *bus;
  Uint8 ram[65536];
  Uint8 in[4];
  Uint8 out[4];
  Uint16 block;
  Uint16 entry;
  Uint8 index;
  bool started;
  bool receiving;
  bool wait_zero;
  bool program_ready;
} SmxApu;

void smx_apu_init(SmxApu *apu, SmxBus *bus);
void smx_apu_write_port(SmxApu *apu, Uint8 index, Uint8 value);
Uint8 smx_apu_read_port(const SmxApu *apu, Uint8 index);
Uint8 smx_apu_ram(const SmxApu *apu, Uint16 addr);
bool smx_apu_core_running(const SmxApu *apu);

#endif /* SMX_APU_H_ */
