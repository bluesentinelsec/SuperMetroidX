#include "smx/present.h"

#include "present_shaders.h"

#include <SDL3/SDL_opengl.h>

/* OpenGL 1.1 entry points; SDL_opengl_glext.h types the later ones. */
typedef void(APIENTRY *SmxGlViewport)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void(APIENTRY *SmxGlClearColor)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
typedef void(APIENTRY *SmxGlClear)(GLbitfield mask);
typedef void(APIENTRY *SmxGlGenTextures)(GLsizei n, GLuint *textures);
typedef void(APIENTRY *SmxGlDeleteTextures)(GLsizei n, const GLuint *textures);
typedef void(APIENTRY *SmxGlBindTexture)(GLenum target, GLuint texture);
typedef void(APIENTRY *SmxGlTexParameteri)(GLenum target, GLenum name, GLint value);
typedef void(APIENTRY *SmxGlTexImage2D)(GLenum target, GLint level, GLint format, GLsizei w,
                                        GLsizei h, GLint border, GLenum pixel_format, GLenum type,
                                        const void *pixels);
typedef void(APIENTRY *SmxGlTexSubImage2D)(GLenum target, GLint level, GLint x, GLint y, GLsizei w,
                                           GLsizei h, GLenum format, GLenum type,
                                           const void *pixels);
typedef void(APIENTRY *SmxGlPixelStorei)(GLenum name, GLint value);
typedef void(APIENTRY *SmxGlDrawArrays)(GLenum mode, GLint first, GLsizei count);

struct SmxPresentGl {
  SDL_GLContext context;
  SmxGlViewport Viewport;
  SmxGlClearColor ClearColor;
  SmxGlClear Clear;
  SmxGlGenTextures GenTextures;
  SmxGlDeleteTextures DeleteTextures;
  SmxGlBindTexture BindTexture;
  SmxGlTexParameteri TexParameteri;
  SmxGlTexImage2D TexImage2D;
  SmxGlTexSubImage2D TexSubImage2D;
  SmxGlPixelStorei PixelStorei;
  SmxGlDrawArrays DrawArrays;
  PFNGLACTIVETEXTUREPROC ActiveTexture;
  PFNGLCREATESHADERPROC CreateShader;
  PFNGLSHADERSOURCEPROC ShaderSource;
  PFNGLCOMPILESHADERPROC CompileShader;
  PFNGLGETSHADERIVPROC GetShaderiv;
  PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
  PFNGLDELETESHADERPROC DeleteShader;
  PFNGLCREATEPROGRAMPROC CreateProgram;
  PFNGLATTACHSHADERPROC AttachShader;
  PFNGLLINKPROGRAMPROC LinkProgram;
  PFNGLGETPROGRAMIVPROC GetProgramiv;
  PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
  PFNGLDELETEPROGRAMPROC DeleteProgram;
  PFNGLUSEPROGRAMPROC UseProgram;
  PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
  PFNGLUNIFORM1IPROC Uniform1i;
  PFNGLUNIFORM1FPROC Uniform1f;
  PFNGLUNIFORM2FPROC Uniform2f;
  PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
  PFNGLBINDVERTEXARRAYPROC BindVertexArray;
  PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
  Uint32 texture;
  Uint32 program;
  Uint32 vertex_array;
  Sint32 output_size;
};

/* An NTSC SNES pixel is 8:7 as wide as it is tall. */
#define SMX_PRESENT_TV_WIDTH 8
#define SMX_PRESENT_TV_HEIGHT 7
/* Whole-number scales for a new window, and the share of the display it may cover. */
#define SMX_PRESENT_MAX_SCALE 4
#define SMX_PRESENT_WINDOW_PERCENT 85

void smx_backdrop(Uint8 *red, Uint8 *green, Uint8 *blue) {
  *red = 12;
  *green = 18;
  *blue = 32;
}

