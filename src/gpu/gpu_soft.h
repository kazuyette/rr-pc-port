/* rr-pc-port -- phase 3, minimal software rasterizer.
 *
 * This module is the beginning of a from-scratch PS1 GPU emulation: a
 * flat RGB framebuffer plus primitive-drawing routines shaped like the
 * real PS1 GPU's primitive packets (flat-shaded triangle first; gouraud,
 * textured, sprite, and line primitives are Phase 3 follow-ups -- see
 * ROADMAP.md).
 *
 * Deliberately free of any windowing/SDL dependency: this file only
 * touches gpu_framebuffer[], so it can be exercised headless (unit
 * tests, CI) independent of whatever presentation backend (SDL2 today,
 * maybe GL later) blits it to a real screen. main.c owns the SDL2
 * texture-blit step; this module never calls into SDL.
 */
#ifndef RR_PC_PORT_GPU_SOFT_H
#define RR_PC_PORT_GPU_SOFT_H

#include <stdint.h>

#define GPU_FB_WIDTH  320
#define GPU_FB_HEIGHT 240

/* Packed 0x00RRGGBB (top byte unused/zero), row-major, origin top-left --
 * matches what SDL_UpdateTexture with SDL_PIXELFORMAT_RGB888 expects. */
extern uint32_t gpu_framebuffer[GPU_FB_WIDTH * GPU_FB_HEIGHT];

/* Fill the whole framebuffer with a single color. */
void gpu_clear(uint32_t color);

/* Real PS1 GPU primitive coordinates are integer screen-space (the GPU
 * has no floating point). Flat-shaded triangle: one solid color, no
 * per-vertex color/UV yet -- see ROADMAP.md Phase 3 for gouraud/textured
 * follow-ups. Standard scanline fill, correctness over speed. */
void gpu_draw_triangle_flat(int x0, int y0, int x1, int y1, int x2, int y2,
                             uint32_t color);

/* Gouraud-shaded triangle: one packed 0x00RRGGBB color per vertex,
 * linearly interpolated per-pixel across the triangle interior using
 * the same barycentric edge-function weights gpu_draw_triangle_flat
 * uses for its inside test. This is a real PS1 GPU primitive type
 * (the hardware natively draws gouraud-shaded polygons), not a demo
 * gimmick -- the eventual GPU-packet dispatcher routes straight here
 * for POLY_G3/G4 packets. */
void gpu_draw_triangle_gouraud(int x0, int y0, int x1, int y1, int x2, int y2,
                                uint32_t color0, uint32_t color1, uint32_t color2);

/* Flat-shaded quad, built from two triangles sharing the (x0,y0)-(x2,y2)
 * diagonal: (v0,v1,v2) and (v0,v2,v3). Vertices are expected in order
 * around the quad's perimeter (matches the PS1 GPU's POLY_F4 packet
 * shape) -- useful groundwork for wiring in real MAP.RRM road quads
 * later, since that record format is quad-shaped per-section geometry. */
void gpu_draw_quad_flat(int x0, int y0, int x1, int y1,
                         int x2, int y2, int x3, int y3,
                         uint32_t color);

/* Single-pixel line from (x0,y0) to (x1,y1), integer Bresenham --
 * matches the PS1 GPU's flat-shaded LINE_F2 primitive. Handles all
 * octants/slopes including vertical, horizontal, and single-point
 * (x0==x1 && y0==y1) lines. */
void gpu_draw_line(int x0, int y0, int x1, int y1, uint32_t color);

/* Texture-mapped quad, built from two triangles sharing the
 * (x0,y0)-(x2,y2) diagonal, same vertex ordering convention as
 * gpu_draw_quad_flat -- (v0,v1,v2) and (v0,v2,v3), vertices in order
 * around the quad's perimeter.
 *
 * (u,v) are normalized texture coordinates in [0,1] per vertex,
 * interpolated per-pixel with the same integer barycentric weights
 * gpu_draw_triangle_gouraud uses for vertex colors (simple affine
 * interpolation, NOT perspective-correct -- fine for this proof-of-
 * concept, real PS1-accurate perspective correction is a later phase).
 * Sampling is nearest-neighbor (no bilinear filtering).
 *
 * `texture_rgba` is a tex_w*tex_h array of RGBA8888 texels packed as
 * TIM_RGBA(r,g,b,a) (see tools/texparse/tim.h) -- R in the lowest
 * byte. This module doesn't depend on tim.h/tim.c (stays dependency-
 * free like the rest of gpu_soft), so the packing is just documented
 * here to match; any RGBA8888 buffer in that byte order works,
 * texture decoding is entirely the caller's concern.
 *
 * Texels with alpha == 0 are skipped (not written to the framebuffer)
 * -- simple binary transparency, matching TIM's occasional "raw 0x0000
 * == transparent" convention if a caller chooses to encode it that
 * way; texels with alpha > 0 are drawn fully opaque (no blending),
 * consistent with this rasterizer having no alpha-blend support
 * elsewhere. Out-of-range UV coordinates are clamped to the texture's
 * valid pixel range rather than wrapping or sampling out of bounds. */
void gpu_draw_quad_textured(int x0, int y0, float u0, float v0,
                             int x1, int y1, float u1, float v1,
                             int x2, int y2, float u2, float v2,
                             int x3, int y3, float u3, float v3,
                             const uint32_t *texture_rgba, int tex_w, int tex_h);

#endif /* RR_PC_PORT_GPU_SOFT_H */
