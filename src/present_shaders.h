#ifndef SMX_PRESENT_SHADERS_H_
#define SMX_PRESENT_SHADERS_H_

#include <SDL3/SDL.h>

/*
 * GLSL 1.50 for an OpenGL 3.2 core context. The vertex shader draws one
 * triangle strip over the viewport from gl_VertexID, so no buffers are bound,
 * and hands the fragment shader its position in picture pixels, row 0 at the top.
 */
static const Uint8 smx_present_vertex_shader[] =
    "#version 150\n"
    "uniform vec2 source_size;\n"
    "out vec2 coord;\n"
    "void main() {\n"
    "  vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "  coord = vec2(corner.x, 1.0 - corner.y) * source_size;\n"
    "  gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/*
 * Each picture pixel is drawn flat and blended with its neighbor only across
 * the output pixel its edge falls in, so non-integer scales stay sharp and
 * even. With `scanlines` at 1, each picture line is a beam whose brightness
 * falls off as a Gaussian from the line's center; bright lines spread wider,
 * as on a CRT. Beams add in linear light. The gain makes up most of the
 * brightness lost between lines. Below a 2x vertical scale the lines would
 * alias, so the effect fades out between 2.5x and 1.5x.
 *
 * An in-between picture of motion interpolation is drawn at whole pixels;
 * `shifts` holds how far past that each row moves, and `shift_y` the
 * vertical fraction for rows `shift_y.y` up to `shift_y.z`. The picture is
 * sampled that far on, so the camera moves by fractions of a pixel. With
 * scanlines, the beams stay put and the picture moves under them.
 */
static const Uint8 smx_present_fragment_shader[] =
    "#version 150\n"
    "uniform sampler2D picture;\n"
    "uniform vec2 source_size;\n"
    "uniform vec2 output_size;\n"
    "uniform float scanlines;\n"
    "uniform sampler2D shifts;\n"
    "uniform vec3 shift_y;\n"
    "in vec2 coord;\n"
    "out vec4 color;\n"
    "float sharp(float x, float scale) {\n"
    "  float base = floor(x);\n"
    "  float d = x - base - 0.5;\n"
    "  float flat_part = max(0.5 - 0.5 / scale, 0.0);\n"
    "  return base + 0.5 + (d - clamp(d, -flat_part, flat_part)) * max(scale, 1.0);\n"
    "}\n"
    "vec3 fetch(float x, float y) {\n"
    "  return pow(texture(picture, vec2(x, y) / source_size).rgb, vec3(2.2));\n"
    "}\n"
    "float beam(float d, vec3 c) {\n"
    "  float width = mix(0.20, 0.32, sqrt(max(max(c.r, c.g), c.b)));\n"
    "  return exp(-0.5 * d * d / (width * width));\n"
    "}\n"
    "float moved_y(float y) {\n"
    "  if (y >= shift_y.y && y < shift_y.z) {\n"
    "    return min(y + shift_y.x, shift_y.z - 0.5);\n"
    "  }\n"
    "  return y;\n"
    "}\n"
    "void main() {\n"
    "  vec2 scale = output_size / source_size;\n"
    "  float my = moved_y(coord.y);\n"
    "  float row = clamp(floor(my), 0.0, source_size.y - 1.0);\n"
    "  float mx = coord.x + texelFetch(shifts, ivec2(int(row), 0), 0).r;\n"
    "  float x = sharp(mx, scale.x);\n"
    "  vec3 plain = fetch(x, sharp(my, scale.y));\n"
    "  float y0 = floor(coord.y - 0.5) + 0.5;\n"
    "  float d = coord.y - y0;\n"
    "  float ya = moved_y(y0);\n"
    "  float yb = moved_y(y0 + 1.0);\n"
    "  vec3 a = fetch(x, ya) * step(0.0, y0);\n"
    "  vec3 b = fetch(x, yb) * step(y0 + 1.0, source_size.y);\n"
    "  vec3 lit = (a * beam(d, a) + b * beam(1.0 - d, b)) * 1.3;\n"
    "  float strength = scanlines * clamp(scale.y - 1.5, 0.0, 1.0);\n"
    "  color = vec4(pow(clamp(mix(plain, lit, strength), 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);\n"
    "}\n";

#endif /* SMX_PRESENT_SHADERS_H_ */
