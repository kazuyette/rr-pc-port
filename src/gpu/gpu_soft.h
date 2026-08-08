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

#endif /* RR_PC_PORT_GPU_SOFT_H */
