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

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("dma: %s", name);
    g_failed = true;
  }
}

static void write_reg(SmxBus *bus, Uint16 addr, Uint8 value) {
  smx_bus_write(bus, 0x000000u | addr, value);
}

int main(void) {
  SmxBus bus;
  SmxDma dma;
  Uint8 index = 0;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_dma_init(&dma, &bus);

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
  write_reg(&bus, 0x420C, 0xFF);
  smx_dma_run(&dma, 0x01);

  expect_true("ran", dma.ran);
  expect_u8("count", (Uint8)dma.log_count, 4);
  for (; index < 4; index += 1) {
    static const Uint8 want[4] = {0x11, 0x22, 0x33, 0x44};
    expect_u8("byte", dma.log[index], want[index]);
  }
  expect_u8("vram low latch", smx_bus_mmio(&bus, 0x2118), 0x44);
  /* HDMA enable is stored and does not move bytes by itself. */
  dma.log_count = 0;
  smx_dma_run(&dma, 0x00);
  expect_u8("hdma idle", (Uint8)dma.log_count, 0);
  expect_u8("hdma latch", smx_bus_mmio(&bus, 0x420C), 0xFF);

  smx_bus_quit(&bus);
  return g_failed ? 1 : 0;
}
