#include "smx/cart.h"
#include "smx/clock.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_u8(const char *name, Uint8 got, Uint8 want) {
  if (got != want) {
    SDL_Log("clock: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("clock: %s", name);
    g_failed = true;
  }
}

static void test_vblank_poll(void) {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxClock clock;
  static const Uint8 code[] = {0xA2, 0x01, 0xAD, 0x12, 0x42, 0x10, 0xFB, 0xAD, 0x12,
                               0x42, 0x30, 0xFB, 0xCA, 0xD0, 0xF3, 0xA9, 0x55};

  smx_bus_init(&bus, NULL, 0, 0);
  smx_cpu_init(&cpu, &bus);
  smx_dma_init(&dma, &bus);
  smx_clock_init(&clock, &cpu, &dma);
  cpu.e = false;
  cpu.p = 0x30;
  cpu.s = 0x01FF;
  cpu.pc = 0x1000;
  {
    Uint32 index = 0;
    for (; index < sizeof(code); index += 1) {
      smx_bus_write(&bus, 0x1000 + index, code[index]);
    }
  }
  expect_true("frame", smx_clock_frame(&clock));
  expect_true("second frame", smx_clock_frame(&clock));
  expect_u8("left the wait", (Uint8)cpu.a, 0x55);
  smx_bus_quit(&bus);
}

static void test_nmi(void) {
  Uint8 image[0x8000];
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxClock clock;
  static const Uint8 reset[] = {0xA9, 0x80, 0x8D, 0x00, 0x42, 0x4C, 0x05, 0x80};
  static const Uint8 handler[] = {0xA9, 0x99, 0x8D, 0x10, 0x00, 0x40};

  SDL_memset(image, 0, sizeof(image));
  SDL_memcpy(image, reset, sizeof(reset));
  SDL_memcpy(image + 0x100, handler, sizeof(handler));
  image[0x7FFA] = 0x00;
  image[0x7FFB] = 0x81;
  image[0x7FFC] = 0x00;
  image[0x7FFD] = 0x80;

  smx_bus_init(&bus, image, sizeof(image), 0);
  smx_cpu_init(&cpu, &bus);
  smx_dma_init(&dma, &bus);
  smx_clock_init(&clock, &cpu, &dma);
  smx_cpu_reset(&cpu);
  expect_true("nmi frame", smx_clock_frame(&clock));
  expect_u8("nmi wrote", smx_bus_wram(&bus, 0x0010), 0x99);
  smx_bus_quit(&bus);
}

static void test_rom_reaches_dma(void) {
  const char *path = SDL_getenv("SMX_ROM");
  SmxCart cart;
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxClock clock;
  Uint32 guard = 0;

  if (path == NULL || path[0] == '\0') {
    path = "SuperMetroid.sfc";
  }
  smx_cart_init(&cart);
  if (!smx_cart_load_path(&cart, path)) {
    if (SDL_getenv("SMX_REQUIRE_ROM") != NULL) {
      expect_true("rom", false);
    }
    return;
  }
  smx_bus_init(&bus, cart.bytes, cart.size, smx_cart_sram_bytes(cart.sram_size_code));
  smx_cpu_init(&cpu, &bus);
  smx_dma_init(&dma, &bus);
  smx_clock_init(&clock, &cpu, &dma);
  smx_cpu_reset(&cpu);
  while (clock.dma_count == 0 && guard < 8000000u) {
    if (!smx_clock_step(&clock)) {
      expect_true("cpu kept running", false);
      break;
    }
    guard += 1;
  }
  expect_true("reached STA $420B", clock.dma_count > 0);
  if (g_failed) {
    SDL_Log("clock rom pc=%u:%u steps=%u dma=%u p=%u", (unsigned)cpu.pb, (unsigned)cpu.pc,
            (unsigned)guard, (unsigned)clock.dma_count, (unsigned)cpu.p);
  }
  smx_bus_quit(&bus);
  smx_cart_free(&cart);
}

int main(void) {
  test_vblank_poll();
  test_nmi();
  test_rom_reaches_dma();
  return g_failed ? 1 : 0;
}
