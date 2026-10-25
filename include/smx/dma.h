#ifndef SMX_DMA_H_
#define SMX_DMA_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * General-purpose DMA and HDMA.
 * HDMA reloads at the start of each frame and transfers once per active line.
 */
typedef struct SmxDma {
  SmxBus *bus;
  Uint8 log[64];
  Uint32 log_count;
  bool ran;
  Uint32 hdma_addr[8];
  Uint16 hdma_indirect[8];
  Uint8 hdma_lines[8];
  bool hdma_repeat[8];
  bool hdma_do[8];
  bool hdma_done[8];
} SmxDma;

void smx_dma_init(SmxDma *dma, SmxBus *bus);

/**
 * Run the channels selected by `$420B`.
 * Each set bit starts one general-purpose transfer.
 * Returns the master clocks the CPU is paused: 8 to start, then 8 for each
 * channel and 8 for each byte.
 */
Uint32 smx_dma_run(SmxDma *dma, Uint8 channels);

/**
 * Load HDMA line counts from the channels enabled in `$420C`.
 * Returns the master clocks spent: 18 when any channel is enabled, 8 for each
 * table entry, and 16 more for each indirect address.
 */
Uint32 smx_dma_hdma_init(SmxDma *dma);

/**
 * Run one scanline of HDMA. Channels with a repeat flag transfer again.
 * Returns the master clocks spent: 18 when any channel is active, 8 for each
 * active channel, 8 for each byte, and the cost of any table entry it loads.
 */
Uint32 smx_dma_hdma_line(SmxDma *dma);

#endif /* SMX_DMA_H_ */
