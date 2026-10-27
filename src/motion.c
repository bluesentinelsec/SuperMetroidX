#include "smx/motion.h"

static SmxMotionFrame *smx_motion_building(SmxMotion *motion) {
  return motion->frames[motion->building];
}

static void smx_motion_line(void *user, const SmxPpu *ppu, Uint32 scanline) {
  SmxMotion *motion = (SmxMotion *)user;
  SmxMotionFrame *frame = smx_motion_building(motion);
  SmxMotionLine *line = &frame->lines[scanline];
  Uint32 index = 0;

  if (scanline == 1u) {
    /* VRAM and OAM change only in vblank and forced blank, so line 1 has them for the picture. */
    SDL_memcpy(frame->vram, ppu->vram, sizeof(frame->vram));
    SDL_memcpy(frame->oam, ppu->oam, sizeof(frame->oam));
    for (index = 0; index <= SMX_PPU_HEIGHT; index += 1) {
      frame->lines[index].drawn = false;
    }
    SDL_memcpy(frame->palettes[0], ppu->cgram, sizeof(frame->palettes[0]));
    frame->palette_count = 1;
    frame->cgram_serial = ppu->cgram_serial;
  } else if (frame->palette_count == 0) {
    /* The picture began before the hooks: skip it. */
    return;
  } else if (ppu->cgram_serial != frame->cgram_serial &&
             frame->palette_count < SMX_PPU_HEIGHT) {
    SDL_memcpy(frame->palettes[frame->palette_count], ppu->cgram,
               sizeof(frame->palettes[0]));
    frame->palette_count += 1;
    frame->cgram_serial = ppu->cgram_serial;
  }
  SDL_memcpy(line->regs, &ppu->bus->mmio[0x2100], SMX_MOTION_REGS);
  SDL_memcpy(line->bg_h, ppu->bg_h, sizeof(line->bg_h));
  SDL_memcpy(line->bg_v, ppu->bg_v, sizeof(line->bg_v));
  line->m7[0] = ppu->m7a;
  line->m7[1] = ppu->m7b;
  line->m7[2] = ppu->m7c;
  line->m7[3] = ppu->m7d;
  line->m7[4] = ppu->m7x;
  line->m7[5] = ppu->m7y;
  line->m7[6] = ppu->m7h;
  line->m7[7] = ppu->m7v;
  line->fixed[0] = ppu->fixed_r;
  line->fixed[1] = ppu->fixed_g;
  line->fixed[2] = ppu->fixed_b;
  line->oam_base = ppu->oam_base;
  line->oam_rotate = ppu->oam_rotate;
  line->palette = (Uint8)(frame->palette_count - 1u);
  line->drawn = true;
}

static void smx_motion_picture(void *user, const SmxPpu *ppu) {
  SmxMotion *motion = (SmxMotion *)user;
  SmxMotionFrame *frame = smx_motion_building(motion);
  Uint32 line = 1;
  Uint32 spare = motion->before;

  (void)ppu;
  frame->whole = frame->palette_count != 0;
  for (; frame->whole && line <= SMX_PPU_HEIGHT; line += 1) {
    frame->whole = frame->lines[line].drawn;
  }
  motion->before = motion->last;
  motion->last = motion->building;
  motion->building = spare;
  /* The shell sets the next picture's objects once this frame ends. */
  motion->frames[motion->building]->palette_count = 0;
  motion->frames[motion->building]->object_count = 0;
  motion->frames[motion->building]->playing = false;
  motion->count += 1;
}

bool smx_motion_init(SmxMotion *motion, SmxPpu *ppu) {
  Uint32 index = 0;

  SDL_memset(motion, 0, sizeof(*motion));
  for (; index < 3u; index += 1) {
    motion->frames[index] = (SmxMotionFrame *)SDL_calloc(1, sizeof(SmxMotionFrame));
    if (motion->frames[index] == NULL) {
      smx_motion_quit(motion);
      return false;
    }
  }
  motion->building = 0;
  motion->last = 1;
  motion->before = 2;
  smx_bus_init(&motion->bus, NULL, 0, 0);
  smx_ppu_init(&motion->ppu, &motion->bus);
  motion->source = ppu;
  ppu->line_hook = smx_motion_line;
  ppu->picture_hook = smx_motion_picture;
  ppu->hook_user = motion;
  return true;
}