void smx_present_defaults(SmxPresentOptions *options) {
#ifdef NDEBUG
  options->window_mode = SMX_WINDOW_BORDERLESS;
#else
  options->window_mode = SMX_WINDOW_WINDOWED;
#endif
  options->fill = true;
  options->tv_aspect = true;
  options->scanlines = true;
}

static bool smx_present_choice(const char *value, const char *on, const char *off, bool *out) {
  if (SDL_strcmp(value, on) == 0) {
    *out = true;
    return true;
  }
  if (SDL_strcmp(value, off) == 0) {
    *out = false;
    return true;
  }
  return false;
}

Sint32 smx_present_option(SmxPresentOptions *options, Sint32 count, char **args) {
  const char *name = args[0];
  const char *value = count > 1 ? args[1] : NULL;
  bool known = false;

  if (SDL_strcmp(name, "--window-mode") == 0) {
    known = true;
    if (value == NULL) {
    } else if (SDL_strcmp(value, "windowed") == 0) {
      options->window_mode = SMX_WINDOW_WINDOWED;
      return 2;
    } else if (SDL_strcmp(value, "borderless") == 0) {
      options->window_mode = SMX_WINDOW_BORDERLESS;
      return 2;
    } else if (SDL_strcmp(value, "exclusive") == 0 || SDL_strcmp(value, "fullscreen") == 0) {
      options->window_mode = SMX_WINDOW_EXCLUSIVE;
      return 2;
    }
  } else if (SDL_strcmp(name, "--scale") == 0) {
    known = true;
    if (value != NULL && smx_present_choice(value, "fit", "integer", &options->fill)) {
      return 2;
    }
  } else if (SDL_strcmp(name, "--aspect") == 0) {
    known = true;
    if (value != NULL && smx_present_choice(value, "tv", "square", &options->tv_aspect)) {
      return 2;
    }
  } else if (SDL_strcmp(name, "--scanlines") == 0) {
    known = true;
    if (value != NULL && smx_present_choice(value, "on", "off", &options->scanlines)) {
      return 2;
    }
  }
  return known ? -1 : 0;
}

void smx_present_usage(void) {
  SmxPresentOptions defaults;

  smx_present_defaults(&defaults);
  SDL_Log("  --window-mode MODE  windowed, borderless (fullscreen at the desktop's mode), or");
  SDL_Log("                      exclusive (fullscreen at its own mode); default %s",
          defaults.window_mode == SMX_WINDOW_WINDOWED ? "windowed" : "borderless");
  SDL_Log("  --scale fit|integer fill the window, or keep whole-number scales; default fit");
  SDL_Log("  --aspect tv|square  8:7 pixels as on an NTSC television, or square; default tv");
  SDL_Log("  --scanlines on|off  CRT scanlines through OpenGL; default on");
}

/* The picture's shape on screen as `num` wide by `den` tall. */
static void smx_present_shape(const SmxPresentOptions *options, Sint32 src_w, Sint32 src_h,
                              Sint64 *num, Sint64 *den) {
  *num = src_w;
  *den = src_h;
  if (options->tv_aspect) {
    *num *= SMX_PRESENT_TV_WIDTH;
    *den *= SMX_PRESENT_TV_HEIGHT;
  }
}

