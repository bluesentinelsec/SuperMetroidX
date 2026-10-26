#include "smx/sram.h"

void smx_sram_file_name(char *out, Uint32 size, const char *rom_path) {
  const char *name = rom_path;
  const char *at = rom_path;
  Uint32 length = 0;

  for (; *at != '\0'; at += 1) {
    if (*at == '/' || *at == '\\') {
      name = at + 1;
    }
  }
  length = (Uint32)SDL_strlen(name);
  for (at = name + length; at > name; at -= 1) {
    if (at[-1] == '.') {
      length = (Uint32)(at - 1 - name);
      break;
    }
  }
  SDL_snprintf(out, size, "%.*s.srm", (int)length, name);
}

bool smx_sram_default_path(char *path, Uint32 size, const char *rom_path) {
  const char *override = SDL_getenv("SMX_DATA_DIR");
  char *dir = NULL;
  char name[256];

  smx_sram_file_name(name, sizeof(name), rom_path);
  if (override != NULL && override[0] != '\0') {
    SDL_CreateDirectory(override);
    SDL_snprintf(path, size, "%s/%s", override, name);
    return true;
  }
  dir = SDL_GetPrefPath("SuperMetroidX", "SuperMetroidX");
  if (dir == NULL) {
    return false;
  }
  SDL_snprintf(path, size, "%s%s", dir, name);
  SDL_free(dir);
  return true;
}

bool smx_sram_open(SmxSram *sram, const char *path, SmxBus *bus) {
  size_t got = 0;
  Uint8 *file = NULL;

  SDL_memset(sram, 0, sizeof(*sram));
  if (bus->sram == NULL || bus->sram_size == 0) {
    return false;
  }
  SDL_strlcpy(sram->path, path, sizeof(sram->path));
  sram->size = bus->sram_size;
  sram->saved = (Uint8 *)SDL_calloc(1, sram->size);
  if (sram->saved == NULL) {
    return false;
  }
  file = (Uint8 *)SDL_LoadFile(path, &got);
  if (file != NULL) {
    /* A file of another size is from another cartridge; copy what overlaps. */
    Uint32 count = got < (size_t)sram->size ? (Uint32)got : sram->size;
    SDL_memcpy(bus->sram, file, count);
    SDL_memcpy(sram->saved, file, count);
    if (got != (size_t)sram->size) {
      /* Differ from SRAM so the next flush rewrites the file at its size. */
      sram->saved[0] = (Uint8)~bus->sram[0];
    }
    SDL_free(file);
  }
  return true;
}

bool smx_sram_flush(SmxSram *sram, const SmxBus *bus) {
  char temp[sizeof(sram->path) + 4];

  if (sram->saved == NULL || bus->sram == NULL) {
    return false;
  }
  if (SDL_memcmp(sram->saved, bus->sram, sram->size) == 0) {
    return true;
  }
  SDL_snprintf(temp, sizeof(temp), "%s.tmp", sram->path);
  if (!SDL_SaveFile(temp, bus->sram, sram->size) || !SDL_RenamePath(temp, sram->path)) {
    SDL_Log("sram: cannot save %s: %s", sram->path, SDL_GetError());
    return false;
  }
  SDL_memcpy(sram->saved, bus->sram, sram->size);
  return true;
}

void smx_sram_close(SmxSram *sram) {
  SDL_free(sram->saved);
  SDL_memset(sram, 0, sizeof(*sram));
}