void smx_motion_quit(SmxMotion *motion) {
  Uint32 index = 0;

  if (motion->source != NULL) {
    motion->source->line_hook = NULL;
    motion->source->picture_hook = NULL;
    motion->source->hook_user = NULL;
    smx_ppu_quit(&motion->ppu);
    smx_bus_quit(&motion->bus);
  }
  for (; index < 3u; index += 1) {
    SDL_free(motion->frames[index]);
  }
  SDL_memset(motion, 0, sizeof(*motion));
}

void smx_motion_reset(SmxMotion *motion) {
  motion->count = 0;
  motion->frames[motion->building]->palette_count = 0;
}

/* Super Metroid's RAM: the main state, the camera, Samus, and the enemy table. */
#define SMX_MOTION_GAME_STATE 0x0998u
#define SMX_MOTION_STATE_PLAY 0x08u
#define SMX_MOTION_CAMERA_X 0x0911u
#define SMX_MOTION_CAMERA_Y 0x0915u
#define SMX_MOTION_SAMUS_X 0x0AF6u
#define SMX_MOTION_SAMUS_Y 0x0AFAu
#define SMX_MOTION_SAMUS_RX 0x0AFEu
#define SMX_MOTION_SAMUS_RY 0x0B00u
#define SMX_MOTION_ENEMIES 0x0F78u
#define SMX_MOTION_ENEMY_SIZE 0x40u
#define SMX_MOTION_ENEMY_COUNT 32u
/* Samus's projectiles: X and Y words, ten of each. */
#define SMX_MOTION_BEAM_X 0x0B64u
#define SMX_MOTION_BEAM_Y 0x0B78u
#define SMX_MOTION_BEAM_COUNT 10u
/* Object ids: Samus, then enemies, then Samus's projectiles. */
#define SMX_MOTION_ID_ENEMY 0x100u
#define SMX_MOTION_ID_BEAM 0x200u

static Uint16 smx_motion_word(const Uint8 *wram, Uint32 addr) {
  return (Uint16)(wram[addr] | ((Uint16)wram[addr + 1u] << 8));
}

static void smx_motion_add(SmxMotionFrame *frame, Uint16 id, Sint32 x, Sint32 y, Sint32 rx,
                           Sint32 ry) {
  SmxMotionObject *object = NULL;

  if (frame->object_count == SMX_MOTION_OBJECTS || x < -256 || x > 512 || y < -256 || y > 480) {
    return;
  }
  object = &frame->objects[frame->object_count];
  object->id = id;
  object->x = (Sint16)x;
  object->y = (Sint16)y;
  object->rx = (Sint16)rx;
  object->ry = (Sint16)ry;
  frame->object_count += 1;
}

void smx_motion_objects(SmxMotion *motion, const Uint8 *wram) {
  /* The RAM after a frame places what the next picture shows. */
  SmxMotionFrame *frame = smx_motion_building(motion);
  Sint32 camera_x = smx_motion_word(wram, SMX_MOTION_CAMERA_X);
  Sint32 camera_y = smx_motion_word(wram, SMX_MOTION_CAMERA_Y);
  Uint32 index = 0;

  frame->object_count = 0;
  frame->playing = wram[SMX_MOTION_GAME_STATE] == SMX_MOTION_STATE_PLAY;
  if (!frame->playing) {
    return;
  }
  smx_motion_add(frame, 0, smx_motion_word(wram, SMX_MOTION_SAMUS_X) - camera_x,
                 smx_motion_word(wram, SMX_MOTION_SAMUS_Y) - camera_y,
                 smx_motion_word(wram, SMX_MOTION_SAMUS_RX),
                 smx_motion_word(wram, SMX_MOTION_SAMUS_RY));
  for (; index < SMX_MOTION_ENEMY_COUNT; index += 1) {
    Uint32 base = SMX_MOTION_ENEMIES + index * SMX_MOTION_ENEMY_SIZE;
    if (smx_motion_word(wram, base) == 0) {
      continue;
    }
    smx_motion_add(frame, (Uint16)(SMX_MOTION_ID_ENEMY + index),
                   smx_motion_word(wram, base + 2u) - camera_x,
                   smx_motion_word(wram, base + 6u) - camera_y, smx_motion_word(wram, base + 10u),
                   smx_motion_word(wram, base + 12u));
  }
  for (index = 0; index < SMX_MOTION_BEAM_COUNT; index += 1) {
    Sint32 x = smx_motion_word(wram, SMX_MOTION_BEAM_X + index * 2u);
    Sint32 y = smx_motion_word(wram, SMX_MOTION_BEAM_Y + index * 2u);
    if (x == 0 && y == 0) {
      continue;
    }
    smx_motion_add(frame, (Uint16)(SMX_MOTION_ID_BEAM + index), x - camera_x, y - camera_y, 8, 8);
  }
}

