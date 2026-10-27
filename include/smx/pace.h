#ifndef SMX_PACE_H_
#define SMX_PACE_H_

#include <SDL3/SDL.h>

/** Presents kept to measure the display's refresh: about 2 s at 120 Hz. */
#define SMX_PACE_PRESENTS 256u

/**
 * Frame pacing for a display whose refresh is not the SNES's 60.0988 Hz.
 *
 * The shell presents once per refresh and asks how many game frames to run
 * first. When the refresh is within 1.5% of a whole multiple k of the game's
 * rate (60, 59.94, 120, or 240 Hz), the pace locks: one game frame every k
 * refreshes, so every frame shows for the same time, and the game runs at
 * refresh / k Hz. The audio is resampled to match. Otherwise game time
 * follows the display's time and frames show for k or k+1 refreshes. Without
 * vsync, the shell waits for each frame's time instead.
 *
 * Game frames are emulated exactly as before; only when they are shown
 * changes.
 */
typedef struct SmxPace {
  /** One game frame at the console's rate. */
  Uint64 frame_ns;
  /**
   * The display's refresh period. The display mode's, when presents over the
   * last SMX_PACE_PRESENTS agree with it within 2%; otherwise what they
   * measure, the time they span divided by the refreshes between them.
   */
  Uint64 period_ns;
  /** The display mode's refresh period, or 0 when it did not say. */
  Uint64 mode_ns;
  /** The audio queue, smoothed over about half a second of presents. */
  float queued;
  /** Refreshes per game frame when locked, else 0. */
  Uint32 lock;
  /** Presents wait for the display's refresh. */
  bool vsync;
  /** Game time per refresh: frame_ns / lock when locked, else period_ns. */
  Uint64 step_ns;
  /** Game time owed and not yet emulated. */
  Uint64 owed_ns;
  /** Present times, and the refreshes since the present before each. */
  Uint64 presents[SMX_PACE_PRESENTS];
  Uint32 refreshes[SMX_PACE_PRESENTS];
  Uint32 count;
  Uint32 next;
} SmxPace;

/** Start pacing `frame_ns` game frames on a display that reports `refresh_hz` (0 if unknown). */
void smx_pace_init(SmxPace *pace, Uint64 frame_ns, float refresh_hz);

/** A present finished at `now_ns`. Returns how many game frames to run before the next. */
Uint32 smx_pace_presented(SmxPace *pace, Uint64 now_ns);

/** Without vsync: nanoseconds to wait at `now_ns` before the next game frame is due. */
Uint64 smx_pace_wait_ns(const SmxPace *pace, Uint64 now_ns);

/** How far, from 0 to 1, the display is into the next game frame. */
float smx_pace_phase(const SmxPace *pace);

/** The display stopped (or resumed) waiting for refreshes, as when the window is hidden. */
void smx_pace_set_vsync(SmxPace *pace, bool vsync, Uint64 now_ns);

/**
 * The audio resampling ratio: the game's speed against the console, nudged by
 * at most 0.25% to keep `queued` sample frames near `target`. Call it once
 * per present; it smooths `queued`, which jumps as the device takes audio.
 */
float smx_pace_audio_ratio(SmxPace *pace, Uint32 queued, Uint32 target);

#endif /* SMX_PACE_H_ */
