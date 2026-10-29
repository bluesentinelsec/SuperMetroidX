#include "smx/icon.h"

#include <SDL3/SDL.h>

/*
 * smxicon writes the game's icon for packaging:
 *
 *   smxicon png SIZE OUT.png   one PNG, for Linux's icon theme
 *   smxicon ico OUT.ico        Windows: PNG images from 16 to 256 pixels
 *   smxicon icns OUT.icns      macOS: images from 16 to 1024 pixels, laid out
 *                              as Apple's iconutil writes them
 */

typedef struct SmxIconImage {
  Uint8 *data;
  Uint32 length;
} SmxIconImage;

static const Uint32 smx_icon_ico_sizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};

/*
 * The icns images and their pixel sizes. macOS reads 16 and 32 pixel icons
 * from ic04 and ic05 as run-length coded ARGB; every other type holds a PNG.
 */
static const struct {
  char type[5];
  Uint32 size;
  bool argb;
} smx_icon_icns_types[] = {
    {"ic04", 16, true},   {"ic05", 32, true},   {"ic11", 32, false},   {"ic12", 64, false},
    {"ic07", 128, false}, {"ic13", 256, false}, {"ic08", 256, false},  {"ic14", 512, false},
    {"ic09", 512, false}, {"ic10", 1024, false},
};

