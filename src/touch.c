#include "smx/touch.h"

/* Joypad bits, as a 16-bit read of `$4218` gives them. */
#define SMX_TOUCH_JOY_B 0x8000u
#define SMX_TOUCH_JOY_Y 0x4000u
#define SMX_TOUCH_JOY_SELECT 0x2000u
#define SMX_TOUCH_JOY_START 0x1000u
#define SMX_TOUCH_JOY_UP 0x0800u
#define SMX_TOUCH_JOY_DOWN 0x0400u
#define SMX_TOUCH_JOY_LEFT 0x0200u
#define SMX_TOUCH_JOY_RIGHT 0x0100u
#define SMX_TOUCH_JOY_A 0x0080u
#define SMX_TOUCH_JOY_X 0x0040u
#define SMX_TOUCH_JOY_L 0x0020u
#define SMX_TOUCH_JOY_R 0x0010u

/*
 * How far past its edge a control still takes a finger, as a share of its
 * size. Face buttons reach far enough that a thumb between two presses both.
 */
#define SMX_TOUCH_DPAD_REACH 0.75f
#define SMX_TOUCH_FACE_REACH 1.3f
#define SMX_TOUCH_BAR_REACH_X 1.15f
#define SMX_TOUCH_BAR_REACH_Y 1.6f
#define SMX_TOUCH_MENU_REACH 1.4f
/* Near the D-pad's center no direction is held; past this share of the
 * finger's distance along an axis, that axis's direction is. */
#define SMX_TOUCH_DPAD_DEAD 0.08f
#define SMX_TOUCH_DPAD_AXIS 0.38f
/* The D-pad's arms are this share of its size wide. */
#define SMX_TOUCH_DPAD_ARM 0.34f

/* Points on a round end. */
#define SMX_TOUCH_ROUND_POINTS 24u

/* Opacity of a control at rest and while held. */
#define SMX_TOUCH_ALPHA 0.6f
#define SMX_TOUCH_ALPHA_HELD 0.92f
/* The dark D-pad stays nearly solid, to stand out from the body behind it. */
#define SMX_TOUCH_DPAD_ALPHA 0.88f
/* The light grey of the console's body, behind the D-pad and the face buttons. */
#define SMX_TOUCH_BODY_ALPHA 0.28f

static const Uint16 smx_touch_bits[SMX_TOUCH_CONTROLS] = {
    0,
    SMX_TOUCH_JOY_A,
    SMX_TOUCH_JOY_B,
    SMX_TOUCH_JOY_X,
    SMX_TOUCH_JOY_Y,
    SMX_TOUCH_JOY_L,
    SMX_TOUCH_JOY_R,
    SMX_TOUCH_JOY_SELECT,
    SMX_TOUCH_JOY_START,
    0,
};

/* The Super Famicom's colors, 0-255. */
static const Uint8 smx_touch_colors[SMX_TOUCH_CONTROLS][3] = {
    {44, 44, 50},    /* D-pad */
    {214, 42, 46},   /* A, red */
    {236, 184, 24},  /* B, yellow */
    {44, 84, 196},   /* X, blue */
    {36, 150, 76},   /* Y, green */
    {176, 176, 184}, /* L */
    {176, 176, 184}, /* R */
    {96, 96, 104},   /* Select */
    {96, 96, 104},   /* Start */
    {44, 44, 50},    /* menu */
};

static const char *const smx_touch_labels[SMX_TOUCH_CONTROLS] = {
    "", "A", "B", "X", "Y", "L", "R", "SELECT", "START", "",
};

void smx_touch_init(SmxTouch *touch) {
  SDL_memset(touch, 0, sizeof(*touch));
  touch->shown = true;
}

static void smx_touch_place(SmxTouch *touch, SmxTouchControl control, float x, float y, float w,
                            float h) {
  touch->shapes[control].x = x;
  touch->shapes[control].y = y;
  touch->shapes[control].w = w;
  touch->shapes[control].h = h;
}