void smx_present_fit(const SmxPresentOptions *options, Sint32 src_w, Sint32 src_h, Sint32 dst_w,
                     Sint32 dst_h, Sint32 *x, Sint32 *y, Sint32 *w, Sint32 *h) {
  Sint64 num = 1;
  Sint64 den = 1;

  if (src_w <= 0 || src_h <= 0) {
    *x = 0;
    *y = 0;
    *w = dst_w;
    *h = dst_h;
    return;
  }
  smx_present_shape(options, src_w, src_h, &num, &den);
  if (options->fill) {
    if ((Sint64)dst_w * den >= (Sint64)dst_h * num) {
      *h = dst_h;
      *w = (Sint32)(((Sint64)dst_h * num * 2 + den) / (den * 2));
    } else {
      *w = dst_w;
      *h = (Sint32)(((Sint64)dst_w * den * 2 + num) / (num * 2));
    }
  } else {
    Sint64 scale = dst_h / src_h;
    Sint64 across = ((Sint64)dst_w * den) / (num * src_h);
    if (across < scale) {
      scale = across;
    }
    if (scale < 1) {
      scale = 1;
    }
    *h = (Sint32)(src_h * scale);
    *w = (Sint32)(((Sint64)*h * num * 2 + den) / (den * 2));
  }
  *x = (dst_w - *w) / 2;
  *y = (dst_h - *h) / 2;
}

void smx_present_window_size(const SmxPresentOptions *options, Sint32 src_w, Sint32 src_h,
                             Sint32 usable_w, Sint32 usable_h, Sint32 *w, Sint32 *h) {
  Sint64 num = 1;
  Sint64 den = 1;
  Sint32 scale = SMX_PRESENT_MAX_SCALE;

  smx_present_shape(options, src_w, src_h, &num, &den);
  for (; scale > 1; scale -= 1) {
    Sint64 height = (Sint64)src_h * scale;
    Sint64 width = (height * num * 2 + den) / (den * 2);
    if (height * 100 <= (Sint64)usable_h * SMX_PRESENT_WINDOW_PERCENT &&
        width * 100 <= (Sint64)usable_w * SMX_PRESENT_WINDOW_PERCENT) {
      break;
    }
  }
  *h = src_h * scale;
  *w = (Sint32)(((Sint64)*h * num * 2 + den) / (den * 2));
}

static bool smx_present_load(struct SmxPresentGl *gl) {
  bool ok = true;

#define SMX_GL_LOAD(field, name)                                                                   \
  do {                                                                                             \
    SDL_FunctionPointer entry = SDL_GL_GetProcAddress(name);                                       \
    SDL_memcpy(&gl->field, &entry, sizeof(entry));                                                 \
    if (entry == NULL) {                                                                           \
      SDL_Log("OpenGL has no %s", name);                                                           \
      ok = false;                                                                                  \
    }                                                                                              \
  } while (0)
  SMX_GL_LOAD(Viewport, "glViewport");
  SMX_GL_LOAD(ClearColor, "glClearColor");
  SMX_GL_LOAD(Clear, "glClear");
  SMX_GL_LOAD(GenTextures, "glGenTextures");
  SMX_GL_LOAD(DeleteTextures, "glDeleteTextures");
  SMX_GL_LOAD(BindTexture, "glBindTexture");
  SMX_GL_LOAD(TexParameteri, "glTexParameteri");
  SMX_GL_LOAD(TexImage2D, "glTexImage2D");
  SMX_GL_LOAD(TexSubImage2D, "glTexSubImage2D");
  SMX_GL_LOAD(PixelStorei, "glPixelStorei");
  SMX_GL_LOAD(DrawArrays, "glDrawArrays");
  SMX_GL_LOAD(ActiveTexture, "glActiveTexture");
  SMX_GL_LOAD(CreateShader, "glCreateShader");
  SMX_GL_LOAD(ShaderSource, "glShaderSource");
  SMX_GL_LOAD(CompileShader, "glCompileShader");
  SMX_GL_LOAD(GetShaderiv, "glGetShaderiv");
  SMX_GL_LOAD(GetShaderInfoLog, "glGetShaderInfoLog");
  SMX_GL_LOAD(DeleteShader, "glDeleteShader");
  SMX_GL_LOAD(CreateProgram, "glCreateProgram");
  SMX_GL_LOAD(AttachShader, "glAttachShader");
  SMX_GL_LOAD(LinkProgram, "glLinkProgram");
  SMX_GL_LOAD(GetProgramiv, "glGetProgramiv");
  SMX_GL_LOAD(GetProgramInfoLog, "glGetProgramInfoLog");
  SMX_GL_LOAD(DeleteProgram, "glDeleteProgram");
  SMX_GL_LOAD(UseProgram, "glUseProgram");
  SMX_GL_LOAD(GetUniformLocation, "glGetUniformLocation");
  SMX_GL_LOAD(Uniform1i, "glUniform1i");
  SMX_GL_LOAD(Uniform1f, "glUniform1f");
  SMX_GL_LOAD(Uniform2f, "glUniform2f");
  SMX_GL_LOAD(GenVertexArrays, "glGenVertexArrays");
  SMX_GL_LOAD(BindVertexArray, "glBindVertexArray");
  SMX_GL_LOAD(DeleteVertexArrays, "glDeleteVertexArrays");
#undef SMX_GL_LOAD
  return ok;
}

