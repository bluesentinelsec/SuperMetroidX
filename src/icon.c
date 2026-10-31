#include "smx/icon.h"

/*
 * The icon: a rounded night-sky tile with an orange ring and a glowing cyan
 * X across it. Every shape is a signed distance, so edges are antialiased at
 * any size without supersampling.
 */

typedef struct SmxIconColor {
  float r, g, b;
} SmxIconColor;

/* Sizes of the window icon, the first the surface itself. */
static const Uint32 smx_icon_window_sizes[] = {64, 16, 24, 32, 48, 128, 256};

static float smx_icon_clamp(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static float smx_icon_smooth(float edge0, float edge1, float value) {
  float t = smx_icon_clamp((value - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}

static float smx_icon_max(float a, float b) {
  return a > b ? a : b;
}

static float smx_icon_min(float a, float b) {
  return a < b ? a : b;
}

static SmxIconColor smx_icon_mix(SmxIconColor a, SmxIconColor b, float t) {
  SmxIconColor color = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
  return color;
}

/* The distance from (x, y) to a square of half side `half` and corner radius `radius` at 0.5. */
static float smx_icon_tile(float x, float y, float half, float radius) {
  float qx = SDL_fabsf(x - 0.5f) - half + radius;
  float qy = SDL_fabsf(y - 0.5f) - half + radius;
  float ox = smx_icon_max(qx, 0.0f);
  float oy = smx_icon_max(qy, 0.0f);
  return SDL_sqrtf(ox * ox + oy * oy) + smx_icon_min(smx_icon_max(qx, qy), 0.0f) - radius;
}

/* The distance from (x, y) to the segment a-b, and in `along` how far along it the nearest point
 * is. */
static float smx_icon_segment(float x, float y, float ax, float ay, float bx, float by,
                              float *along) {
  float px = x - ax;
  float py = y - ay;
  float dx = bx - ax;
  float dy = by - ay;
  float t = smx_icon_clamp((px * dx + py * dy) / (dx * dx + dy * dy));
  float ex = px - dx * t;
  float ey = py - dy * t;
  *along = t;
  return SDL_sqrtf(ex * ex + ey * ey);
}

/*
 * The distance from (x, y) to the X: two beams, widest where they cross and
 * tapering to their ends. `half` gets the beam's half width there.
 */
static float smx_icon_cross(float x, float y, float *half) {
  const float middle = 0.085f;
  const float end = 0.026f;
  float t = 0.0f;
  float a = smx_icon_segment(x, y, 0.235f, 0.235f, 0.765f, 0.765f, &t);
  float half_a = end + (middle - end) * (1.0f - SDL_fabsf(2.0f * t - 1.0f));
  float b = smx_icon_segment(x, y, 0.765f, 0.235f, 0.235f, 0.765f, &t);
  float half_b = end + (middle - end) * (1.0f - SDL_fabsf(2.0f * t - 1.0f));

  if (a - half_a < b - half_b) {
    *half = half_a;
    return a - half_a;
  }
  *half = half_b;
  return b - half_b;
}

/* Stars, in tile coordinates with their radii, away from the emblem's middle. */
static const float smx_icon_stars[][3] = {
    {0.14f, 0.16f, 0.010f}, {0.31f, 0.09f, 0.006f}, {0.83f, 0.13f, 0.008f},
    {0.91f, 0.34f, 0.006f}, {0.08f, 0.42f, 0.007f}, {0.12f, 0.80f, 0.006f},
    {0.88f, 0.86f, 0.009f}, {0.66f, 0.93f, 0.006f}, {0.37f, 0.92f, 0.007f},
};

/* The opaque color of the tile at (x, y), in tile coordinates; `pixel` is one pixel there. */
static SmxIconColor smx_icon_shade(float x, float y, float pixel, bool stars) {
  const SmxIconColor top = {0.12f, 0.13f, 0.34f};
  const SmxIconColor bottom = {0.02f, 0.02f, 0.08f};
  const SmxIconColor teal = {0.08f, 0.55f, 0.55f};
  const SmxIconColor orange = {1.0f, 0.52f, 0.10f};
  const SmxIconColor amber = {1.0f, 0.80f, 0.36f};
  const SmxIconColor cyan = {0.18f, 0.78f, 1.0f};
  const SmxIconColor core = {0.90f, 0.98f, 1.0f};
  const SmxIconColor outline = {0.02f, 0.03f, 0.10f};
  const float ring_radius = 0.355f;
  const float ring_half = 0.032f;
  const float outline_width = 0.026f;
  float cx = x - 0.5f;
  float cy = y - 0.5f;
  float middle = SDL_sqrtf(cx * cx + cy * cy);
  SmxIconColor color = smx_icon_mix(top, bottom, smx_icon_clamp(y));
  float ring = SDL_fabsf(middle - ring_radius) - ring_half;
  float cross_half = 0.0f;
  float cross = smx_icon_cross(x, y, &cross_half);
  float glow = 0.0f;
  float cover = 0.0f;
  Uint32 star = 0;

  glow = 0.40f * SDL_expf(-(middle * middle) / (0.30f * 0.30f));
  color = smx_icon_mix(color, teal, glow);
  if (stars) {
    for (; star < SDL_arraysize(smx_icon_stars); star += 1) {
      float sx = x - smx_icon_stars[star][0];
      float sy = y - smx_icon_stars[star][1];
      float d = SDL_sqrtf(sx * sx + sy * sy) - smx_icon_stars[star][2];
      color = smx_icon_mix(color, core, 0.8f * smx_icon_clamp(0.5f - d / pixel));
    }
  }

  /* The ring, lighter at the top, with a soft orange glow outside it. */
  glow = 0.30f * SDL_expf(-smx_icon_max(ring, 0.0f) / 0.03f);
  color.r = smx_icon_clamp(color.r + orange.r * glow);
  color.g = smx_icon_clamp(color.g + orange.g * glow);
  color.b = smx_icon_clamp(color.b + orange.b * glow);
  cover = smx_icon_clamp(0.5f - ring / pixel);
  color = smx_icon_mix(color, smx_icon_mix(amber, orange, smx_icon_clamp(y)), cover);

  /* The X: a cyan glow, a dark outline that cuts the ring, then the beams. */
  glow = 0.55f * SDL_expf(-smx_icon_max(cross, 0.0f) / 0.035f);
  color.r = smx_icon_clamp(color.r + cyan.r * glow);
  color.g = smx_icon_clamp(color.g + cyan.g * glow);
  color.b = smx_icon_clamp(color.b + cyan.b * glow);
  cover = smx_icon_clamp(0.5f - (cross - outline_width) / pixel);
  color = smx_icon_mix(color, outline, cover * 0.92f);
  cover = smx_icon_clamp(0.5f - cross / pixel);
  {
    float inside = smx_icon_clamp(-cross / cross_half);
    color = smx_icon_mix(color, smx_icon_mix(cyan, core, inside * inside), cover);
  }
  return color;
}

void smx_icon_draw(Uint32 size, SmxIconStyle style, Uint8 *rgba) {
  /* macOS: 824 of 1024 pixels, corner radius 185; elsewhere almost the whole image. */
  float half = style == SMX_ICON_MACOS ? 412.0f / 1024.0f : (size <= 32u ? 0.5f : 0.485f);
  float radius = style == SMX_ICON_MACOS ? 185.0f / 1024.0f : half * 0.42f;
  float pixel = 1.0f / (float)size;
  float scale = 1.0f / (2.0f * half);
  Uint32 row = 0;

  for (; row < size; row += 1) {
    Uint32 column = 0;
    for (; column < size; column += 1) {
      float x = ((float)column + 0.5f) * pixel;
      float y = ((float)row + 0.5f) * pixel;
      float tile = smx_icon_tile(x, y, half, radius);
      float cover = smx_icon_clamp(0.5f - tile / pixel);
      float shadow = 0.0f;
      float alpha = 0.0f;
      SmxIconColor color = {0.0f, 0.0f, 0.0f};
      Uint8 *out = rgba + ((Uint64)row * size + column) * 4u;

      if (style == SMX_ICON_MACOS) {
        float drop = smx_icon_tile(x, y - 10.0f / 1024.0f, half, radius);
        shadow = 0.40f * (1.0f - smx_icon_smooth(-0.004f, 0.028f, drop));
      }
      if (cover > 0.0f) {
        float tx = (x - 0.5f) * scale + 0.5f;
        float ty = (y - 0.5f) * scale + 0.5f;
        float edge = smx_icon_clamp(0.5f - (tile + 0.012f) / pixel);
        color = smx_icon_shade(tx, ty, pixel * scale, size >= 64u);
        /* A faint light rim along the top of the tile. */
        color = smx_icon_mix(color, (SmxIconColor){1.0f, 1.0f, 1.0f},
                             (1.0f - edge) * 0.14f * smx_icon_clamp(1.0f - ty * 1.6f));
      }
      /* The tile over its shadow, which is black. */
      alpha = cover + shadow * (1.0f - cover);
      if (alpha > 0.0f) {
        color.r *= cover / alpha;
        color.g *= cover / alpha;
        color.b *= cover / alpha;
      }
      out[0] = (Uint8)(smx_icon_clamp(color.r) * 255.0f + 0.5f);
      out[1] = (Uint8)(smx_icon_clamp(color.g) * 255.0f + 0.5f);
      out[2] = (Uint8)(smx_icon_clamp(color.b) * 255.0f + 0.5f);
      out[3] = (Uint8)(smx_icon_clamp(alpha) * 255.0f + 0.5f);
    }
  }
}

static SDL_Surface *smx_icon_surface_sized(Uint32 size) {
  SDL_Surface *surface = SDL_CreateSurface((int)size, (int)size, SDL_PIXELFORMAT_RGBA32);
  Uint32 row = 0;
  Uint8 *rgba = NULL;

  if (surface == NULL) {
    return NULL;
  }
  rgba = (Uint8 *)SDL_malloc((size_t)size * size * 4u);
  if (rgba == NULL) {
    SDL_DestroySurface(surface);
    return NULL;
  }
  smx_icon_draw(size, SMX_ICON_FULL, rgba);
  for (; row < size; row += 1) {
    SDL_memcpy((Uint8 *)surface->pixels + (size_t)row * (size_t)surface->pitch,
               rgba + (size_t)row * size * 4u, (size_t)size * 4u);
  }
  SDL_free(rgba);
  return surface;
}

SDL_Surface *smx_icon_surface(void) {
  SDL_Surface *surface = smx_icon_surface_sized(smx_icon_window_sizes[0]);
  Uint32 index = 1;

  for (; surface != NULL && index < SDL_arraysize(smx_icon_window_sizes); index += 1) {
    SDL_Surface *image = smx_icon_surface_sized(smx_icon_window_sizes[index]);
    if (image != NULL) {
      SDL_AddSurfaceAlternateImage(surface, image);
      SDL_DestroySurface(image);
    }
  }
  return surface;
}