void smx_touch_layout(SmxTouch *touch, Sint32 width, Sint32 height, const SDL_Rect *safe) {
  float left = (float)safe->x;
  float top = (float)safe->y;
  float right = (float)(safe->x + safe->w);
  float bottom = (float)(safe->y + safe->h);
  /* Everything scales with the safe height, or half the width on a narrow screen. */
  float u = (float)safe->h < (float)safe->w * 0.5f ? (float)safe->h : (float)safe->w * 0.5f;
  float pad_x = left + 0.17f * u;
  float pad_y = bottom - 0.30f * u;
  float face_x = right - 0.1775f * u;
  float face_d = 0.10f * u;
  float face = 0.115f * u;
  float center = (float)width * 0.5f;

  touch->width = (float)width;
  touch->height = (float)height;
  smx_touch_place(touch, SMX_TOUCH_DPAD, pad_x, pad_y, 0.30f * u, 0.30f * u);
  smx_touch_place(touch, SMX_TOUCH_L, pad_x - 0.077f * u, pad_y - 0.25f * u, 0.14f * u, 0.075f * u);
  smx_touch_place(touch, SMX_TOUCH_R, pad_x + 0.077f * u, pad_y - 0.25f * u, 0.14f * u, 0.075f * u);
  smx_touch_place(touch, SMX_TOUCH_X, face_x, pad_y - face_d, face, face);
  smx_touch_place(touch, SMX_TOUCH_A, face_x + face_d, pad_y, face, face);
  smx_touch_place(touch, SMX_TOUCH_B, face_x, pad_y + face_d, face, face);
  smx_touch_place(touch, SMX_TOUCH_Y, face_x - face_d, pad_y, face, face);
  smx_touch_place(touch, SMX_TOUCH_SELECT, center - 0.08f * u, bottom - 0.045f * u, 0.13f * u,
                  0.05f * u);
  smx_touch_place(touch, SMX_TOUCH_START, center + 0.08f * u, bottom - 0.045f * u, 0.13f * u,
                  0.05f * u);
  smx_touch_place(touch, SMX_TOUCH_MENU, right - 0.06f * u, top + 0.06f * u, 0.085f * u,
                  0.085f * u);
}

/* The finger is over the control, with the control's reach. */
static bool smx_touch_over(const SmxTouch *touch, SmxTouchControl control, float x, float y) {
  const SmxTouchShape *shape = &touch->shapes[control];
  float dx = x - shape->x;
  float dy = y - shape->y;
  float reach = control == SMX_TOUCH_MENU ? SMX_TOUCH_MENU_REACH : SMX_TOUCH_FACE_REACH;

  if (control == SMX_TOUCH_DPAD) {
    reach = shape->w * SMX_TOUCH_DPAD_REACH;
    return dx * dx + dy * dy <= reach * reach;
  }
  if (control >= SMX_TOUCH_L && control <= SMX_TOUCH_START) {
    return SDL_fabsf(dx) <= shape->w * 0.5f * SMX_TOUCH_BAR_REACH_X &&
           SDL_fabsf(dy) <= shape->h * 0.5f * SMX_TOUCH_BAR_REACH_Y;
  }
  reach *= shape->w * 0.5f;
  return dx * dx + dy * dy <= reach * reach;
}

static SmxTouchFinger *smx_touch_finger(SmxTouch *touch, SDL_FingerID id) {
  Uint32 index = 0;

  for (; index < SMX_TOUCH_FINGERS; index += 1) {
    if (touch->fingers[index].down && touch->fingers[index].id == id) {
      return &touch->fingers[index];
    }
  }
  return NULL;
}

