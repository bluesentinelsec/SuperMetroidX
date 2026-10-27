#include "smx/pace.h"

/* Lock when k refreshes are within this share of a game frame (1.5%). */
#define SMX_PACE_LOCK_PER_MILLE 15u
/* Presents to see before trusting the measured period. */
#define SMX_PACE_SETTLE 64u
/* Recent intervals whose median counts the refreshes in an interval. */
#define SMX_PACE_MEDIAN 31u
/* A median interval below this means presents do not wait for the display. */
#define SMX_PACE_NO_VSYNC_NS 2000000u
/* Most game frames run for one present, after a stall. */
#define SMX_PACE_MOST_FRAMES 4u
/* Largest audio correction: 0.25%, about 4 cents. */
#define SMX_PACE_AUDIO_NUDGE 0.0025f
/* The share of each present's audio queue in its smoothed value. */
#define SMX_PACE_QUEUE_WEIGHT 0.02f
/* The display mode's period stands when measurement agrees within 2%. */
#define SMX_PACE_MODE_PER_MILLE 20u

static void smx_pace_lock(SmxPace *pace) {
  Uint64 k = (pace->frame_ns + pace->period_ns / 2u) / pace->period_ns;
  Uint64 span = k * pace->period_ns;
  Uint64 error = span > pace->frame_ns ? span - pace->frame_ns : pace->frame_ns - span;

  if (k >= 1u && error * 1000u <= pace->frame_ns * SMX_PACE_LOCK_PER_MILLE) {
    pace->lock = (Uint32)k;
    pace->step_ns = pace->frame_ns / k;
  } else {
    pace->lock = 0;
    pace->step_ns = pace->period_ns;
  }
}

void smx_pace_init(SmxPace *pace, Uint64 frame_ns, float refresh_hz) {
  SDL_memset(pace, 0, sizeof(*pace));
  pace->frame_ns = frame_ns;
  pace->mode_ns = refresh_hz > 1.0f ? (Uint64)(1e9 / (double)refresh_hz) : 0;
  pace->period_ns = pace->mode_ns != 0 ? pace->mode_ns : frame_ns;
  pace->queued = -1.0f;
  pace->vsync = true;
  smx_pace_lock(pace);
}

/* The present `back` presents before the newest. */
static Uint32 smx_pace_slot(const SmxPace *pace, Uint32 back) {
  return (pace->next + SMX_PACE_PRESENTS - 1u - back) % SMX_PACE_PRESENTS;
}

/* The median of the last SMX_PACE_MEDIAN intervals. */
static Uint64 smx_pace_median(const SmxPace *pace) {
  Uint64 sorted[SMX_PACE_MEDIAN];
  Uint32 count = 0;
  Uint32 index = 0;

  for (; count < SMX_PACE_MEDIAN && count + 1u < pace->count; count += 1) {
    Uint64 interval = pace->presents[smx_pace_slot(pace, count)] -
                      pace->presents[smx_pace_slot(pace, count + 1u)];
    for (index = count; index > 0 && sorted[index - 1u] > interval; index -= 1) {
      sorted[index] = sorted[index - 1u];
    }
    sorted[index] = interval;
  }
  return count == 0 ? pace->period_ns : sorted[count / 2u];
}

/* Refine the period from the presents kept, once there are enough. */
static void smx_pace_measure(SmxPace *pace) {
  Uint64 median = smx_pace_median(pace);
  Uint64 span = 0;
  Uint64 refreshes = 0;
  Uint32 back = 0;

  if (pace->count < SMX_PACE_SETTLE) {
    return;
  }
  if (median < SMX_PACE_NO_VSYNC_NS) {
    pace->vsync = false;
    smx_pace_lock(pace);
    return;
  }
  for (; back + 1u < pace->count; back += 1) {
    refreshes += pace->refreshes[smx_pace_slot(pace, back)];
  }
  span = pace->presents[smx_pace_slot(pace, 0)] - pace->presents[smx_pace_slot(pace, back)];
  if (refreshes != 0) {
    Uint64 measured = span / refreshes;
    Uint64 off = measured > pace->mode_ns ? measured - pace->mode_ns : pace->mode_ns - measured;
    bool mode = pace->mode_ns != 0 && off * 1000u <= pace->mode_ns * SMX_PACE_MODE_PER_MILLE;
    pace->period_ns = mode ? pace->mode_ns : measured;
    smx_pace_lock(pace);
  }
}

