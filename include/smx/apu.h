#ifndef SMX_APU_H_
#define SMX_APU_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * CPU-side APU ports `$2140-$2143`.
 * The SPC700 is not executed. That waits until a rendered frame matches an
 * NTSC oracle, which `smx_apu_core_running` reports.
 */
typedef struct SmxApu {
  SmxBus *bus;
  Uint8 port[4];
} SmxApu;

void smx_apu_init(SmxApu *apu, SmxBus *bus);
void smx_apu_write_port(SmxApu *apu, Uint8 index, Uint8 value);
Uint8 smx_apu_read_port(const SmxApu *apu, Uint8 index);
bool smx_apu_core_running(const SmxApu *apu);

#endif /* SMX_APU_H_ */
