#ifndef SMX_MOTION_H_
#define SMX_MOTION_H_

#include "smx/ppu.h"

#include <SDL3/SDL.h>

/** Registers `$2100`-`$2133` a scanline is drawn with. */
#define SMX_MOTION_REGS 0x34u
/** Game objects kept per frame for sprites whose tiles changed. */
#define SMX_MOTION_OBJECTS 64u

/** The PPU state one scanline was drawn with. */
typedef struct SmxMotionLine {
  Uint8 regs[SMX_MOTION_REGS];
  Uint16 bg_h[4];
  Uint16 bg_v[4];
  Uint16 m7[8];
  /** The fixed color, which `$2132` sets a channel at a time. */
  Uint8 fixed[3];
  Uint16 oam_base;
  bool oam_rotate;
  bool drawn;
  /** The CGRAM copy in effect, an index into `SmxMotionFrame.palettes`. */
  Uint8 palette;
} SmxMotionLine;

/** A game object's place on screen in one frame, from game RAM. */
typedef struct SmxMotionObject {
  /** Who it is, the same from frame to frame: 0 is Samus. */
  Uint16 id;
  Sint16 x;
  Sint16 y;
  /** Half its width and height. */
  Sint16 rx;
  Sint16 ry;
} SmxMotionObject;

/** Everything one picture was drawn from. */
typedef struct SmxMotionFrame {
  Uint8 vram[0x10000];
  Uint8 oam[544];
  /** CGRAM as the frame began, and again after each change within it. */
  Uint16 palettes[SMX_PPU_HEIGHT][256];
  Uint32 palette_count;
  Uint32 cgram_serial;
  SmxMotionLine lines[SMX_PPU_HEIGHT + 1];
  /** Lines 1-224 were all drawn. */
  bool whole;
  SmxMotionObject objects[SMX_MOTION_OBJECTS];
  Uint32 object_count;
  /** The game was in play, so `objects` hold. */
  bool playing;
} SmxMotionFrame;

/**
 * How far each row of an in-between picture is drawn past its pixel grid,
 * in picture pixels, for the presenter to shift by. The rows from `top` up
 * to but not including `bottom` move together; the rest, such as the HUD,
 * do not move.
 */
typedef struct SmxMotionShift {
  float x[SMX_PPU_HEIGHT];
  float y;
  Uint32 top;
  Uint32 bottom;
} SmxMotionShift;

/**
 * Motion interpolation: in-between pictures for displays faster than the
 * game, drawn from the last two pictures without touching the game.
 *
 * The PPU's hooks record what each picture was drawn from. An in-between
 * picture redraws the last one through the same renderer, on a spare PPU,
 * with each background's scroll and each sprite's place moved back toward
 * the picture before. Scroll moves by its change per line. A sprite moves by
 * the change of the same sprite, matched by its tiles in OAM order, or of the
 * game object it belongs to when its tiles changed. Layers are drawn at whole
 * pixels; the fraction of the world layer's scroll is left for the presenter,
 * so the camera moves smoothly at any scale.
 */
typedef struct SmxMotion {
  SmxMotionFrame *frames[3];
  /** The frame being recorded, the last whole picture, and the one before it. */
  Uint32 building;
  Uint32 last;
  Uint32 before;
  /** Whole pictures recorded since the motion was reset. */
  Uint32 count;
  SmxBus bus;
  SmxPpu ppu;
  SmxPpu *source;
  /** Scratch for matching sprites between pictures. */
  Uint8 runs[129][129];
} SmxMotion;

/** Record `ppu`'s pictures through its hooks. */
bool smx_motion_init(SmxMotion *motion, SmxPpu *ppu);
void smx_motion_quit(SmxMotion *motion);

/** Forget the pictures recorded, as after loading a state. */
void smx_motion_reset(SmxMotion *motion);

/**
 * Set the game objects of the last picture from Super Metroid's RAM, after
 * a frame is emulated. Outside play there are none.
 */
void smx_motion_objects(SmxMotion *motion, const Uint8 *wram);

/**
 * Draw the last picture moved back toward the one before by `1 - alpha`,
 * with alpha between 0 and 1. Returns the XRGB8888 picture and fills `shift`,
 * or returns NULL when the last picture should show as it is: alpha is 1,
 * or there are not two whole pictures to move between.
 */
const Uint8 *smx_motion_draw(SmxMotion *motion, float alpha, SmxMotionShift *shift);

/**
 * Redraw the last picture as it was, through the spare PPU. It matches the
 * picture except where a register changed partway through a line. For tests.
 */
const Uint8 *smx_motion_redraw(SmxMotion *motion);

#endif /* SMX_MOTION_H_ */
