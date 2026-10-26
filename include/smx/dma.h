#ifndef SMX_DMA_H_
#define SMX_DMA_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * The DMA controller.
 * Channel registers are the bus's `$43x0-$43xA`. Transfers update them as
 * they run, so the CPU reads the live address, count, table pointer, and
 * line counter.
 *
 * Work is requested at a point in time and taken at the start of a CPU
 * cycle by `smx_dma_service`. The first cycle start after a request only
 * clears `delay`; the next one does the work.
 *
 * `advance` spends master clocks, and `now` reads the master clock. The
 * clock may request HDMA meanwhile, and a general-purpose transfer lets it
 * run between bytes. Each transfer starts on an 8-clock boundary and pads
 * its end to a whole CPU cycle of `speed` clocks. `counter` holds the
 * clocks spent since the start, for that padding. Without `advance`, no
 * time passes.
 *
 * The other fields:
 * - `active`: channels whose general-purpose transfer has not finished.
 * - `hdma_done`: HDMA channels finished for this frame.
 * - `hdma_do`: HDMA channels that transfer on the next line.
 * - `log`: the bytes of the last general-purpose transfer, for tests.
 */
typedef struct SmxDma {
  SmxBus *bus;
  void (*advance)(void *user, Uint32 clocks);
  const Uint64 *now;
  void *user;
  Uint8 speed;
  bool delay;
  bool gp;
  bool hdma;
  bool hdma_init;
  Uint8 active;
  Uint8 hdma_done;
  Uint8 hdma_do;
  Uint32 counter;
  Uint8 log[64];
  Uint32 log_count;
  bool ran;
} SmxDma;

void smx_dma_init(SmxDma *dma, SmxBus *bus);

/** A write of `channels` to `$420B`: start general-purpose transfers. */
void smx_dma_request(SmxDma *dma, Uint8 channels);

/** Line 0 has reached the point where HDMA reloads its tables. */
void smx_dma_request_hdma_init(SmxDma *dma);

/** A line has reached the point where HDMA transfers, if any channel is enabled. */
void smx_dma_request_hdma(SmxDma *dma);

/** True when a request is waiting for a CPU cycle start. */
bool smx_dma_pending(const SmxDma *dma);

/** At the start of a CPU cycle: take the delay or run the waiting work. True when a transfer ran.
 */
bool smx_dma_service(SmxDma *dma);

#endif /* SMX_DMA_H_ */