static Uint32 smx_present_shader(struct SmxPresentGl *gl, GLenum type, const Uint8 *source) {
  const GLchar *text = (const GLchar *)source;
  GLuint shader = gl->CreateShader(type);
  GLint compiled = GL_FALSE;

  gl->ShaderSource(shader, 1, &text, NULL);
  gl->CompileShader(shader);
  gl->GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled != GL_TRUE) {
    char log[1024];
    gl->GetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
    SDL_Log("shader did not compile: %s", log);
    gl->DeleteShader(shader);
    return 0;
  }
  return (Uint32)shader;
}

static bool smx_present_program(struct SmxPresentGl *gl, Sint32 width, Sint32 height) {
  Uint32 vertex = smx_present_shader(gl, GL_VERTEX_SHADER, smx_present_vertex_shader);
  Uint32 fragment = smx_present_shader(gl, GL_FRAGMENT_SHADER, smx_present_fragment_shader);
  GLint linked = GL_FALSE;
  GLuint program = 0;

  if (vertex == 0 || fragment == 0) {
    if (vertex != 0) {
      gl->DeleteShader((GLuint)vertex);
    }
    if (fragment != 0) {
      gl->DeleteShader((GLuint)fragment);
    }
    return false;
  }
  program = gl->CreateProgram();
  gl->AttachShader(program, (GLuint)vertex);
  gl->AttachShader(program, (GLuint)fragment);
  gl->LinkProgram(program);
  gl->DeleteShader((GLuint)vertex);
  gl->DeleteShader((GLuint)fragment);
  gl->GetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    char log[1024];
    gl->GetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
    SDL_Log("shaders did not link: %s", log);
    gl->DeleteProgram(program);
    return false;
  }
  gl->program = (Uint32)program;
  gl->UseProgram(program);
  gl->Uniform1i(gl->GetUniformLocation(program, "picture"), 0);
  gl->Uniform2f(gl->GetUniformLocation(program, "source_size"), (GLfloat)width, (GLfloat)height);
  gl->output_size = (Sint32)gl->GetUniformLocation(program, "output_size");
  return true;
}

static bool smx_present_open_gl(SmxPresenter *presenter) {
  struct SmxPresentGl *gl = (struct SmxPresentGl *)SDL_calloc(1, sizeof(*gl));
  GLuint texture = 0;
  GLuint vertex_array = 0;

  if (gl == NULL) {
    return false;
  }
  gl->context = SDL_GL_CreateContext(presenter->window);
  if (gl->context == NULL) {
    SDL_Log("OpenGL 3.2 unavailable: %s", SDL_GetError());
    SDL_free(gl);
    return false;
  }
  presenter->gl = gl;
  if (!smx_present_load(gl) || !smx_present_program(gl, presenter->width, presenter->height)) {
    return false;
  }
  /* Vsync when the driver allows it; adaptive first, so a late frame tears instead of waiting. */
  if (!SDL_GL_SetSwapInterval(-1)) {
    SDL_GL_SetSwapInterval(1);
  }
  gl->Uniform1f(gl->GetUniformLocation((GLuint)gl->program, "scanlines"),
                presenter->options.scanlines ? 1.0f : 0.0f);
  gl->GenVertexArrays(1, &vertex_array);
  gl->BindVertexArray(vertex_array);
  gl->vertex_array = (Uint32)vertex_array;
  gl->GenTextures(1, &texture);
  gl->ActiveTexture(GL_TEXTURE0);
  gl->BindTexture(GL_TEXTURE_2D, texture);
  gl->texture = (Uint32)texture;
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)presenter->width, (GLsizei)presenter->height,
                 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
  gl->PixelStorei(GL_UNPACK_ALIGNMENT, 4);
  return true;
}

