#include "smx/cart.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("cart: %s", name);
    g_failed = true;
  }
}

static void expect_u16(const char *name, Uint16 got, Uint16 want) {
  if (got != want) {
    SDL_Log("cart: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_u32(const char *name, Uint32 got, Uint32 want) {
  if (got != want) {
    SDL_Log("cart: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

/** Build a tiny LoROM image whose header fields this test chooses. */
static void test_synthetic(void) {
  Uint8 image[0x8000];
  SmxCart cart;
  Uint16 sum = 0;

  SDL_memset(image, 0, sizeof(image));
  SDL_memset(image + 0x7FC0, ' ', SMX_CART_TITLE_LEN);
  SDL_memcpy(image + 0x7FC0, "TEST CART", 9);
  image[0x7FD5] = 0x20;
  image[0x7FD7] = 0x08;
  image[0x7FD8] = 0x03;
  image[0x7FFC] = 0x00;
  image[0x7FFD] = 0x80;
  /* Byte sum of a word and its ones-complement is 0x1FE. */
  sum = (Uint16)(smx_cart_checksum(image, sizeof(image)) + 0x1FE);
  image[0x7FDE] = (Uint8)sum;
  image[0x7FDF] = (Uint8)(sum >> 8);
  image[0x7FDC] = (Uint8)~image[0x7FDE];
  image[0x7FDD] = (Uint8)~image[0x7FDF];

  smx_cart_init(&cart);
  expect_true("synthetic loads", smx_cart_load_bytes(&cart, image, sizeof(image)));
  expect_true("synthetic lorom", cart.lorom);
  expect_true("synthetic fast", cart.fast_rom);
  expect_true("synthetic checksum", cart.checksum_ok);
  expect_true("synthetic title", SDL_strcmp(cart.title, "TEST CART            ") == 0);
  expect_u16("synthetic reset", cart.reset_vector, 0x8000);
  expect_u32("synthetic sram", smx_cart_sram_bytes(cart.sram_size_code), 8192);
  expect_true("reject short", !smx_cart_load_bytes(&cart, image, 100));
  smx_cart_free(&cart);
}

/**
 * Published identity of the user-supplied image.
 * Skips when the file is absent so CI does not need the ROM.
 */
static void test_user_rom(void) {
  const char *path = SDL_getenv("SMX_ROM");
  SmxCart cart;

  if (path == NULL || path[0] == '\0') {
    path = "SuperMetroid.sfc";
  }
  smx_cart_init(&cart);
  if (!smx_cart_load_path(&cart, path)) {
    if (SDL_getenv("SMX_REQUIRE_ROM") != NULL) {
      expect_true("user rom present", false);
    } else {
      SDL_Log("cart: no ROM at %s; skipping identity check", path);
    }
    smx_cart_free(&cart);
    return;
  }

  expect_u32("rom size", cart.size, 3145728);
  expect_true("title", SDL_strncmp(cart.title, "Super Metroid", 13) == 0);
  expect_true("lorom", cart.lorom);
  expect_true("fast rom", cart.fast_rom);
  expect_u16("map mode", cart.map_mode, 0x30);
  expect_u16("checksum", cart.checksum, 0xF8DF);
  expect_u16("complement", cart.complement, 0x0720);
  expect_true("checksum matches image", cart.checksum_ok);
  expect_u16("reset", cart.reset_vector, 0x841C);
  expect_u32("sram", smx_cart_sram_bytes(cart.sram_size_code), 8192);
  smx_cart_free(&cart);
}

int main(void) {
  test_synthetic();
  test_user_rom();
  return g_failed ? 1 : 0;
}
