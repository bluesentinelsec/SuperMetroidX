#include "smx/skip.h"

/* Super Metroid's RAM, in bank $7E. */
#define SMX_SKIP_GAME_STATE 0x0998u
#define SMX_SKIP_CONTROLS 0x09B2u
#define SMX_SKIP_MAX_MISSILES 0x09C8u
#define SMX_SKIP_MISSILES 0x09C6u
#define SMX_SKIP_HUD_ITEM 0x09D2u
#define SMX_SKIP_SAVED_CONTROLS 0x0D82u
/* The intro's scene: a routine in bank $8B the intro calls each frame. */
#define SMX_SKIP_SCENE 0x1F51u

/* `$0998` in the intro. */
#define SMX_SKIP_STATE_INTRO 0x1Eu

/*
 * Scenes in bank $8B. $A395 sets the intro up; it leaves no scene while it
 * waits for its music. $C0C5 fades out and clears the intro, then $C100
 * starts the game at Ceres.
 */
#define SMX_SKIP_SCENE_SETUP 0xA395u
#define SMX_SKIP_SCENE_END 0xC0C5u
#define SMX_SKIP_SCENE_START 0xC100u

/*
 * The flashback of Samus fighting Metroids gives her 900 missiles and demo
 * controls at $8B:A3B0, after saving the player's controls at $0D82, and
 * selects missiles in some of its scenes. It puts them back at $8B:B769
 * before the last scene, which saves the new game with what it finds.
 */
#define SMX_SKIP_FLASHBACK_MISSILES 900u
#define SMX_SKIP_CONTROL_WORDS 7u

static Uint16 smx_skip_get(const Uint8 *wram, Uint32 at) {
  return (Uint16)(wram[at] | ((Uint16)wram[at + 1u] << 8));
}

static void smx_skip_put(Uint8 *wram, Uint32 at, Uint16 value) {
  wram[at] = (Uint8)value;
  wram[at + 1u] = (Uint8)(value >> 8);
}

void smx_skip_init(SmxSkip *skip) {
  SDL_memset(skip, 0, sizeof(*skip));
}

/* The intro has started a scene, and is not already ending. */
static bool smx_skip_can(const Uint8 *wram) {
  Uint16 scene = smx_skip_get(wram, SMX_SKIP_SCENE);

  return scene != 0 && scene != SMX_SKIP_SCENE_SETUP && scene != SMX_SKIP_SCENE_END &&
         scene != SMX_SKIP_SCENE_START;
}

static void smx_skip_end(Uint8 *wram) {
  Uint32 word = 0;

  if (smx_skip_get(wram, SMX_SKIP_MAX_MISSILES) == SMX_SKIP_FLASHBACK_MISSILES) {
    for (; word < SMX_SKIP_CONTROL_WORDS; word += 1) {
      smx_skip_put(wram, SMX_SKIP_CONTROLS + word * 2u,
                   smx_skip_get(wram, SMX_SKIP_SAVED_CONTROLS + word * 2u));
    }
    smx_skip_put(wram, SMX_SKIP_MAX_MISSILES, 0);
    smx_skip_put(wram, SMX_SKIP_MISSILES, 0);
    smx_skip_put(wram, SMX_SKIP_HUD_ITEM, 0);
  }
  smx_skip_put(wram, SMX_SKIP_SCENE, SMX_SKIP_SCENE_END);
}

SmxSkipEvent smx_skip_apply(SmxSkip *skip, Uint8 *wram, Uint16 joy) {
  bool was = skip->in_intro;

  skip->in_intro = smx_skip_get(wram, SMX_SKIP_GAME_STATE) == SMX_SKIP_STATE_INTRO &&
                   smx_skip_get(wram, SMX_SKIP_SCENE) != SMX_SKIP_SCENE_END &&
                   smx_skip_get(wram, SMX_SKIP_SCENE) != SMX_SKIP_SCENE_START;
  if (!skip->in_intro || joy == 0) {
    skip->held = 0;
    return skip->in_intro && !was ? SMX_SKIP_INTRO : SMX_SKIP_NONE;
  }
  if (skip->held < SMX_SKIP_HOLD_FRAMES) {
    skip->held += 1;
  }
  if (skip->held < SMX_SKIP_HOLD_FRAMES || !smx_skip_can(wram)) {
    return was ? SMX_SKIP_NONE : SMX_SKIP_INTRO;
  }
  smx_skip_end(wram);
  skip->held = 0;
  skip->in_intro = false;
  return SMX_SKIP_SKIPPED;
}

Uint32 smx_skip_shown(const SmxSkip *skip) {
  return skip->held >= SMX_SKIP_SHOW_FRAMES ? skip->held : 0;
}
