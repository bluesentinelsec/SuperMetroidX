#ifndef SMX_DMA_H_
#define SMX_DMA_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/** General-purpose DMA. HDMA is latched and not run. */
typedef struct SmxDma {
  SmxBus *bus;
  Uint8 log[64];
  Uint32 log_count;
  bool ran;
} SmxDma;

void smx_dma_init(SmxDma *dma, SmxBus *bus);

/**
 * Run the channels selected by `$420B`.
 * Each set bit starts one general-purpose transfer. `$420C` is left for HDMA.
 */
void smx_dma_run(SmxDma *dma, Uint8 channels);

#endif /* SMX_DMA_H_ */