/* Larger changes in a frame are cuts, such as a new room, and do not move. */
#define SMX_MOTION_MAX_SCROLL 48
#define SMX_MOTION_MAX_SPRITE 32
/* How far past a game object's box a sprite can be and still belong to it. */
#define SMX_MOTION_OBJECT_REACH 16
/* How near a matched sprite of the same palette must be to lend its motion. */
#define SMX_MOTION_NEIGHBOR 24
/* Samus's sprites use palette 4. */
#define SMX_MOTION_SAMUS_PALETTE 4u

/* `value` wrapped into [-range / 2, range / 2). */
static Sint32 smx_motion_wrap(Sint32 value, Sint32 range) {
  value %= range;
  if (value >= range / 2) {
    value -= range;
  } else if (value < -range / 2) {
    value += range;
  }
  return value;
}

static Sint32 smx_motion_abs(Sint32 value) {
  return value < 0 ? -value : value;
}

static Sint32 smx_motion_round(float value) {
  return (Sint32)SDL_floorf(value + 0.5f);
}

/* One OAM entry, decoded. */
typedef struct SmxMotionSprite {
  Sint32 x;
  Sint32 y;
  Uint16 tile;
  Uint8 attr;
  bool large;
} SmxMotionSprite;

static void smx_motion_sprites(const Uint8 *oam, SmxMotionSprite *sprites) {
  Uint32 index = 0;

  for (; index < 128u; index += 1) {
    Uint8 high = (Uint8)((oam[512u + (index >> 2)] >> ((index & 3u) * 2u)) & 0x03);
    sprites[index].x = (Sint32)oam[index * 4u] | ((high & 0x01) != 0 ? 0x100 : 0);
    if (sprites[index].x >= 256) {
      sprites[index].x -= 512;
    }
    sprites[index].y = oam[index * 4u + 1u];
    sprites[index].tile = (Uint16)(oam[index * 4u + 2u] | ((Uint16)(oam[index * 4u + 3u] & 1u) << 8));
    sprites[index].attr = oam[index * 4u + 3u];
    sprites[index].large = (high & 0x02) != 0;
  }
}

static bool smx_motion_same(const SmxMotionSprite *a, const SmxMotionSprite *b) {
  return a->tile == b->tile && a->attr == b->attr && a->large == b->large &&
         smx_motion_abs(smx_motion_wrap(a->x - b->x, 512)) <= SMX_MOTION_MAX_SPRITE &&
         smx_motion_abs(smx_motion_wrap(a->y - b->y, 256)) <= SMX_MOTION_MAX_SPRITE;
}

static const SmxMotionObject *smx_motion_find(const SmxMotionFrame *frame, Uint16 id) {
  Uint32 index = 0;

  for (; index < frame->object_count; index += 1) {
    if (frame->objects[index].id == id) {
      return &frame->objects[index];
    }
  }
  return NULL;
}

/*
 * The motion of sprites whose tiles changed, from the game object they sit
 * on: Samus for her palette, else the nearest enemy or projectile around it.
 */
