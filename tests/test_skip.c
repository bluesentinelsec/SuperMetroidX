#include "smx/skip.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("skip: %s", name);
    g_failed = true;
  }
}

static Uint16 get16(const Uint8 *at, Uint32 offset) {
  return (Uint16)(at[offset] | ((Uint16)at[offset + 1u] << 8));
}

static void put16(Uint8 *at, Uint32 offset, Uint16 value) {
  at[offset] = (Uint8)value;
  at[offset + 1u] = (Uint8)(value >> 8);
}

/* WRAM in the intro's flashback: 900 missiles and demo controls, the player's saved at $0D82. */
static Uint8 *fake_wram(void) {
  Uint8 *wram = (Uint8 *)SDL_calloc(1, 0x20000);
  Uint32 word = 0;

  put16(wram, 0x0998, 0x1E);
  put16(wram, 0x1F51, 0xA391);
  put16(wram, 0x09C6, 900);
  put16(wram, 0x09C8, 900);
  put16(wram, 0x09D2, 1);
  for (; word < 7u; word += 1) {
    put16(wram, 0x09B2 + word * 2u, 0x1111);
    put16(wram, 0x0D82 + word * 2u, (Uint16)(0x0080u + word));
  }
  return wram;
}

/* Hold `joy` for `frames` frames; returns the last event. */
static SmxSkipEvent hold(SmxSkip *skip, Uint8 *wram, Uint16 joy, Uint32 frames) {
  SmxSkipEvent event = SMX_SKIP_NONE;
  Uint32 frame = 0;

  for (; frame < frames; frame += 1) {
    event = smx_skip_apply(skip, wram, joy);
  }
  return event;
}

static void test_hold(void) {
  SmxSkip skip;
  Uint8 *wram = fake_wram();

  smx_skip_init(&skip);
  expect("the intro is announced", smx_skip_apply(&skip, wram, 0) == SMX_SKIP_INTRO);
  expect("only once", smx_skip_apply(&skip, wram, 0) == SMX_SKIP_NONE);
  hold(&skip, wram, 0x0080, SMX_SKIP_SHOW_FRAMES - 1u);
  expect("a tap shows nothing", smx_skip_shown(&skip) == 0);
  hold(&skip, wram, 0x0080, 1);
  expect("a hold shows", smx_skip_shown(&skip) == SMX_SKIP_SHOW_FRAMES);
  hold(&skip, wram, 0, 1);
  expect("letting go starts over", smx_skip_shown(&skip) == 0 && skip.held == 0);
  expect("held not quite long enough",
         hold(&skip, wram, 0x1000, SMX_SKIP_HOLD_FRAMES - 1u) == SMX_SKIP_NONE &&
             get16(wram, 0x1F51) == 0xA391);
  expect("held long enough, it skips",
         hold(&skip, wram, 0x1000, 1) == SMX_SKIP_SKIPPED && get16(wram, 0x1F51) == 0xC0C5);
  expect("the flashback's missiles go",
         get16(wram, 0x09C6) == 0 && get16(wram, 0x09C8) == 0 && get16(wram, 0x09D2) == 0);
  expect("the player's controls come back",
         get16(wram, 0x09B2) == 0x0080 && get16(wram, 0x09BE) == 0x0086);
  expect("then nothing more is shown or done",
         hold(&skip, wram, 0x1000, SMX_SKIP_HOLD_FRAMES * 2u) == SMX_SKIP_NONE &&
             smx_skip_shown(&skip) == 0 && get16(wram, 0x1F51) == 0xC0C5);
  SDL_free(wram);
}

static void test_after_flashback(void) {
  SmxSkip skip;
  Uint8 *wram = fake_wram();

  /* The flashback has put the player's controls back and taken the missiles. */
  put16(wram, 0x09C6, 0);
  put16(wram, 0x09C8, 0);
  put16(wram, 0x09B2, 0x4000);
  smx_skip_init(&skip);
  expect("a later scene skips",
         hold(&skip, wram, 0x8000, SMX_SKIP_HOLD_FRAMES) == SMX_SKIP_SKIPPED);
  expect("and leaves the controls alone", get16(wram, 0x09B2) == 0x4000);
  SDL_free(wram);
}

static void test_not_yet(void) {
  SmxSkip skip;
  Uint8 *wram = fake_wram();

  smx_skip_init(&skip);
  put16(wram, 0x1F51, 0x0000);
  expect("not while the intro sets up",
         hold(&skip, wram, 0x0080, SMX_SKIP_HOLD_FRAMES * 2u) != SMX_SKIP_SKIPPED &&
             get16(wram, 0x1F51) == 0);
  put16(wram, 0x1F51, 0xA5A7);
  expect("but as soon as it starts", hold(&skip, wram, 0x0080, 1) == SMX_SKIP_SKIPPED);
  put16(wram, 0x0998, 0x08);
  put16(wram, 0x1F51, 0xA5A7);
  smx_skip_init(&skip);
  expect("never in play", hold(&skip, wram, 0x0080, SMX_SKIP_HOLD_FRAMES * 2u) == SMX_SKIP_NONE &&
                              smx_skip_shown(&skip) == 0 && get16(wram, 0x1F51) == 0xA5A7);
  SDL_free(wram);
}

int main(void) {
  test_hold();
  test_after_flashback();
  test_not_yet();
  return g_failed ? 1 : 0;
}
