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

static void test_joy_busy(void) {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxClock clock;
  Uint32 guard = 0;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_cpu_init(&cpu, &bus);
  smx_dma_init(&dma, &bus);
  smx_clock_init(&clock, &cpu, &dma);
  cpu.e = false;
  cpu.p = 0x30;
  cpu.s = 0x01FF;
  cpu.pc = 0x1000;
  smx_bus_write(&bus, 0x1000, 0x80);
  smx_bus_write(&bus, 0x1001, 0xFE);
  while ((smx_bus_mmio(&bus, 0x4212) & 0x80) == 0 && guard < 40000u) {
    smx_clock_step(&clock);
    guard += 1;
  }
  expect_u8("joy busy", (Uint8)(smx_bus_mmio(&bus, 0x4212) & 0x01), 0x01);
  guard = 0;
  while (guard < 600u) {
    smx_clock_step(&clock);
    guard += 1;
  }
  expect_u8("joy done", (Uint8)(smx_bus_mmio(&bus, 0x4212) & 0x01), 0x00);
  expect_u8("still vblank", (Uint8)(smx_bus_mmio(&bus, 0x4212) & 0x80), 0x80);
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

static void test_irq_and_joy(void) {
  Uint8 image[0x8000];
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxClock clock;
  static const Uint8 reset[] = {0xA9, 0x21, 0x8D, 0x00, 0x42, 0xA9, 0x01,
                                0x8D, 0x09, 0x42, 0x58, 0x80, 0xFE};
  static const Uint8 handler[] = {0xA9, 0x77, 0x8D, 0x10, 0x00, 0x40};

  SDL_memset(image, 0, sizeof(image));
  SDL_memcpy(image, reset, sizeof(reset));
  SDL_memcpy(image + 0x100, handler, sizeof(handler));
  image[0x7FFE] = 0x00;
  image[0x7FFF] = 0x81;
  image[0x7FFC] = 0x00;
  image[0x7FFD] = 0x80;

  smx_bus_init(&bus, image, sizeof(image), 0);
  smx_cpu_init(&cpu, &bus);
  smx_dma_init(&dma, &bus);
  smx_clock_init(&clock, &cpu, &dma);
  clock.joy = 0x0010;
  smx_cpu_reset(&cpu);
  expect_true("irq frame", smx_clock_frame(&clock));
  expect_u8("irq wrote", smx_bus_wram(&bus, 0x0010), 0x77);
  expect_u8("joy start", smx_bus_read(&bus, 0x004218), 0x10);
  smx_bus_quit(&bus);
}

static void test_line_before_hdma(void) {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu ppu;
  SmxClock clock;
  static const Uint8 loop[] = {0x80, 0xFE};
  Uint32 row0 = 0;
  Uint32 row1 = 0;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_cpu_init(&cpu, &bus);
  smx_dma_init(&dma, &bus);
  smx_ppu_init(&ppu, &bus);
  smx_clock_init(&clock, &cpu, &dma);
  clock.ppu = &ppu;
  cpu.e = false;
  cpu.p = 0x30;
  cpu.s = 0x01FF;
  cpu.pc = 0x1000;
  smx_bus_write(&bus, 0x1000, loop[0]);
  smx_bus_write(&bus, 0x1001, loop[1]);
  smx_ppu_write_cgram(&ppu, 0, 0x001F);
  smx_bus_write(&bus, 0x7E0000, 0x01);
  smx_bus_write(&bus, 0x7E0001, 0x0F);
  smx_bus_write(&bus, 0x7E0002, 0x01);
  smx_bus_write(&bus, 0x7E0003, 0x8F);
  smx_bus_write(&bus, 0x7E0004, 0x00);
  smx_bus_write(&bus, 0x4300, 0x00);
  smx_bus_write(&bus, 0x4301, 0x00);
  smx_bus_write(&bus, 0x4302, 0x00);
  smx_bus_write(&bus, 0x4303, 0x00);
  smx_bus_write(&bus, 0x4304, 0x7E);
  smx_bus_write(&bus, 0x420C, 0x01);
  expect_true("scanline frame", smx_clock_frame(&clock));
  row0 = ppu.pixels[2];
  row1 = ppu.pixels[SMX_PPU_WIDTH * 4u + 2u];
  expect_true("line 0 kept its brightness", row0 == 255);
  expect_true("line 1 blanked by the next HDMA", row1 == 0);
  smx_ppu_quit(&ppu);
  smx_bus_quit(&bus);
}

static void test_h_irq(void) {
  Uint8 image[0x8000];
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxClock clock;
  static const Uint8 reset[] = {0xA9, 0x10, 0x8D, 0x00, 0x42, 0xA9, 0x08,
                                0x8D, 0x07, 0x42, 0x58, 0x80, 0xFE};
  static const Uint8 handler[] = {0xA9, 0x66, 0x8D, 0x10, 0x00, 0x40};
  Uint32 guard = 0;

  SDL_memset(image, 0, sizeof(image));
  SDL_memcpy(image, reset, sizeof(reset));
  SDL_memcpy(image + 0x100, handler, sizeof(handler));
  image[0x7FFE] = 0x00;
  image[0x7FFF] = 0x81;
  image[0x7FFC] = 0x00;
  image[0x7FFD] = 0x80;

  smx_bus_init(&bus, image, sizeof(image), 0);
  smx_cpu_init(&cpu, &bus);
  smx_dma_init(&dma, &bus);
  smx_clock_init(&clock, &cpu, &dma);
  smx_cpu_reset(&cpu);
  while (smx_bus_wram(&bus, 0x0010) != 0x66 && guard < 256u) {
    smx_clock_step(&clock);
    guard += 1;
  }
  expect_u8("h-irq wrote", smx_bus_wram(&bus, 0x0010), 0x66);
  smx_bus_quit(&bus);
}

int main(void) {
  test_vblank_poll();
  test_joy_busy();
  test_nmi();
  test_irq_and_joy();
  test_h_irq();
  test_line_before_hdma();
  test_rom_reaches_dma();
  return g_failed ? 1 : 0;
}
