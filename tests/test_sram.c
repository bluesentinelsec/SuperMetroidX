#include "smx/bus.h"
#include "smx/sram.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("sram: %s", name);
    g_failed = true;
  }
}

int main(void) {
  static const char path[] = "smx_test_sram.srm";
  char name[1024];
  SmxBus bus;
  SmxSram sram;
  size_t got = 0;
  Uint8 *file = NULL;

  SDL_RemovePath(path);
  /* The default path is not exercised: it would create the user-data directory. */
  smx_sram_file_name(name, sizeof(name), "roms/Super.Metroid.sfc");
  expect_true("name drops the directory and extension", SDL_strcmp(name, "Super.Metroid.srm") == 0);
  smx_sram_file_name(name, sizeof(name), "dir.v2\\\\SuperMetroid");
  expect_true("name without extension", SDL_strcmp(name, "SuperMetroid.srm") == 0);

  smx_bus_init(&bus, NULL, 0, 0x2000);
  expect_true("open missing", smx_sram_open(&sram, path, &bus));
  expect_true("missing starts zeroed", bus.sram[0] == 0 && bus.sram[0x1FFF] == 0);
  expect_true("clean flush", smx_sram_flush(&sram, &bus));
  expect_true("clean flush writes nothing", !SDL_GetPathInfo(path, NULL));
  bus.sram[0x10] = 0x5A;
  bus.sram[0x1FFF] = 0xA5;
  expect_true("dirty flush", smx_sram_flush(&sram, &bus));
  file = (Uint8 *)SDL_LoadFile(path, &got);
  expect_true("file written", file != NULL && got == 0x2000 && file[0x10] == 0x5A);
  SDL_free(file);
  smx_sram_close(&sram);
  smx_bus_quit(&bus);

  smx_bus_init(&bus, NULL, 0, 0x2000);
  expect_true("reopen", smx_sram_open(&sram, path, &bus));
  expect_true("reloaded", bus.sram[0x10] == 0x5A && bus.sram[0x1FFF] == 0xA5);
  SDL_RemovePath(path);
  expect_true("unchanged flush", smx_sram_flush(&sram, &bus));
  expect_true("unchanged flush writes nothing", !SDL_GetPathInfo(path, NULL));
  smx_sram_close(&sram);
  smx_bus_quit(&bus);

  smx_bus_init(&bus, NULL, 0, 0);
  expect_true("no sram", !smx_sram_open(&sram, path, &bus));
  smx_bus_quit(&bus);
  return g_failed ? 1 : 0;
}
