#include "smx/bus.h"
#include "smx/dma.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_u8(const char *name, Uint8 got, Uint8 want) {
  if (got != want) {
    SDL_Log("dma: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_u32(const char *name, Uint32 got, Uint32 want) {
  if (got != want) {
    SDL_Log("dma: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("dma: %s", name);
    g_failed = true;
  }
}

static void write_reg(SmxBus *bus, Uint16 addr, Uint8 value) {
  smx_bus_write(bus, 0x000000u | addr, value);
}

/* A stand-in clock: time only passes when the controller spends it. */
static Uint64 g_now = 0;

static void fake_advance(void *user, Uint32 clocks) {
  (void)user;
  g_now += clocks;
}

static void test_general_purpose(void) {
  SmxBus bus;
  SmxDma dma;
  Uint8 index = 0;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_dma_init(&dma, &bus);
  dma.advance = fake_advance;
  dma.now = &g_now;
  smx_bus_write(&bus, 0x7E0100, 0x11);
  smx_bus_write(&bus, 0x7E0101, 0x22);
  smx_bus_write(&bus, 0x7E0102, 0x33);
  smx_bus_write(&bus, 0x7E0103, 0x44);
  write_reg(&bus, 0x4300, 0x00);
  write_reg(&bus, 0x4301, 0x18);
  write_reg(&bus, 0x4302, 0x00);
  write_reg(&bus, 0x4303, 0x01);
  write_reg(&bus, 0x4304, 0x7E);
  write_reg(&bus, 0x4305, 0x04);
  write_reg(&bus, 0x4306, 0x00);

  g_now = 0;
  smx_dma_request(&dma, 0x01);
  expect_true("waits a cycle", !smx_dma_service(&dma) && smx_dma_pending(&dma));
  expect_true("then runs", smx_dma_service(&dma));
  expect_true("done", !smx_dma_pending(&dma));
  /* 8 to reach a boundary, 8 to start, 8 for the channel, 8 per byte, 8 to end the cycle. */
  expect_u32("clocks", (Uint32)g_now, 64);
  expect_u8("count", (Uint8)dma.log_count, 4);
  for (; index < 4; index += 1) {
    static const Uint8 want[4] = {0x11, 0x22, 0x33, 0x44};
    expect_u8("byte", dma.log[index], want[index]);
  }
  expect_u8("vram low latch", smx_bus_mmio(&bus, 0x2118), 0x44);
  expect_u8("count left", smx_bus_read(&bus, 0x004305), 0);
  expect_u8("address moved", smx_bus_read(&bus, 0x004302), 0x04);

  /* A transfer starting 2 clocks past a boundary waits 6 to reach the next. */
  write_reg(&bus, 0x4305, 0x01);
  g_now = 2;
  smx_dma_request(&dma, 0x01);
  smx_dma_service(&dma);
  dma.speed = 6;
  smx_dma_service(&dma);
  /* 6 + 8 + 8 + 8 = 30 since the start; pad to a whole 6-clock cycle. */
  expect_u32("aligned clocks", (Uint32)(g_now - 2u), 30u + (6u - 30u % 6u));
  smx_bus_quit(&bus);
}

static void test_hdma(void) {
  SmxBus bus;
  SmxDma dma;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_dma_init(&dma, &bus);
  smx_bus_write(&bus, 0x7E0000, 0x82);
  smx_bus_write(&bus, 0x7E0001, 0x11);
  smx_bus_write(&bus, 0x7E0002, 0x22);
  smx_bus_write(&bus, 0x7E0003, 0x00);
  write_reg(&bus, 0x4300, 0x00);
  write_reg(&bus, 0x4301, 0x00);
  write_reg(&bus, 0x4302, 0x00);
  write_reg(&bus, 0x4303, 0x00);
  write_reg(&bus, 0x4304, 0x7E);
  write_reg(&bus, 0x420C, 0x01);

  smx_dma_request_hdma_init(&dma);
  smx_dma_service(&dma);
  expect_true("reload ran", smx_dma_service(&dma));
  expect_u8("line count loaded", smx_bus_read(&bus, 0x00430A), 0x82);
  expect_u8("table advanced", smx_bus_read(&bus, 0x004308), 0x01);

  smx_dma_request_hdma(&dma);
  smx_dma_service(&dma);
  smx_dma_service(&dma);
  expect_u8("hdma first", smx_bus_mmio(&bus, 0x2100), 0x11);
  smx_dma_request_hdma(&dma);
  smx_dma_service(&dma);
  smx_dma_service(&dma);
  expect_u8("hdma repeat", smx_bus_mmio(&bus, 0x2100), 0x22);
  smx_dma_request_hdma(&dma);
  smx_dma_service(&dma);
  smx_dma_service(&dma);
  expect_u8("hdma stop", smx_bus_mmio(&bus, 0x2100), 0x22);
  expect_true("channel finished", (dma.hdma_done & 0x01) != 0);

  /* With no channel enabled, a line asks for nothing. */
  write_reg(&bus, 0x420C, 0x00);
  smx_dma_request_hdma(&dma);
  expect_true("idle", !smx_dma_pending(&dma));
  smx_bus_quit(&bus);
}

int main(void) {
  test_general_purpose();
  test_hdma();
  return g_failed ? 1 : 0;
}
