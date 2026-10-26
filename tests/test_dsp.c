#include "smx/dsp.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("dsp: %s", name);
    g_failed = true;
  }
}

static void expect_u8(const char *name, Uint8 got, Uint8 want) {
  if (got != want) {
    SDL_Log("dsp: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

/* Voice 0 plays a looping block of constant +7 nibbles at shift 12 from $0200. */
static void setup(SmxDsp *dsp, Uint8 *ram, Uint8 header) {
  Uint32 i = 0;

  SDL_memset(ram, 0, 65536);
  smx_dsp_init(dsp, ram);
  ram[0x0100] = 0x00;
  ram[0x0101] = 0x02;
  ram[0x0102] = 0x00;
  ram[0x0103] = 0x02;
  ram[0x0200] = header;
  for (i = 1; i < 9; i += 1) {
    ram[0x0200 + i] = 0x77;
  }
  smx_dsp_write(dsp, 0x5D, 0x01);
  smx_dsp_write(dsp, 0x04, 0x00);
  smx_dsp_write(dsp, 0x02, 0x00);
  smx_dsp_write(dsp, 0x03, 0x10);
  smx_dsp_write(dsp, 0x05, 0x00);
  smx_dsp_write(dsp, 0x07, 0x7F);
  smx_dsp_write(dsp, 0x00, 0x7F);
  smx_dsp_write(dsp, 0x01, 0x7F);
  smx_dsp_write(dsp, 0x0C, 0x7F);
  smx_dsp_write(dsp, 0x1C, 0x7F);
  smx_dsp_write(dsp, 0x6C, 0x20);
}

static Uint32 run_samples(SmxDsp *dsp, Uint32 count, Sint16 *last_left) {
  Sint16 frames[2 * 64];
  Uint32 got = 0;
  Uint32 total = 0;

  smx_dsp_run(dsp, count * 32u);
  while ((got = smx_dsp_drain(dsp, frames, 64)) != 0) {
    total += got;
    *last_left = frames[(got - 1u) * 2u];
  }
  return total;
}

static void test_voice(void) {
  static Uint8 ram[65536];
  SmxDsp dsp;
  Sint16 left = 0;
  setup(&dsp, ram, 0xC3);
  smx_dsp_write(&dsp, 0x4C, 0x01);
  expect_true("one frame per sample", run_samples(&dsp, 4, &left) == 4);
  expect_true("key-on delay is silent", left == 0);
  run_samples(&dsp, 60, &left);
  /* 28672 at full direct GAIN, voice volume, and main volume. */
  expect_true("voice plays", left > 27000 && left < 29000);
  expect_u8("envx is the gain level", smx_dsp_read(&dsp, 0x08), 0x7F);
  expect_true("outx follows the voice", smx_dsp_read(&dsp, 0x09) > 0x60);
  expect_u8("looping block sets endx", (Uint8)(smx_dsp_read(&dsp, 0x7C) & 0x01), 0x01);
  smx_dsp_write(&dsp, 0x7C, 0x00);
  expect_u8("writing endx clears it", smx_dsp_read(&dsp, 0x7C), 0x00);

  smx_dsp_write(&dsp, 0x6C, 0x60);
  run_samples(&dsp, 2, &left);
  expect_true("mute", left == 0);
  smx_dsp_write(&dsp, 0x6C, 0x20);

  /* Release drops the envelope by 8 each sample. */
  smx_dsp_write(&dsp, 0x5C, 0x01);
  run_samples(&dsp, 64, &left);
  expect_true("release lowers envx",
              smx_dsp_read(&dsp, 0x08) < 0x7F && smx_dsp_read(&dsp, 0x08) > 0x40);
  run_samples(&dsp, 300, &left);
  expect_u8("release ends at zero", smx_dsp_read(&dsp, 0x08), 0x00);
}

static void test_end_block(void) {
  static Uint8 ram[65536];
  SmxDsp dsp;
  Sint16 left = 0;

  /* End without loop silences the voice as soon as the block is read. */
  setup(&dsp, ram, 0xC1);
  smx_dsp_write(&dsp, 0x4C, 0x01);
  run_samples(&dsp, 64, &left);
  expect_u8("end block sets endx", (Uint8)(smx_dsp_read(&dsp, 0x7C) & 0x01), 0x01);
  expect_u8("end block silences", smx_dsp_read(&dsp, 0x08), 0x00);
  expect_true("end block output", left == 0);
}

static void test_echo(void) {
  static Uint8 ram[65536];
  SmxDsp dsp;
  Sint16 left = 0;
  Uint32 i = 0;
  bool written = false;

  setup(&dsp, ram, 0xC3);
  smx_dsp_write(&dsp, 0x4D, 0x01);
  smx_dsp_write(&dsp, 0x6D, 0x40);
  smx_dsp_write(&dsp, 0x7D, 0x01);
  smx_dsp_write(&dsp, 0x6C, 0x00);
  smx_dsp_write(&dsp, 0x4C, 0x01);
  run_samples(&dsp, 200, &left);
  for (; i < 0x800; i += 1) {
    written = written || ram[0x4000 + i] != 0;
  }
  expect_true("echo writes into spc ram", written);
  expect_true("echo stays in its buffer", ram[0x4800] == 0 && ram[0x3FFF] == 0);
}

static void test_noise(void) {
  static Uint8 ram[65536];
  SmxDsp dsp;
  Uint16 noise = 0x4000;
  Uint32 i = 0;

  SDL_memset(ram, 0, sizeof(ram));
  smx_dsp_init(&dsp, ram);
  /* Rate 31 steps the noise LFSR every sample. */
  smx_dsp_write(&dsp, 0x6C, 0x3F);
  smx_dsp_run(&dsp, 32u * 10u);
  for (; i < 10u; i += 1) {
    Uint16 feedback = (Uint16)((noise << 13) ^ (noise << 14));
    noise = (Uint16)((feedback & 0x4000) ^ (noise >> 1));
  }
  expect_true("noise lfsr", dsp.noise == noise);
}

int main(void) {
  test_voice();
  test_end_block();
  test_echo();
  test_noise();
  return g_failed ? 1 : 0;
}
