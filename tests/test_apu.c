#include "smx/apu.h"
#include "smx/bus.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_u8(const char *name, Uint8 got, Uint8 want) {
  if (got != want) {
    SDL_Log("apu: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("apu: %s", name);
    g_failed = true;
  }
}

int main(void) {
  SmxBus bus;
  SmxApu apu;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_apu_init(&apu, &bus);
  smx_apu_write_port(&apu, 0, 0xAA);
  smx_apu_write_port(&apu, 1, 0xBB);
  expect_u8("port 0", smx_apu_read_port(&apu, 0), 0xAA);
  expect_u8("port 1", smx_apu_read_port(&apu, 1), 0xBB);
  expect_u8("mmio", smx_bus_mmio(&bus, 0x2140), 0xAA);
  expect_true("spc stopped", !smx_apu_core_running(&apu));
  smx_bus_quit(&bus);
  return g_failed ? 1 : 0;
}
