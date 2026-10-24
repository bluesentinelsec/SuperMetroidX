#include "smx/bus.h"
#include "smx/cart.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_u8(const char *name, Uint8 got, Uint8 want) {
  if (got != want) {
    SDL_Log("bus: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void test_wram_sram_mmio(void) {
  SmxBus bus;

  smx_bus_init(&bus, NULL, 0, 8192);
  smx_bus_write(&bus, 0x7E1234, 0x5A);
  expect_u8("wram mirror", smx_bus_read(&bus, 0x001234), 0x5A);
  smx_bus_write(&bus, 0x7F0000, 0xA5);
  expect_u8("wram high bank", smx_bus_read(&bus, 0x7F0000), 0xA5);
  expect_u8("wram banks differ", smx_bus_read(&bus, 0x7E0000), 0x00);

  smx_bus_write(&bus, 0x700100, 0x3C);
  expect_u8("sram", smx_bus_read(&bus, 0x700100), 0x3C);
  expect_u8("sram mirror", smx_bus_read(&bus, 0xF00100), 0x3C);

  smx_bus_write(&bus, 0x00420D, 0x01);
  expect_u8("memsel latch", smx_bus_mmio(&bus, 0x420D), 0x01);
  expect_u8("memsel reads open bus", smx_bus_read(&bus, 0x00420D), 0x01);
  smx_bus_write(&bus, 0x7E0000, 0x11);
  expect_u8("inidisp latch", smx_bus_mmio(&bus, 0x2100), 0);
  smx_bus_write(&bus, 0x802100, 0x8F);
  expect_u8("inidisp latch after write", smx_bus_mmio(&bus, 0x2100), 0x8F);
  expect_u8("inidisp reads open bus", smx_bus_read(&bus, 0x002100), 0x8F);
  smx_bus_read(&bus, 0x7E0000);
  expect_u8("inidisp still open bus", smx_bus_read(&bus, 0x002100), 0x11);
  smx_bus_write(&bus, 0x004202, 0x03);
  smx_bus_write(&bus, 0x004203, 0x04);
  expect_u8("product low", smx_bus_read(&bus, 0x004216), 12);
  expect_u8("product high", smx_bus_read(&bus, 0x004217), 0);
  smx_bus_write(&bus, 0x004204, 0x0A);
  smx_bus_write(&bus, 0x004205, 0x00);
  smx_bus_write(&bus, 0x004206, 0x03);
  expect_u8("quotient", smx_bus_read(&bus, 0x004214), 3);
  expect_u8("remainder", smx_bus_read(&bus, 0x004216), 1);

  smx_bus_quit(&bus);
}

static void test_lorom_window(void) {
  const char *path = SDL_getenv("SMX_ROM");
  SmxCart cart;
  SmxBus bus;
  Uint8 low = 0;
  Uint8 high = 0;

  if (path == NULL || path[0] == '\0') {
    path = "SuperMetroid.sfc";
  }
  smx_cart_init(&cart);
  if (!smx_cart_load_path(&cart, path)) {
    if (SDL_getenv("SMX_REQUIRE_ROM") != NULL) {
      SDL_Log("bus: ROM missing");
      g_failed = true;
    }
    return;
  }

  smx_bus_init(&bus, cart.bytes, cart.size, smx_cart_sram_bytes(cart.sram_size_code));
  low = smx_bus_read(&bus, 0x008000 | cart.reset_vector);
  high = smx_bus_read(&bus, 0x800000 | cart.reset_vector);
  expect_u8("fastrom mirror", high, low);
  expect_u8("file window", low, cart.bytes[cart.reset_vector - 0x8000]);
  /* Bank $40 is the second megabyte, not a mirror of bank $00. */
  expect_u8("bank 40", smx_bus_read(&bus, 0x408000), cart.bytes[0x200000]);
  smx_bus_quit(&bus);
  smx_cart_free(&cart);
}

int main(void) {
  test_wram_sram_mmio();
  test_lorom_window();
  return g_failed ? 1 : 0;
}
