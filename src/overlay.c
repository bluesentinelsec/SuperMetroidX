#include "smx/overlay.h"

/* Pixels from the picture's left and bottom edges, and around the text. */
#define SMX_OVERLAY_MARGIN 8.0f
#define SMX_OVERLAY_PAD 2.0f
#define SMX_OVERLAY_LINE 10.0f

bool smx_overlay_init(SmxOverlay *overlay, Uint32 width, Uint32 height) {
  SDL_memset(overlay, 0, sizeof(*overlay));
  overlay->width = width;
  overlay->height = height;
  overlay->pixels = (Uint8 *)SDL_malloc((size_t)width * height * 4u);
  if (overlay->pixels == NULL) {
    return false;
  }
  overlay->surface = SDL_CreateSurfaceFrom((int)width, (int)height, SDL_PIXELFORMAT_XRGB8888,
                                           overlay->pixels, (int)(width * 4u));
  if (overlay->surface != NULL) {
    overlay->renderer = SDL_CreateSoftwareRenderer(overlay->surface);
  }
  if (overlay->renderer == NULL) {
    SDL_Log("messages unavailable: %s", SDL_GetError());
    smx_overlay_quit(overlay);
    return false;
  }
  SDL_SetRenderDrawBlendMode(overlay->renderer, SDL_BLENDMODE_BLEND);
  return true;
}

void smx_overlay_quit(SmxOverlay *overlay) {
  if (overlay->renderer != NULL) {
    SDL_DestroyRenderer(overlay->renderer);
  }
  if (overlay->surface != NULL) {
    SDL_DestroySurface(overlay->surface);
  }
  SDL_free(overlay->pixels);
  SDL_memset(overlay, 0, sizeof(*overlay));
}

/* Drop the messages that have shown their time. */
static void smx_overlay_expire(SmxOverlay *overlay, Uint64 now_ms) {
  Uint32 kept = 0;
  Uint32 index = 0;

  for (; index < overlay->count; index += 1) {
    if (overlay->until[index] > now_ms) {
      if (kept != index) {
        SDL_memcpy(overlay->text[kept], overlay->text[index], sizeof(overlay->text[0]));
        overlay->until[kept] = overlay->until[index];
      }
      kept += 1;
    }
  }
  overlay->count = kept;
}

void smx_overlay_show(SmxOverlay *overlay, const char *text, Uint64 now_ms) {
  smx_overlay_expire(overlay, now_ms);
  if (overlay->count == SMX_OVERLAY_LINES) {
    SDL_memmove(overlay->text[0], overlay->text[1],
                sizeof(overlay->text[0]) * (SMX_OVERLAY_LINES - 1u));
    SDL_memmove(&overlay->until[0], &overlay->until[1],
                sizeof(overlay->until[0]) * (SMX_OVERLAY_LINES - 1u));
    overlay->count -= 1u;
  }
  SDL_strlcpy(overlay->text[overlay->count], text, sizeof(overlay->text[0]));
  overlay->until[overlay->count] = now_ms + SMX_OVERLAY_MS;
  overlay->count += 1u;
}

const Uint8 *smx_overlay_draw(SmxOverlay *overlay, const Uint8 *picture, Uint64 now_ms) {
  Uint32 index = 0;
  float bottom = 0.0f;

  smx_overlay_expire(overlay, now_ms);
  if (overlay->renderer == NULL || overlay->count == 0 || picture == NULL) {
    return picture;
  }
  SDL_memcpy(overlay->pixels, picture, (size_t)overlay->width * overlay->height * 4u);
  /* The newest message is at the bottom. */
  bottom = (float)overlay->height - SMX_OVERLAY_MARGIN;
  for (index = overlay->count; index > 0; index -= 1) {
    const char *text = overlay->text[index - 1u];
    float y = bottom - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE -
              (float)(overlay->count - index) * SMX_OVERLAY_LINE;
    SDL_FRect box;
    box.x = SMX_OVERLAY_MARGIN - SMX_OVERLAY_PAD;
    box.y = y - SMX_OVERLAY_PAD;
    box.w = (float)SDL_strlen(text) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + SMX_OVERLAY_PAD * 2.0f;
    box.h = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + SMX_OVERLAY_PAD * 2.0f;
    SDL_SetRenderDrawColor(overlay->renderer, 0, 0, 0, 176);
    SDL_RenderFillRect(overlay->renderer, &box);
    SDL_SetRenderDrawColor(overlay->renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(overlay->renderer, SMX_OVERLAY_MARGIN, y, text);
  }
  SDL_RenderPresent(overlay->renderer);
  return overlay->pixels;
}