static void smx_present_close_gl(struct SmxPresentGl *gl) {
  if (gl->program != 0) {
    gl->DeleteProgram((GLuint)gl->program);
  }
  if (gl->texture != 0) {
    GLuint texture = (GLuint)gl->texture;
    gl->DeleteTextures(1, &texture);
  }
  if (gl->vertex_array != 0) {
    GLuint vertex_array = (GLuint)gl->vertex_array;
    gl->DeleteVertexArrays(1, &vertex_array);
  }
  SDL_GL_DestroyContext(gl->context);
  SDL_free(gl);
}

static bool smx_present_open_renderer(SmxPresenter *presenter) {
  presenter->renderer = SDL_CreateRenderer(presenter->window, NULL);
  if (presenter->renderer == NULL) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return false;
  }
  presenter->texture =
      SDL_CreateTexture(presenter->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                        presenter->width, presenter->height);
  if (presenter->texture == NULL) {
    SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
    return false;
  }
  SDL_SetTextureScaleMode(presenter->texture, SDL_SCALEMODE_PIXELART);
  SDL_SetRenderVSync(presenter->renderer, 1);
  return true;
}

/* Enter the fullscreen kind in `mode`, or leave fullscreen for WINDOWED. */
static void smx_present_window_mode(SmxPresenter *presenter, SmxWindowMode mode) {
  SDL_Window *window = presenter->window;

  if (mode == SMX_WINDOW_WINDOWED) {
    SDL_SetWindowFullscreen(window, false);
    return;
  }
  if (mode == SMX_WINDOW_EXCLUSIVE) {
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
    SDL_DisplayMode exclusive;
    if (desktop != NULL && SDL_GetClosestFullscreenDisplayMode(display, desktop->w, desktop->h,
                                                               60.0f, true, &exclusive)) {
      SDL_SetWindowFullscreenMode(window, &exclusive);
    } else {
      SDL_Log("no exclusive fullscreen mode; using borderless");
      SDL_SetWindowFullscreenMode(window, NULL);
    }
  } else {
    SDL_SetWindowFullscreenMode(window, NULL);
  }
  SDL_SetWindowFullscreen(window, true);
}

bool smx_present_open(SmxPresenter *presenter, const char *title, Sint32 width, Sint32 height,
                      const SmxPresentOptions *options) {
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
  SDL_Rect usable = {0, 0, 1280, 720};
  Sint32 window_w = 0;
  Sint32 window_h = 0;

  SDL_memset(presenter, 0, sizeof(*presenter));
  presenter->options = *options;
  presenter->width = width;
  presenter->height = height;
  SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
  smx_present_window_size(options, width, height, (Sint32)usable.w, (Sint32)usable.h, &window_w,
                          &window_h);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  presenter->window =
      SDL_CreateWindow(title, (int)window_w, (int)window_h, flags | SDL_WINDOW_OPENGL);
  if (presenter->window != NULL && !smx_present_open_gl(presenter)) {
    if (presenter->gl != NULL) {
      smx_present_close_gl(presenter->gl);
      presenter->gl = NULL;
    }
    SDL_DestroyWindow(presenter->window);
    presenter->window = NULL;
  }
  if (presenter->window == NULL) {
    SDL_Log("presenting without OpenGL, so without scanlines");
    presenter->window = SDL_CreateWindow(title, (int)window_w, (int)window_h, flags);
    if (presenter->window == NULL) {
      SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
      return false;
    }
    if (!smx_present_open_renderer(presenter)) {
      smx_present_close(presenter);
      return false;
    }
  }
  smx_present_window_mode(presenter, options->window_mode);
  SDL_ShowWindow(presenter->window);
  return true;
}