bool smx_touch_event(SmxTouch *touch, const SDL_Event *event) {
  SmxTouchFinger *finger = NULL;
  float x = 0.0f;
  float y = 0.0f;
  Uint32 index = 0;

  if (event->type != SDL_EVENT_FINGER_DOWN && event->type != SDL_EVENT_FINGER_MOTION &&
      event->type != SDL_EVENT_FINGER_UP && event->type != SDL_EVENT_FINGER_CANCELED) {
    return false;
  }
  x = event->tfinger.x * touch->width;
  y = event->tfinger.y * touch->height;
  finger = smx_touch_finger(touch, event->tfinger.fingerID);
  if (event->type == SDL_EVENT_FINGER_DOWN) {
    if (smx_touch_over(touch, SMX_TOUCH_MENU, x, y)) {
      return true;
    }
    for (; finger == NULL && index < SMX_TOUCH_FINGERS; index += 1) {
      if (!touch->fingers[index].down) {
        finger = &touch->fingers[index];
      }
    }
    if (finger == NULL) {
      return false;
    }
    finger->id = event->tfinger.fingerID;
    finger->down = true;
    finger->dpad = smx_touch_over(touch, SMX_TOUCH_DPAD, x, y);
  } else if (finger == NULL) {
    return false;
  } else if (event->type != SDL_EVENT_FINGER_MOTION) {
    finger->down = false;
    return false;
  }
  finger->x = x;
  finger->y = y;
  return false;
}

void smx_touch_release(SmxTouch *touch) {
  Uint32 index = 0;

  for (; index < SMX_TOUCH_FINGERS; index += 1) {
    touch->fingers[index].down = false;
  }
}

/* The directions a finger on the D-pad holds. */
static Uint16 smx_touch_dpad(const SmxTouch *touch, const SmxTouchFinger *finger) {
  const SmxTouchShape *pad = &touch->shapes[SMX_TOUCH_DPAD];
  float dx = finger->x - pad->x;
  float dy = finger->y - pad->y;
  float length = SDL_sqrtf(dx * dx + dy * dy);
  Uint16 joy = 0;

  if (length < pad->w * SMX_TOUCH_DPAD_DEAD) {
    return 0;
  }
  if (dx < -length * SMX_TOUCH_DPAD_AXIS) {
    joy = (Uint16)(joy | SMX_TOUCH_JOY_LEFT);
  } else if (dx > length * SMX_TOUCH_DPAD_AXIS) {
    joy = (Uint16)(joy | SMX_TOUCH_JOY_RIGHT);
  }
  if (dy < -length * SMX_TOUCH_DPAD_AXIS) {
    joy = (Uint16)(joy | SMX_TOUCH_JOY_UP);
  } else if (dy > length * SMX_TOUCH_DPAD_AXIS) {
    joy = (Uint16)(joy | SMX_TOUCH_JOY_DOWN);
  }
  return joy;
}

Uint16 smx_touch_joy(const SmxTouch *touch) {
  Uint16 joy = 0;
  Uint32 index = 0;
  Uint32 control = 0;

  for (; index < SMX_TOUCH_FINGERS; index += 1) {
    const SmxTouchFinger *finger = &touch->fingers[index];
    if (!finger->down) {
      continue;
    }
    if (finger->dpad) {
      joy = (Uint16)(joy | smx_touch_dpad(touch, finger));
      continue;
    }
    for (control = SMX_TOUCH_A; control <= SMX_TOUCH_START; control += 1) {
      if (smx_touch_over(touch, (SmxTouchControl)control, finger->x, finger->y)) {
        joy = (Uint16)(joy | smx_touch_bits[control]);
      }
    }
  }
  return joy;
}