static bool smx_icon_encode(Uint32 size, SmxIconStyle style, SmxIconImage *image) {
  SDL_Surface *surface = SDL_CreateSurface((int)size, (int)size, SDL_PIXELFORMAT_RGBA32);
  Uint8 *rgba = (Uint8 *)SDL_malloc((size_t)size * size * 4u);
  SDL_IOStream *io = SDL_IOFromDynamicMem();
  bool ok = surface != NULL && rgba != NULL && io != NULL;
  Uint32 row = 0;

  image->data = NULL;
  image->length = 0;
  if (ok) {
    smx_icon_draw(size, style, rgba);
    for (; row < size; row += 1) {
      SDL_memcpy((Uint8 *)surface->pixels + (size_t)row * (size_t)surface->pitch,
                 rgba + (size_t)row * size * 4u, (size_t)size * 4u);
    }
    ok = SDL_SavePNG_IO(surface, io, false);
  }
  if (ok) {
    Sint64 length = SDL_TellIO(io);
    void *bytes = SDL_GetPointerProperty(SDL_GetIOProperties(io),
                                         SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
    image->data = (Uint8 *)SDL_malloc((size_t)length);
    ok = length > 0 && bytes != NULL && image->data != NULL;
    if (ok) {
      SDL_memcpy(image->data, bytes, (size_t)length);
      image->length = (Uint32)length;
    }
  }
  if (io != NULL) {
    SDL_CloseIO(io);
  }
  SDL_free(rgba);
  SDL_DestroySurface(surface);
  if (!ok) {
    SDL_Log("smxicon: cannot encode a %u pixel icon: %s", (unsigned)size, SDL_GetError());
  }
  return ok;
}

/*
 * "ARGB", then the alpha, red, green, and blue planes, each run-length coded:
 * a byte below 0x80 copies that many plus one bytes; 0x80 and above repeats
 * the next byte that many minus 0x7D times.
 */
static bool smx_icon_argb(Uint32 size, SmxIconImage *image) {
  const Uint32 pixels = size * size;
  Uint8 *rgba = (Uint8 *)SDL_malloc((size_t)pixels * 4u);
  /* The worst case adds one byte per 128 literals. */
  Uint8 *out = (Uint8 *)SDL_malloc(4u + (size_t)pixels * 4u + (size_t)pixels / 32u + 4u);
  static const Uint32 planes[4] = {3, 0, 1, 2};
  Uint32 length = 4;
  Uint32 plane = 0;

  image->data = NULL;
  image->length = 0;
  if (rgba == NULL || out == NULL) {
    SDL_free(rgba);
    SDL_free(out);
    return false;
  }
  smx_icon_draw(size, SMX_ICON_MACOS, rgba);
  SDL_memcpy(out, "ARGB", 4);
  for (; plane < 4u; plane += 1) {
    const Uint8 *in = rgba + planes[plane];
    Uint32 at = 0;
    while (at < pixels) {
      Uint32 run = 1;
      while (at + run < pixels && run < 130u && in[(at + run) * 4u] == in[at * 4u]) {
        run += 1;
      }
      if (run >= 3u) {
        out[length] = (Uint8)(0x80u + run - 3u);
        out[length + 1u] = in[at * 4u];
        length += 2u;
        at += run;
      } else {
        Uint32 start = at;
        Uint32 count = 0;
        Uint32 index = 0;
        /* Literals until a run of three or 128 bytes. */
        while (at < pixels && count < 128u &&
               !(at + 2u < pixels && in[at * 4u] == in[(at + 1u) * 4u] &&
                 in[at * 4u] == in[(at + 2u) * 4u])) {
          at += 1;
          count += 1;
        }
        out[length] = (Uint8)(count - 1u);
        length += 1u;
        for (; index < count; index += 1) {
          out[length + index] = in[(start + index) * 4u];
        }
        length += count;
      }
    }
  }
  SDL_free(rgba);
  image->data = out;
  image->length = length;
  return true;
}

static bool smx_icon_write(const char *path, const Uint8 *bytes, Uint32 length) {
  if (!SDL_SaveFile(path, bytes, length)) {
    SDL_Log("smxicon: cannot write %s: %s", path, SDL_GetError());
    return false;
  }
  return true;
}

static void smx_icon_le16(Uint8 *out, Uint32 value) {
  out[0] = (Uint8)value;
  out[1] = (Uint8)(value >> 8);
}

static void smx_icon_le32(Uint8 *out, Uint32 value) {
  smx_icon_le16(out, value);
  smx_icon_le16(out + 2, value >> 16);
}

static void smx_icon_be32(Uint8 *out, Uint32 value) {
  out[0] = (Uint8)(value >> 24);
  out[1] = (Uint8)(value >> 16);
  out[2] = (Uint8)(value >> 8);
  out[3] = (Uint8)value;
}

/* An ICONDIR, one ICONDIRENTRY per image, then the images, each a PNG. */
static bool smx_icon_ico(const char *path) {
  const Uint32 count = SDL_arraysize(smx_icon_ico_sizes);
  SmxIconImage images[SDL_arraysize(smx_icon_ico_sizes)];
  Uint32 length = 6u + 16u * count;
  Uint32 offset = length;
  Uint32 index = 0;
  Uint8 *file = NULL;
  bool ok = true;

  for (; index < count; index += 1) {
    ok = smx_icon_encode(smx_icon_ico_sizes[index], SMX_ICON_FULL, &images[index]) && ok;
    length += images[index].length;
  }
  file = ok ? (Uint8 *)SDL_calloc(1, length) : NULL;
  if (file != NULL) {
    smx_icon_le16(file + 2, 1);
    smx_icon_le16(file + 4, count);
    for (index = 0; index < count; index += 1) {
      Uint8 *entry = file + 6u + 16u * index;
      Uint32 size = smx_icon_ico_sizes[index];
      entry[0] = (Uint8)(size >= 256u ? 0u : size);
      entry[1] = entry[0];
      smx_icon_le16(entry + 4, 1);
      smx_icon_le16(entry + 6, 32);
      smx_icon_le32(entry + 8, images[index].length);
      smx_icon_le32(entry + 12, offset);
      SDL_memcpy(file + offset, images[index].data, images[index].length);
      offset += images[index].length;
    }
    ok = smx_icon_write(path, file, length);
  }
  for (index = 0; index < count; index += 1) {
    SDL_free(images[index].data);
  }
  SDL_free(file);
  return ok && file != NULL;
}

/* "icns" and the file's length, then per image its type, its length with this header, and its data. */
static bool smx_icon_icns(const char *path) {
  const Uint32 count = SDL_arraysize(smx_icon_icns_types);
  SmxIconImage images[SDL_arraysize(smx_icon_icns_types)];
  Uint32 length = 8u;
  Uint32 offset = 8u;
  Uint32 index = 0;
  Uint8 *file = NULL;
  bool ok = true;

  for (; index < count; index += 1) {
    Uint32 size = smx_icon_icns_types[index].size;
    ok = (smx_icon_icns_types[index].argb ? smx_icon_argb(size, &images[index])
                                          : smx_icon_encode(size, SMX_ICON_MACOS, &images[index])) &&
         ok;
    length += 8u + images[index].length;
  }
  file = ok ? (Uint8 *)SDL_malloc(length) : NULL;
  if (file != NULL) {
    SDL_memcpy(file, "icns", 4);
    smx_icon_be32(file + 4, length);
    for (index = 0; index < count; index += 1) {
      SDL_memcpy(file + offset, smx_icon_icns_types[index].type, 4);
      smx_icon_be32(file + offset + 4u, 8u + images[index].length);
      SDL_memcpy(file + offset + 8u, images[index].data, images[index].length);
      offset += 8u + images[index].length;
    }
    ok = smx_icon_write(path, file, length);
  }
  for (index = 0; index < count; index += 1) {
    SDL_free(images[index].data);
  }
  SDL_free(file);
  return ok && file != NULL;
}

int main(int argc, char **argv) {
  if (argc == 4 && SDL_strcmp(argv[1], "png") == 0) {
    Sint32 size = SDL_atoi(argv[2]);
    SmxIconImage image;
    bool ok = false;
    if (size < 16 || size > 1024) {
      SDL_Log("smxicon: a PNG is 16 to 1024 pixels");
      return 1;
    }
    ok = smx_icon_encode((Uint32)size, SMX_ICON_FULL, &image) &&
         smx_icon_write(argv[3], image.data, image.length);
    SDL_free(image.data);
    return ok ? 0 : 1;
  }
  if (argc == 3 && SDL_strcmp(argv[1], "ico") == 0) {
    return smx_icon_ico(argv[2]) ? 0 : 1;
  }
  if (argc == 3 && SDL_strcmp(argv[1], "icns") == 0) {
    return smx_icon_icns(argv[2]) ? 0 : 1;
  }
  SDL_Log("usage: smxicon png SIZE OUT.png | smxicon ico OUT.ico | smxicon icns OUT.icns");
  return 1;
}
