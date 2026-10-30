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
 */
static const Uint8 smx_present_fragment_shader[] =
    "#version 150\n"
    "uniform sampler2D picture;\n"
    "uniform vec2 source_size;\n"
    "uniform vec2 output_size;\n"
    "uniform float scanlines;\n"
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
    "void main() {\n"
    "  vec2 scale = output_size / source_size;\n"
    "  float x = sharp(coord.x, scale.x);\n"
    "  vec3 plain = fetch(x, sharp(coord.y, scale.y));\n"
    "  float y0 = floor(coord.y - 0.5) + 0.5;\n"
    "  float d = coord.y - y0;\n"
    "  vec3 a = fetch(x, y0) * step(0.0, y0);\n"
    "  vec3 b = fetch(x, y0 + 1.0) * step(y0 + 1.0, source_size.y);\n"
    "  vec3 lit = (a * beam(d, a) + b * beam(1.0 - d, b)) * 1.3;\n"
    "  float strength = scanlines * clamp(scale.y - 1.5, 0.0, 1.0);\n"
    "  color = vec4(pow(clamp(mix(plain, lit, strength), 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);\n"
    "}\n";

/*
 * The same picture in Metal Shading Language, for SDL's GPU renderer where
 * there is no desktop OpenGL, such as on iOS. SDL compiles it when the game
 * starts. Its inputs are those of SDL's own textured-triangle vertex shader:
 * the vertex color, then texture coordinates from 0 to 1. The picture is
 * texture and sampler 0; the uniforms are buffer 0.
 */
static const Uint8 smx_present_metal_shader[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Picture { float2 source_size; float2 output_size; float scanlines; };\n"
    "struct main0_in {\n"
    "  float4 color [[user(locn0)]];\n"
    "  float2 uv [[user(locn1)]];\n"
    "};\n"
    "struct main0_out { float4 color [[color(0)]]; };\n"
    "static float sharp(float x, float scale) {\n"
    "  float base = floor(x);\n"
    "  float d = x - base - 0.5;\n"
    "  float flat_part = max(0.5 - 0.5 / scale, 0.0);\n"
    "  return base + 0.5 + (d - clamp(d, -flat_part, flat_part)) * max(scale, 1.0);\n"
    "}\n"
    "static float3 fetch(texture2d<float> picture, sampler pixels, float2 size, float x, float y) "
    "{\n"
    "  return pow(picture.sample(pixels, float2(x, y) / size).rgb, float3(2.2));\n"
    "}\n"
    "static float beam(float d, float3 c) {\n"
    "  float width = mix(0.20, 0.32, sqrt(max(max(c.r, c.g), c.b)));\n"
    "  return exp(-0.5 * d * d / (width * width));\n"
    "}\n"
    "fragment main0_out main0(main0_in in [[stage_in]], constant Picture &p [[buffer(0)]],\n"
    "                         texture2d<float> picture [[texture(0)]],\n"
    "                         sampler pixels [[sampler(0)]]) {\n"
    "  float2 coord = in.uv * p.source_size;\n"
    "  float2 scale = p.output_size / p.source_size;\n"
    "  float x = sharp(coord.x, scale.x);\n"
    "  float3 plain = fetch(picture, pixels, p.source_size, x, sharp(coord.y, scale.y));\n"
    "  float y0 = floor(coord.y - 0.5) + 0.5;\n"
    "  float d = coord.y - y0;\n"
    "  float3 a = fetch(picture, pixels, p.source_size, x, y0) * step(0.0, y0);\n"
    "  float3 b = fetch(picture, pixels, p.source_size, x, y0 + 1.0) *\n"
    "             step(y0 + 1.0, p.source_size.y);\n"
    "  float3 lit = (a * beam(d, a) + b * beam(1.0 - d, b)) * 1.3;\n"
    "  float strength = p.scanlines * clamp(scale.y - 1.5, 0.0, 1.0);\n"
    "  main0_out out;\n"
    "  out.color = float4(pow(clamp(mix(plain, lit, strength), 0.0, 1.0), float3(1.0 / 2.2)), "
    "1.0);\n"
    "  return out;\n"
    "}\n";

#endif /* SMX_PRESENT_SHADERS_H_ */