Uint32 smx_pace_presented(SmxPace *pace, Uint64 now_ns) {
  Uint64 last = pace->count == 0 ? 0 : pace->presents[smx_pace_slot(pace, 0)];
  Uint64 elapsed = now_ns - last;
  Uint64 refreshes = 1;
  Uint32 frames = 0;

  if (pace->count == 0) {
    pace->presents[0] = now_ns;
    pace->refreshes[0] = 1;
    pace->next = 1;
    pace->count = 1;
    return 1;
  }
  if (pace->vsync) {
    /* Count refreshes against the median, so jitter in when a present returns is ignored. */
    Uint64 median = smx_pace_median(pace);
    if (median < SMX_PACE_NO_VSYNC_NS) {
      median = pace->period_ns;
    }
    refreshes = (elapsed + median / 2u) / median;
    if (refreshes < 1u) {
      refreshes = 1;
    }
    if (refreshes > SMX_PACE_MOST_FRAMES * 4u) {
      refreshes = SMX_PACE_MOST_FRAMES * 4u;
    }
    pace->owed_ns += refreshes * pace->step_ns;
  } else {
    pace->owed_ns += elapsed;
  }
  pace->presents[pace->next] = now_ns;
  pace->refreshes[pace->next] = (Uint32)refreshes;
  pace->next = (pace->next + 1u) % SMX_PACE_PRESENTS;
  if (pace->count < SMX_PACE_PRESENTS) {
    pace->count += 1;
  }
  if (pace->vsync) {
    smx_pace_measure(pace);
  }

  frames = (Uint32)(pace->owed_ns / pace->frame_ns);
  pace->owed_ns -= (Uint64)frames * pace->frame_ns;
  if (frames > SMX_PACE_MOST_FRAMES) {
    /* After a stall, run a few frames and let the rest of the time go. */
    frames = SMX_PACE_MOST_FRAMES;
    pace->owed_ns = 0;
  }
  return frames;
}

Uint64 smx_pace_wait_ns(const SmxPace *pace, Uint64 now_ns) {
  Uint64 last = pace->count == 0 ? now_ns : pace->presents[smx_pace_slot(pace, 0)];
  Uint64 due = pace->frame_ns > pace->owed_ns ? pace->frame_ns - pace->owed_ns : 0;
  Uint64 elapsed = now_ns - last;

  if (pace->vsync) {
    return 0;
  }
  return due > elapsed ? due - elapsed : 0;
}

float smx_pace_alpha(const SmxPace *pace) {
  double owed = (double)pace->owed_ns;

  if (!pace->vsync) {
    return 1.0f;
  }
  /* Locked, the coming refresh is a whole step on and never past the last frame. */
  if (pace->lock != 0) {
    owed += (double)pace->step_ns;
  }
  return owed >= (double)pace->frame_ns ? 1.0f : (float)(owed / (double)pace->frame_ns);
}

void smx_pace_set_vsync(SmxPace *pace, bool vsync, Uint64 now_ns) {
  Uint64 period = pace->period_ns;
  Uint64 mode = pace->mode_ns;
  Uint64 frame = pace->frame_ns;
  float queued = pace->queued;

  if (pace->vsync == vsync) {
    return;
  }
  /* Measure again from here; the presents kept were paced the other way. */
  SDL_memset(pace, 0, sizeof(*pace));
  pace->frame_ns = frame;
  pace->period_ns = period;
  pace->mode_ns = mode;
  pace->queued = queued;
  pace->vsync = vsync;
  pace->presents[0] = now_ns;
  pace->refreshes[0] = 1;
  pace->next = 1;
  pace->count = 1;
  smx_pace_lock(pace);
}

float smx_pace_audio_ratio(SmxPace *pace, Uint32 queued, Uint32 target) {
  float ratio = 1.0f;
  float error = 0.0f;

  if (pace->queued < 0.0f) {
    pace->queued = (float)queued;
  }
  pace->queued += ((float)queued - pace->queued) * SMX_PACE_QUEUE_WEIGHT;
  if (pace->vsync && pace->lock != 0) {
    ratio = (float)((double)pace->frame_ns / ((double)pace->lock * (double)pace->period_ns));
  }
  if (target != 0) {
    error = (pace->queued - (float)target) / (float)target;
    error = error > 1.0f ? 1.0f : (error < -1.0f ? -1.0f : error);
  }
  return ratio * (1.0f + SMX_PACE_AUDIO_NUDGE * error);
}