static bool smx_motion_object_move(const SmxMotionFrame *last, const SmxMotionFrame *before,
                                   const SmxMotionSprite *sprite, Sint32 *dx, Sint32 *dy) {
  Sint32 size = sprite->large ? 16 : 8;
  Sint32 cx = sprite->x + size / 2;
  Sint32 cy = sprite->y + 1 + size / 2;
  Sint32 best = 0x7FFFFFFF;
  bool found = false;
  Uint32 index = 0;

  if (!last->playing || !before->playing) {
    return false;
  }
  for (; index < last->object_count; index += 1) {
    const SmxMotionObject *now = &last->objects[index];
    const SmxMotionObject *then = smx_motion_find(before, now->id);
    Sint32 ox = smx_motion_abs(cx - now->x);
    Sint32 oy = smx_motion_abs(cy - now->y);
    bool samus = now->id == 0 && ((sprite->attr >> 1) & 7u) == SMX_MOTION_SAMUS_PALETTE;
    if (then == NULL || ox > now->rx + SMX_MOTION_OBJECT_REACH ||
        oy > now->ry + SMX_MOTION_OBJECT_REACH) {
      continue;
    }
    /* Samus's own sprites go with her; others go with the nearest object. */
    if (samus || ox + oy < best) {
      best = samus ? -1 : ox + oy;
      *dx = now->x - then->x;
      *dy = now->y - then->y;
      found = true;
    }
  }
  return found && smx_motion_abs(*dx) <= SMX_MOTION_MAX_SPRITE &&
         smx_motion_abs(*dy) <= SMX_MOTION_MAX_SPRITE;
}

/*
 * Each sprite's motion from the picture before to the last. In play, a sprite
 * on a game object moves with it, all of a metasprite together, whatever its
 * animation does to the tiles. Otherwise the same tiles in the same OAM order
 * (the longest common run, as Super Metroid redraws OAM in object order every
 * frame), else a matched neighbor of the same palette, else the world layer's.
 */
static void smx_motion_sprite_moves(SmxMotion *motion, const SmxMotionFrame *last,
                                    const SmxMotionFrame *before, Sint32 world_dx, Sint32 world_dy,
                                    Sint32 *dx, Sint32 *dy) {
  SmxMotionSprite now[128];
  SmxMotionSprite then[128];
  bool matched[128];
  Uint8(*runs)[129] = motion->runs;
  Sint32 i = 0;
  Sint32 j = 0;

  smx_motion_sprites(last->oam, now);
  smx_motion_sprites(before->oam, then);
  for (i = 128; i >= 0; i -= 1) {
    for (j = 128; j >= 0; j -= 1) {
      if (i == 128 || j == 128) {
        runs[i][j] = 0;
      } else if (smx_motion_same(&now[i], &then[j])) {
        runs[i][j] = (Uint8)(runs[i + 1][j + 1] + 1u);
      } else {
        runs[i][j] = runs[i + 1][j] > runs[i][j + 1] ? runs[i + 1][j] : runs[i][j + 1];
      }
    }
  }
  for (i = 0; i < 128; i += 1) {
    matched[i] = false;
    dx[i] = 0;
    dy[i] = 0;
  }
  for (i = 0, j = 0; i < 128 && j < 128;) {
    if (smx_motion_same(&now[i], &then[j]) && runs[i][j] == runs[i + 1][j + 1] + 1u) {
      matched[i] = true;
      dx[i] = smx_motion_wrap(now[i].x - then[j].x, 512);
      dy[i] = smx_motion_wrap(now[i].y - then[j].y, 256);
      i += 1;
      j += 1;
    } else if (runs[i + 1][j] >= runs[i][j + 1]) {
      i += 1;
    } else {
      j += 1;
    }
  }
  for (i = 0; i < 128; i += 1) {
    Sint32 best = SMX_MOTION_NEIGHBOR * 2 + 1;
    Sint32 k = 0;
    Sint32 ox = 0;
    Sint32 oy = 0;
    if (smx_motion_object_move(last, before, &now[i], &ox, &oy)) {
      dx[i] = ox;
      dy[i] = oy;
      continue;
    }
    if (matched[i]) {
      continue;
    }
    dx[i] = last->playing ? world_dx : 0;
    dy[i] = last->playing ? world_dy : 0;
    for (; k < 128; k += 1) {
      Sint32 far = smx_motion_abs(now[k].x - now[i].x) + smx_motion_abs(now[k].y - now[i].y);
      if (matched[k] && far < best && (now[k].attr & 0x0E) == (now[i].attr & 0x0E) &&
          smx_motion_abs(now[k].x - now[i].x) <= SMX_MOTION_NEIGHBOR &&
          smx_motion_abs(now[k].y - now[i].y) <= SMX_MOTION_NEIGHBOR) {
        best = far;
        dx[i] = dx[k];
        dy[i] = dy[k];
      }
    }
  }
}

