/* rr-pc-port -- phase 3, minimal software rasterizer implementation.
 * See gpu_soft.h for the module's scope/intent.
 */
#include "gpu_soft.h"

uint32_t gpu_framebuffer[GPU_FB_WIDTH * GPU_FB_HEIGHT];

void gpu_clear(uint32_t color) {
    int i;
    for (i = 0; i < GPU_FB_WIDTH * GPU_FB_HEIGHT; i++) {
        gpu_framebuffer[i] = color;
    }
}

static int min3(int a, int b, int c) {
    int m = a;
    if (b < m) m = b;
    if (c < m) m = c;
    return m;
}

static int max3(int a, int b, int c) {
    int m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}

/* Signed area x2 of the triangle (px0,py0)-(px1,py1)-(px2,py2). Also
 * used as the edge function for barycentric/inside tests. */
static int edge_fn(int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

void gpu_draw_triangle_flat(int x0, int y0, int x1, int y1, int x2, int y2,
                             uint32_t color) {
    int minx, maxx, miny, maxy;
    int area;
    int x, y;

    minx = min3(x0, x1, x2);
    maxx = max3(x0, x1, x2);
    miny = min3(y0, y1, y2);
    maxy = max3(y0, y1, y2);

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > GPU_FB_WIDTH - 1) maxx = GPU_FB_WIDTH - 1;
    if (maxy > GPU_FB_HEIGHT - 1) maxy = GPU_FB_HEIGHT - 1;

    area = edge_fn(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return; /* degenerate (zero-area) triangle, nothing to draw */
    }

    /* Standard barycentric scanline fill: for every pixel in the
     * bounding box, test which side of all three edges it's on. Works
     * for either vertex winding by checking sign consistency against
     * the overall triangle area rather than assuming CW/CCW. */
    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            int px = x, py = y;
            int w0 = edge_fn(x1, y1, x2, y2, px, py);
            int w1 = edge_fn(x2, y2, x0, y0, px, py);
            int w2 = edge_fn(x0, y0, x1, y1, px, py);

            int inside;
            if (area > 0) {
                inside = (w0 >= 0) && (w1 >= 0) && (w2 >= 0);
            } else {
                inside = (w0 <= 0) && (w1 <= 0) && (w2 <= 0);
            }

            if (inside) {
                gpu_framebuffer[y * GPU_FB_WIDTH + x] = color;
            }
        }
    }
}

static int clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static uint32_t pack_rgb(int r, int g, int b) {
    return ((uint32_t)clamp_u8(r) << 16) | ((uint32_t)clamp_u8(g) << 8) | (uint32_t)clamp_u8(b);
}

void gpu_draw_triangle_gouraud(int x0, int y0, int x1, int y1, int x2, int y2,
                                uint32_t color0, uint32_t color1, uint32_t color2) {
    int minx, maxx, miny, maxy;
    int area;
    int x, y;
    int r0, g0, b0, r1, g1, b1, r2, g2, b2;

    minx = min3(x0, x1, x2);
    maxx = max3(x0, x1, x2);
    miny = min3(y0, y1, y2);
    maxy = max3(y0, y1, y2);

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > GPU_FB_WIDTH - 1) maxx = GPU_FB_WIDTH - 1;
    if (maxy > GPU_FB_HEIGHT - 1) maxy = GPU_FB_HEIGHT - 1;

    area = edge_fn(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return; /* degenerate (zero-area) triangle, nothing to draw */
    }

    r0 = (int)((color0 >> 16) & 0xFF); g0 = (int)((color0 >> 8) & 0xFF); b0 = (int)(color0 & 0xFF);
    r1 = (int)((color1 >> 16) & 0xFF); g1 = (int)((color1 >> 8) & 0xFF); b1 = (int)(color1 & 0xFF);
    r2 = (int)((color2 >> 16) & 0xFF); g2 = (int)((color2 >> 8) & 0xFF); b2 = (int)(color2 & 0xFF);

    /* Same scanline structure as gpu_draw_triangle_flat: w0/w1/w2 are
     * the edge-function values used for the inside test, and are also
     * (unnormalized) barycentric weights for vertices 0/1/2
     * respectively -- w0 is the weight opposite vertex 0 (i.e. it's
     * the coefficient for color0), etc, and w0+w1+w2 == area always.
     * Dividing each by `area` (same sign as the w's for inside pixels)
     * gives the actual [0,1] barycentric coordinate to blend colors
     * with -- integer math throughout, correctness over speed. */
    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            int px = x, py = y;
            int w0 = edge_fn(x1, y1, x2, y2, px, py);
            int w1 = edge_fn(x2, y2, x0, y0, px, py);
            int w2 = edge_fn(x0, y0, x1, y1, px, py);

            int inside;
            if (area > 0) {
                inside = (w0 >= 0) && (w1 >= 0) && (w2 >= 0);
            } else {
                inside = (w0 <= 0) && (w1 <= 0) && (w2 <= 0);
            }

            if (inside) {
                int r = (r0 * w0 + r1 * w1 + r2 * w2) / area;
                int g = (g0 * w0 + g1 * w1 + g2 * w2) / area;
                int b = (b0 * w0 + b1 * w1 + b2 * w2) / area;
                gpu_framebuffer[y * GPU_FB_WIDTH + x] = pack_rgb(r, g, b);
            }
        }
    }
}

void gpu_draw_quad_flat(int x0, int y0, int x1, int y1,
                         int x2, int y2, int x3, int y3,
                         uint32_t color) {
    gpu_draw_triangle_flat(x0, y0, x1, y1, x2, y2, color);
    gpu_draw_triangle_flat(x0, y0, x2, y2, x3, y3, color);
}

void gpu_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    /* Standard integer Bresenham, symmetric in all 8 octants. */
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;

    for (;;) {
        if (x >= 0 && x < GPU_FB_WIDTH && y >= 0 && y < GPU_FB_HEIGHT) {
            gpu_framebuffer[y * GPU_FB_WIDTH + x] = color;
        }
        if (x == x1 && y == y1) break;
        {
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x += sx; }
            if (e2 < dx)  { err += dx; y += sy; }
        }
    }
}