/* A convex fan: the round-ended box `w` x `h` around `x`, `y`; a circle when `w` is `h`. */
void smx_touch_fill_capsule(SDL_Renderer *renderer, float x, float y, float w, float h,
                            SDL_FColor color) {
  SDL_Vertex vertices[2u * SMX_TOUCH_ROUND_POINTS + 2u];
  int indices[3u * (2u * SMX_TOUCH_ROUND_POINTS + 1u)];
  float r = h * 0.5f;
  float reach = w > h ? (w - h) * 0.5f : 0.0f;
  Uint32 count = 1;
  Uint32 index = 0;
  Uint32 end = 0;

  SDL_memset(vertices, 0, sizeof(vertices));
  vertices[0].position.x = x;
  vertices[0].position.y = y;
  vertices[0].color = color;
  /* The right end from top to bottom, then the left end from bottom to top. */
  for (end = 0; end < 2u; end += 1) {
    float cx = end == 0 ? x + reach : x - reach;
    for (index = 0; index < SMX_TOUCH_ROUND_POINTS; index += 1) {
      float angle =
          SDL_PI_F * ((float)index / (float)(SMX_TOUCH_ROUND_POINTS - 1u) - 0.5f + (float)end);
      vertices[count].position.x = cx + r * SDL_cosf(angle);
      vertices[count].position.y = y + r * SDL_sinf(angle);
      vertices[count].color = color;
      count += 1;
    }
  }
  for (index = 1; index < count; index += 1) {
    indices[(index - 1u) * 3u] = 0;
    indices[(index - 1u) * 3u + 1u] = (int)index;
    indices[(index - 1u) * 3u + 2u] = (int)(index + 1u < count ? index + 1u : 1u);
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(renderer, NULL, vertices, (int)count, indices, (int)((count - 1u) * 3u));
}

void smx_touch_fill_circle(SDL_Renderer *renderer, float x, float y, float r, SDL_FColor color) {
  smx_touch_fill_capsule(renderer, x, y, r * 2.0f, r * 2.0f, color);
}

static SDL_FColor smx_touch_color(SmxTouchControl control, bool held) {
  SDL_FColor color;
  float lift = held ? 0.25f : 0.0f;

  color.r = (float)smx_touch_colors[control][0] / 255.0f;
  color.g = (float)smx_touch_colors[control][1] / 255.0f;
  color.b = (float)smx_touch_colors[control][2] / 255.0f;
  color.r += (1.0f - color.r) * lift;
  color.g += (1.0f - color.g) * lift;
  color.b += (1.0f - color.b) * lift;
  color.a = held ? SMX_TOUCH_ALPHA_HELD : SMX_TOUCH_ALPHA;
  if (control == SMX_TOUCH_DPAD) {
    color.a = held ? SMX_TOUCH_ALPHA_HELD : SMX_TOUCH_DPAD_ALPHA;
  }
  return color;
}

/* `text` centered on `x`, `y` in SDL's 8x8 font, about `size` pixels tall. */
static void smx_touch_text(SDL_Renderer *renderer, float x, float y, float size, const char *text,
                           float alpha) {
  float scale = SDL_floorf(size / (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
  float before_x = 1.0f;
  float before_y = 1.0f;
  float width = (float)SDL_strlen(text) * (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

  if (scale < 1.0f) {
    scale = 1.0f;
  }
  SDL_GetRenderScale(renderer, &before_x, &before_y);
  SDL_SetRenderScale(renderer, scale, scale);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, (Uint8)(alpha * 255.0f));
  SDL_RenderDebugText(renderer, x / scale - width * 0.5f,
                      y / scale - (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * 0.5f, text);
  SDL_SetRenderScale(renderer, before_x, before_y);
}

/* One arm of the D-pad, or its middle, as a quad. */
static void smx_touch_quad(SDL_Renderer *renderer, float x0, float y0, float x1, float y1,
                           SDL_FColor color) {
  SDL_Vertex vertices[4];
  static const int indices[6] = {0, 1, 2, 0, 2, 3};

  SDL_memset(vertices, 0, sizeof(vertices));
  vertices[0].position.x = x0;
  vertices[0].position.y = y0;
  vertices[1].position.x = x1;
  vertices[1].position.y = y0;
  vertices[2].position.x = x1;
  vertices[2].position.y = y1;
  vertices[3].position.x = x0;
  vertices[3].position.y = y1;
  vertices[0].color = color;
  vertices[1].color = color;
  vertices[2].color = color;
  vertices[3].color = color;
  SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);
}

/* The cross, each arm lit while its direction is held. */
static void smx_touch_draw_dpad(const SmxTouch *touch, SDL_Renderer *renderer, Uint16 joy) {
  const SmxTouchShape *pad = &touch->shapes[SMX_TOUCH_DPAD];
  float half = pad->w * 0.5f;
  float arm = pad->w * SMX_TOUCH_DPAD_ARM * 0.5f;
  float x = pad->x;
  float y = pad->y;
  SDL_FColor rest = smx_touch_color(SMX_TOUCH_DPAD, false);
  SDL_FColor held = smx_touch_color(SMX_TOUCH_DPAD, true);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  smx_touch_quad(renderer, x - arm, y - half, x + arm, y - arm,
                 (joy & SMX_TOUCH_JOY_UP) != 0 ? held : rest);
  smx_touch_quad(renderer, x - arm, y + arm, x + arm, y + half,
                 (joy & SMX_TOUCH_JOY_DOWN) != 0 ? held : rest);
  smx_touch_quad(renderer, x - half, y - arm, x - arm, y + arm,
                 (joy & SMX_TOUCH_JOY_LEFT) != 0 ? held : rest);
  smx_touch_quad(renderer, x + arm, y - arm, x + half, y + arm,
                 (joy & SMX_TOUCH_JOY_RIGHT) != 0 ? held : rest);
  smx_touch_quad(renderer, x - arm, y - arm, x + arm, y + arm, rest);
  /* The dimple in the middle of the cross. */
  rest.r *= 0.6f;
  rest.g *= 0.6f;
  rest.b *= 0.6f;
  smx_touch_fill_circle(renderer, x, y, arm * 0.55f, rest);
}

/* Three bars, for the menu. */
static void smx_touch_draw_menu(const SmxTouch *touch, SDL_Renderer *renderer) {
  const SmxTouchShape *menu = &touch->shapes[SMX_TOUCH_MENU];
  SDL_FColor bar = {1.0f, 1.0f, 1.0f, 0.85f};
  float w = menu->w * 0.46f;
  float h = menu->w * 0.07f;
  float step = menu->w * 0.15f;
  Sint32 line = -1;

  smx_touch_fill_circle(renderer, menu->x, menu->y, menu->w * 0.5f,
                        smx_touch_color(SMX_TOUCH_MENU, false));
  for (; line <= 1; line += 1) {
    smx_touch_quad(renderer, menu->x - w * 0.5f, menu->y + (float)line * step - h * 0.5f,
                   menu->x + w * 0.5f, menu->y + (float)line * step + h * 0.5f, bar);
  }
}

/* The console's body behind the D-pad and the face buttons, so they show on black too. */
static void smx_touch_draw_body(const SmxTouch *touch, SDL_Renderer *renderer) {
  const SmxTouchShape *pad = &touch->shapes[SMX_TOUCH_DPAD];
  const SmxTouchShape *x = &touch->shapes[SMX_TOUCH_X];
  const SmxTouchShape *a = &touch->shapes[SMX_TOUCH_A];
  SDL_FColor body = {0.84f, 0.84f, 0.87f, SMX_TOUCH_BODY_ALPHA};

  smx_touch_fill_circle(renderer, pad->x, pad->y, pad->w * 0.64f, body);
  smx_touch_fill_circle(renderer, x->x, a->y, a->x - x->x + a->w * 0.62f, body);
}

void smx_touch_draw(const SmxTouch *touch, SDL_Renderer *renderer) {
  Uint16 joy = smx_touch_joy(touch);
  Uint32 control = 0;

  smx_touch_draw_menu(touch, renderer);
  if (!touch->shown) {
    return;
  }
  smx_touch_draw_body(touch, renderer);
  smx_touch_draw_dpad(touch, renderer, joy);
  for (control = SMX_TOUCH_A; control <= SMX_TOUCH_START; control += 1) {
    const SmxTouchShape *shape = &touch->shapes[control];
    bool held = (joy & smx_touch_bits[control]) != 0;
    bool round = control <= SMX_TOUCH_Y;
    smx_touch_fill_capsule(renderer, shape->x, shape->y, shape->w, shape->h,
                           smx_touch_color((SmxTouchControl)control, held));
    smx_touch_text(renderer, shape->x, shape->y, round ? shape->h * 0.42f : shape->h * 0.5f,
                   smx_touch_labels[control], held ? 1.0f : 0.8f);
  }
}
