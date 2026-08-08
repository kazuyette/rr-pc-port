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