/*
 * The line shows `layer`: on the main screen, or on the subscreen while color
 * math blends it in, as some rooms draw the world. Super Metroid's HUD lines
 * keep BG1 on the subscreen with math off, so they do not count.
 */
static bool smx_motion_shows(const SmxMotionLine *line, Uint8 layer) {
  Uint8 bit = (Uint8)(1u << layer);
  bool blended = (line->regs[0x30] & 0x02) != 0 && (line->regs[0x31] & 0x3F) != 0;

  return (line->regs[0x2C] & bit) != 0 || (blended && (line->regs[0x2D] & bit) != 0);
}

/* A line of the last picture can move from the same line before. */
static bool smx_motion_line_moves(const SmxMotionLine *now, const SmxMotionLine *then) {
  return now->drawn && then->drawn && (now->regs[0] & 0x80) == 0 && (then->regs[0] & 0x80) == 0 &&
         (now->regs[5] & 0x07) == (then->regs[5] & 0x07) && (now->regs[5] & 0x07) != 7u;
}

/* Redraw the last picture moved back by `back` of its motion; NULL when nothing moves and not `always`. */
static const Uint8 *smx_motion_render(SmxMotion *motion, float back, bool always,
                                      SmxMotionShift *shift) {
  const SmxMotionFrame *last = motion->frames[motion->last];
  const SmxMotionFrame *before = motion->frames[motion->before];
  SmxPpu *ppu = &motion->ppu;
  float exact_h[SMX_PPU_HEIGHT + 1][4];
  float exact_v[SMX_PPU_HEIGHT + 1][4];
  Sint32 sprite_dx[128];
  Sint32 sprite_dy[128];
  Sint32 world_dx = 0;
  Sint32 world_dy = 0;
  bool moving = false;
  bool found_world = false;
  Uint32 line = 1;
  Uint32 layer = 0;
  Uint32 index = 0;
  Sint32 palette = -1;
  float fx_world = 0.0f;

  if (motion->count < 2u || !last->whole || !before->whole) {
    return NULL;
  }
  shift->y = 0.0f;
  shift->top = 0;
  shift->bottom = 0;

  /* Each background's scroll on each line, moved back by its change since the picture before. */
  for (line = 1; line <= SMX_PPU_HEIGHT; line += 1) {
    const SmxMotionLine *now = &last->lines[line];
    const SmxMotionLine *then = &before->lines[line];
    bool moves = smx_motion_line_moves(now, then);
    bool world = moves && smx_motion_shows(now, 0);
    for (layer = 0; layer < 4u; layer += 1) {
      Sint32 h = now->bg_h[layer] & 0x3FF;
      Sint32 v = now->bg_v[layer] & 0x3FF;
      Sint32 dh = moves ? smx_motion_wrap(h - (then->bg_h[layer] & 0x3FF), 1024) : 0;
      Sint32 dv = moves ? smx_motion_wrap(v - (then->bg_v[layer] & 0x3FF), 1024) : 0;
      if (smx_motion_abs(dh) > SMX_MOTION_MAX_SCROLL || smx_motion_abs(dv) > SMX_MOTION_MAX_SCROLL) {
        dh = 0;
        dv = 0;
      }
      moving = moving || dh != 0 || dv != 0;
      exact_h[line][layer] = (float)h - back * (float)dh;
      exact_v[line][layer] = (float)v - back * (float)dv;
      if (layer == 0 && world && !found_world) {
        /* The first line of the world layer sets how the sprites and the camera move. */
        found_world = true;
        world_dx = -dh;
        world_dy = -dv;
        shift->y = exact_v[line][0] - SDL_floorf(exact_v[line][0]);
        fx_world = exact_h[line][0] - SDL_floorf(exact_h[line][0]);
        shift->top = line - 1u;
      }
    }
    shift->x[line - 1u] = 0.0f;
    if (world) {
      shift->x[line - 1u] = exact_h[line][0] - SDL_floorf(exact_h[line][0]);
      shift->bottom = line;
    }
  }

  SDL_memcpy(ppu->oam, last->oam, sizeof(ppu->oam));
  if (before->palette_count != 0) {
    smx_motion_sprite_moves(motion, last, before, world_dx, world_dy, sprite_dx, sprite_dy);
    for (index = 0; index < 128u; index += 1) {
      Uint8 *entry = &ppu->oam[index * 4u];
      Uint8 *high = &ppu->oam[512u + (index >> 2)];
      Uint8 bit = (Uint8)(1u << ((index & 3u) * 2u));
      Sint32 x = (Sint32)entry[0] | ((*high & bit) != 0 ? 0x100 : 0);
      bool in_world = entry[1] + 1u >= shift->top && entry[1] < shift->bottom;
      Sint32 nx = 0;
      Sint32 ny = 0;
      if ((sprite_dx[index] == 0 && sprite_dy[index] == 0) || x == 0x100) {
        continue;
      }
      if (x >= 256) {
        x -= 512;
      }
      moving = true;
      nx = smx_motion_round((float)x - back * (float)sprite_dx[index] +
                            (in_world ? fx_world : 0.0f));
      ny = smx_motion_round((float)entry[1] - back * (float)sprite_dy[index] +
                            (in_world ? shift->y : 0.0f));
      /* X = -256 is a hidden sprite; keep moved ones off it. */
      if ((nx & 0x1FF) == 0x100) {
        nx += 1;
      }
      entry[0] = (Uint8)nx;
      entry[1] = (Uint8)ny;
      *high = (Uint8)((nx & 0x100) != 0 ? (*high | bit) : (*high & ~bit));
    }
  }
  if (!moving && !always) {
    return NULL;
  }

  /* Redraw the last picture with the moved scroll and sprites; the world fraction is left over. */
  SDL_memcpy(ppu->vram, last->vram, sizeof(ppu->vram));
  ppu->pictures += 1;
  for (line = 1; line <= SMX_PPU_HEIGHT; line += 1) {
    const SmxMotionLine *now = &last->lines[line];
    bool world = shift->bottom != 0 && line - 1u >= shift->top && line - 1u < shift->bottom;
    float fx = shift->x[line - 1u];
    float fy = world ? shift->y : 0.0f;
    if (now->palette != palette) {
      palette = now->palette;
      SDL_memcpy(ppu->cgram, last->palettes[palette], sizeof(ppu->cgram));
    }
    SDL_memcpy(&motion->bus.mmio[0x2100], now->regs, SMX_MOTION_REGS);
    for (layer = 0; layer < 4u; layer += 1) {
      ppu->bg_h[layer] = (Uint16)(smx_motion_round(exact_h[line][layer] - fx) & 0x3FF);
      ppu->bg_v[layer] = (Uint16)(smx_motion_round(exact_v[line][layer] - fy) & 0x3FF);
    }
    ppu->m7a = now->m7[0];
    ppu->m7b = now->m7[1];
    ppu->m7c = now->m7[2];
    ppu->m7d = now->m7[3];
    ppu->m7x = now->m7[4];
    ppu->m7y = now->m7[5];
    ppu->m7h = now->m7[6];
    ppu->m7v = now->m7[7];
    ppu->fixed_r = now->fixed[0];
    ppu->fixed_g = now->fixed[1];
    ppu->fixed_b = now->fixed[2];
    ppu->oam_base = now->oam_base;
    ppu->oam_rotate = now->oam_rotate;
    smx_ppu_present_line(ppu, line);
  }
  if (shift->bottom == 0) {
    shift->y = 0.0f;
  }
  return ppu->pixels;
}

const Uint8 *smx_motion_draw(SmxMotion *motion, float alpha, SmxMotionShift *shift) {
  if (alpha >= 1.0f) {
    return NULL;
  }
  return smx_motion_render(motion, alpha <= 0.0f ? 1.0f : 1.0f - alpha, false, shift);
}

const Uint8 *smx_motion_redraw(SmxMotion *motion) {
  SmxMotionShift shift;

  return smx_motion_render(motion, 0.0f, true, &shift);
}