void smx_present_frame(SmxPresenter *presenter, const Uint8 *pixels) {
  struct SmxPresentGl *gl = presenter->gl;
  int out_w = 0;
  int out_h = 0;
  Sint32 x = 0;
  Sint32 y = 0;
  Sint32 w = 0;
  Sint32 h = 0;
  Uint8 red = 0;
  Uint8 green = 0;
  Uint8 blue = 0;

  if (pixels == NULL) {
    smx_backdrop(&red, &green, &blue);
  }
  SDL_GetWindowSizeInPixels(presenter->window, &out_w, &out_h);
  smx_present_fit(&presenter->options, presenter->width, presenter->height, (Sint32)out_w,
                  (Sint32)out_h, &x, &y, &w, &h);
  if (gl == NULL) {
    SDL_FRect dst;
    SDL_SetRenderDrawColor(presenter->renderer, red, green, blue, 255);
    SDL_RenderClear(presenter->renderer);
    if (pixels != NULL) {
      SDL_UpdateTexture(presenter->texture, NULL, pixels, (int)(presenter->width * 4));
      dst.x = (float)x;
      dst.y = (float)y;
      dst.w = (float)w;
      dst.h = (float)h;
      SDL_RenderTexture(presenter->renderer, presenter->texture, NULL, &dst);
    }
    SDL_RenderPresent(presenter->renderer);
    return;
  }
  gl->Viewport(0, 0, (GLsizei)out_w, (GLsizei)out_h);
  gl->ClearColor((GLfloat)red / 255.0f, (GLfloat)green / 255.0f, (GLfloat)blue / 255.0f, 1.0f);
  gl->Clear(GL_COLOR_BUFFER_BIT);
  if (pixels != NULL) {
    gl->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)presenter->width, (GLsizei)presenter->height,
                      GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
    /* OpenGL counts viewport rows from the bottom. */
    gl->Viewport((GLint)x, (GLint)(out_h - y - h), (GLsizei)w, (GLsizei)h);
    gl->Uniform2f((GLint)gl->output_size, (GLfloat)w, (GLfloat)h);
    gl->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
  SDL_GL_SwapWindow(presenter->window);
}

void smx_present_toggle_fullscreen(SmxPresenter *presenter) {
  SmxWindowMode mode = presenter->options.window_mode;

  if ((SDL_GetWindowFlags(presenter->window) & SDL_WINDOW_FULLSCREEN) != 0) {
    smx_present_window_mode(presenter, SMX_WINDOW_WINDOWED);
  } else {
    smx_present_window_mode(presenter, mode == SMX_WINDOW_WINDOWED ? SMX_WINDOW_BORDERLESS : mode);
  }
}

void smx_present_event(SmxPresenter *presenter, const SDL_Event *event) {
  (void)presenter;
  if (event->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
    SDL_HideCursor();
  } else if (event->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
    SDL_ShowCursor();
  }
}

void smx_present_close(SmxPresenter *presenter) {
  if (presenter->gl != NULL) {
    smx_present_close_gl(presenter->gl);
  }
  if (presenter->texture != NULL) {
    SDL_DestroyTexture(presenter->texture);
  }
  if (presenter->renderer != NULL) {
    SDL_DestroyRenderer(presenter->renderer);
  }
  if (presenter->window != NULL) {
    SDL_DestroyWindow(presenter->window);
  }
  SDL_memset(presenter, 0, sizeof(*presenter));
}
