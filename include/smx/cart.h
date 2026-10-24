#ifndef SMX_CART_H_
#define SMX_CART_H_

#include <SDL3/SDL.h>

/** SNES internal header title, not including the trailing NUL. */
#define SMX_CART_TITLE_LEN 21

/**
 * A user-supplied cartridge image.
 * `bytes` is owned by the cart and freed by `smx_cart_free`.
 * The image is never written back to disk.
 */
typedef struct SmxCart {
  Uint8 *bytes;
  Uint32 size;
  char title[SMX_CART_TITLE_LEN + 1];
  Uint16 checksum;
  Uint16 complement;
  Uint16 reset_vector;
  Uint16 nmi_vector;
  Uint16 irq_vector;
  Uint8 map_mode;
  Uint8 rom_size_code;
  Uint8 sram_size_code;
  bool fast_rom;
  bool lorom;
  bool checksum_ok;
} SmxCart;

void smx_cart_init(SmxCart *cart);
void smx_cart_free(SmxCart *cart);

/** Load a headerless or 512-byte-header image from `path`. */
bool smx_cart_load_path(SmxCart *cart, const char *path);

/**
 * Copy `size` bytes and parse the internal header.
 * A 512-byte copier header is dropped when `size % 1024 == 512`.
 */
bool smx_cart_load_bytes(SmxCart *cart, const Uint8 *bytes, Uint32 size);

/**
 * 16-bit sum used by the cartridge header.
 * When `size` is not a power of two, the bytes above the previous power of
 * two are repeated until the image fills the next power of two.
 */
Uint16 smx_cart_checksum(const Uint8 *bytes, Uint32 size);

/** SRAM size in bytes. A size code of 0 means the cart has no SRAM. */
Uint32 smx_cart_sram_bytes(Uint8 sram_size_code);

#endif /* SMX_CART_H_ */
