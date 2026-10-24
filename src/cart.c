#include "smx/cart.h"

void smx_cart_init(SmxCart *cart) {
  SDL_memset(cart, 0, sizeof(*cart));
}

void smx_cart_free(SmxCart *cart) {
  SDL_free(cart->bytes);
  smx_cart_init(cart);
}

Uint32 smx_cart_sram_bytes(Uint8 sram_size_code) {
  if (sram_size_code == 0) {
    return 0;
  }
  return (Uint32)1024u << sram_size_code;
}

static bool smx_cart_power_of_two(Uint32 size) {
  return size != 0 && (size & (size - 1)) == 0;
}

Uint16 smx_cart_checksum(const Uint8 *bytes, Uint32 size) {
  Uint32 sum = 0;
  Uint32 index = 0;
  Uint32 filled = 1;
  Uint32 previous = 0;

  for (; index < size; index += 1) {
    sum += bytes[index];
  }
  if (size == 0 || smx_cart_power_of_two(size)) {
    return (Uint16)sum;
  }

  while (filled < size) {
    previous = filled;
    filled <<= 1;
  }
  index = size;
  while (index < filled) {
    sum += bytes[previous + ((index - previous) % (size - previous))];
    index += 1;
  }
  return (Uint16)sum;
}

static bool smx_cart_title_ok(const Uint8 *title) {
  Uint32 index = 0;
  bool any = false;

  for (; index < SMX_CART_TITLE_LEN; index += 1) {
    Uint8 ch = title[index];
    if (ch == 0) {
      break;
    }
    if (ch < 32 || ch > 126) {
      return false;
    }
    if (ch != ' ') {
      any = true;
    }
  }
  return any;
}

static bool smx_cart_parse_at(SmxCart *cart, Uint32 base, bool lorom) {
  const Uint8 *header = NULL;
  Uint32 index = 0;

  if (cart->size < base + 0x40) {
    return false;
  }
  header = cart->bytes + base;
  if (!smx_cart_title_ok(header)) {
    return false;
  }

  cart->complement = (Uint16)(header[0x1C] | ((Uint16)header[0x1D] << 8));
  cart->checksum = (Uint16)(header[0x1E] | ((Uint16)header[0x1F] << 8));
  if ((Uint16)(cart->complement + cart->checksum) != 0xFFFF) {
    return false;
  }

  for (; index < SMX_CART_TITLE_LEN; index += 1) {
    cart->title[index] = (char)header[index];
  }
  cart->title[SMX_CART_TITLE_LEN] = '\0';
  cart->map_mode = header[0x15];
  cart->rom_size_code = header[0x17];
  cart->sram_size_code = header[0x18];
  cart->fast_rom = (header[0x15] & 0x30) != 0;
  cart->lorom = lorom;
  cart->reset_vector = (Uint16)(header[0x3C] | ((Uint16)header[0x3D] << 8));
  cart->nmi_vector = (Uint16)(header[0x2A] | ((Uint16)header[0x2B] << 8));
  cart->irq_vector = (Uint16)(header[0x2E] | ((Uint16)header[0x2F] << 8));
  cart->checksum_ok = smx_cart_checksum(cart->bytes, cart->size) == cart->checksum;
  return true;
}

bool smx_cart_load_bytes(SmxCart *cart, const Uint8 *bytes, Uint32 size) {
  Uint32 image = size;
  const Uint8 *body = bytes;
  Uint8 *copy = NULL;

  smx_cart_free(cart);
  if (bytes == NULL || size < 0x8000) {
    return false;
  }
  if ((size % 1024u) == 512u) {
    body += 512;
    image -= 512;
  }

  copy = (Uint8 *)SDL_malloc(image);
  if (copy == NULL) {
    return false;
  }
  SDL_memcpy(copy, body, image);
  cart->bytes = copy;
  cart->size = image;

  if (smx_cart_parse_at(cart, 0x7FC0, true)) {
    return true;
  }
  if (smx_cart_parse_at(cart, 0xFFC0, false)) {
    return true;
  }
  smx_cart_free(cart);
  return false;
}

bool smx_cart_load_path(SmxCart *cart, const char *path) {
  SDL_IOStream *io = NULL;
  Sint64 length = 0;
  Uint8 *bytes = NULL;
  bool ok = false;

  smx_cart_free(cart);
  if (path == NULL) {
    return false;
  }
  io = SDL_IOFromFile(path, "rb");
  if (io == NULL) {
    return false;
  }
  length = SDL_GetIOSize(io);
  if (length < 0x8000 || length > 8 * 1024 * 1024) {
    SDL_CloseIO(io);
    return false;
  }
  bytes = (Uint8 *)SDL_malloc((Uint32)length);
  if (bytes == NULL) {
    SDL_CloseIO(io);
    return false;
  }
  if (SDL_ReadIO(io, bytes, (Uint32)length) != (Uint32)length) {
    SDL_free(bytes);
    SDL_CloseIO(io);
    return false;
  }
  SDL_CloseIO(io);
  ok = smx_cart_load_bytes(cart, bytes, (Uint32)length);
  SDL_free(bytes);
  return ok;
}
