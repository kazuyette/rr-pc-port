/* rr-pc-port -- phase 3, headless sanity check for the software
 * rasterizer. No SDL/display needed: exercises gpu_draw_triangle_flat(),
 * gpu_draw_triangle_gouraud(), gpu_draw_line(), and gpu_draw_quad_flat()
 * directly against the raw framebuffer array and asserts pixel colors,
 * so the rasterizer logic itself is provable correct in CI/sandboxes
 * with no display (see ROADMAP.md Phase 3 constraints).
 *
 * Built as its own small executable (rr_pc_port_gpu_test) by CMake, run
 * separately from the main SDL binary. Exit code 0 = all checks passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include "gpu_soft.h"

static int failures = 0;

static void expect_pixel(int x, int y, uint32_t expected, const char *what) {
    uint32_t got = gpu_framebuffer[y * GPU_FB_WIDTH + x];
    if (got != expected) {
        printf("FAIL: %s at (%d,%d): got 0x%06X, expected 0x%06X\n",
               what, x, y, got, expected);
        failures++;
    } else {
        printf("ok:   %s at (%d,%d) == 0x%06X\n", what, x, y, got);
    }
}

int main(void) {
    printf("-- gpu_soft headless sanity check --\n");

    /* 1. gpu_clear fills every pixel. */
    gpu_clear(0x00202020);
    expect_pixel(0, 0, 0x00202020, "clear top-left");
    expect_pixel(GPU_FB_WIDTH - 1, GPU_FB_HEIGHT - 1, 0x00202020, "clear bottom-right");
    expect_pixel(GPU_FB_WIDTH / 2, GPU_FB_HEIGHT / 2, 0x00202020, "clear center");

    /* 2. Draw a triangle with a known, easy-to-reason-about shape: a
     * right triangle with legs on the axes, so interior/exterior points
     * are trivial to predict by hand.
     *   (20,20) -- (20,100) -- (100,100)
     * Interior test point: (40, 90) is inside (below the hypotenuse,
     * within both legs). Exterior test points: (10,10) is outside the
     * bounding box entirely; (90, 30) is inside the bounding box but
     * above the hypotenuse (outside the triangle). */
    gpu_clear(0x00000000);
    gpu_draw_triangle_flat(20, 20, 20, 100, 100, 100, 0x00FF0000);

    expect_pixel(21, 99, 0x00FF0000, "inside near bottom-left corner");
    expect_pixel(40, 90, 0x00FF0000, "inside, well within hypotenuse");
    expect_pixel(90, 99, 0x00FF0000, "inside near bottom-right corner");

    expect_pixel(10, 10, 0x00000000, "outside bounding box entirely");
    expect_pixel(90, 30, 0x00000000, "inside bbox, above hypotenuse");
    expect_pixel(150, 150, 0x00000000, "far outside");

    /* 3. A second, non-overlapping triangle must not disturb the first
     * (proves gpu_draw_triangle_flat doesn't touch pixels outside its
     * own bounds). */
    gpu_draw_triangle_flat(150, 20, 150, 60, 190, 60, 0x0000FF00);
    expect_pixel(160, 55, 0x0000FF00, "second triangle interior");
    expect_pixel(40, 90, 0x00FF0000, "first triangle untouched by second");

    /* 4. Degenerate (zero-area) triangle must be a safe no-op, not a
     * crash or a stray pixel write. */
    gpu_clear(0x00123456);
    gpu_draw_triangle_flat(5, 5, 5, 5, 5, 5, 0x00FFFFFF);
    expect_pixel(5, 5, 0x00123456, "degenerate triangle is a no-op");

    /* 5. Gouraud triangle: near-pure vertex colors close to each
     * vertex, a pure-red/pure-green/pure-blue triangle so the blend at
     * any interior point is easy to reason about by hand. Right
     * triangle again for a simple bounding box:
     *   v0=(20,20) red, v1=(20,120) green, v2=(120,120) blue.
     * One pixel in from each vertex (to stay inside the fill) should
     * be dominated by that vertex's channel (>=240/255) with the other
     * two channels small -- not bit-exact (barycentric weight isn't
     * exactly 1 one pixel off the vertex), so check dominance/near-zero
     * with a small tolerance rather than an exact color match. */
    gpu_clear(0x00000000);
    gpu_draw_triangle_gouraud(20, 20, 20, 120, 120, 120,
                               0x00FF0000, 0x0000FF00, 0x000000FF);
    {
        struct { int x, y; const char *what; int channel; /* 0=R,1=G,2=B */ } corners[3] = {
            {21, 21, "gouraud near v0, dominated by red", 0},
            {21, 119, "gouraud near v1, dominated by green", 1},
            {119, 119, "gouraud near v2, dominated by blue", 2},
        };
        int i;
        for (i = 0; i < 3; i++) {
            uint32_t px = gpu_framebuffer[corners[i].y * GPU_FB_WIDTH + corners[i].x];
            int r = (int)((px >> 16) & 0xFF);
            int g = (int)((px >> 8) & 0xFF);
            int b = (int)(px & 0xFF);
            int ch[3]; ch[0] = r; ch[1] = g; ch[2] = b;
            int dom = ch[corners[i].channel];
            int others_ok = 1, j;
            for (j = 0; j < 3; j++) {
                if (j != corners[i].channel && ch[j] > 15) others_ok = 0;
            }
            if (dom >= 240 && others_ok) {
                printf("ok:   %s at (%d,%d) == 0x%06X\n", corners[i].what, corners[i].x, corners[i].y, px);
            } else {
                printf("FAIL: %s at (%d,%d): got 0x%06X, expected dominant channel >=240 and others <=15\n",
                       corners[i].what, corners[i].x, corners[i].y, px);
                failures++;
            }
        }
    }
    /* Midpoint of the v1-v2 edge (20,120)-(120,120) is (70,120)
     * (clamped to y=119 to stay inside the fill): barycentric weight
     * for v0 is ~0, so it should be an even green/blue blend, no red. */
    {
        uint32_t mid = gpu_framebuffer[119 * GPU_FB_WIDTH + 70];
        uint32_t midR = (mid >> 16) & 0xFF;
        uint32_t midG = (mid >> 8) & 0xFF;
        uint32_t midB = mid & 0xFF;
        if (midR > 20 || midG < 100 || midB < 100) {
            printf("FAIL: gouraud v1-v2 midpoint at (70,119): got 0x%06X, "
                   "expected near-even green/blue blend with ~no red\n", mid);
            failures++;
        } else {
            printf("ok:   gouraud v1-v2 midpoint at (70,119) == 0x%06X "
                   "(near-even green/blue blend, ~no red)\n", mid);
        }
    }

    /* 6. Gouraud with a degenerate (zero-area) triangle must also be a
     * safe no-op, same as the flat-triangle case. */
    gpu_clear(0x00123456);
    gpu_draw_triangle_gouraud(5, 5, 5, 5, 5, 5, 0x00FF0000, 0x0000FF00, 0x000000FF);
    expect_pixel(5, 5, 0x00123456, "degenerate gouraud triangle is a no-op");

    /* 7. gpu_draw_line: horizontal, vertical, and diagonal cases,
     * checking endpoints and a midpoint. Bresenham should hit every
     * pixel on axis-aligned/45-degree lines exactly. */
    gpu_clear(0x00000000);
    gpu_draw_line(10, 50, 60, 50, 0x00FFFFFF); /* horizontal */
    expect_pixel(10, 50, 0x00FFFFFF, "horizontal line start");
    expect_pixel(35, 50, 0x00FFFFFF, "horizontal line midpoint");
    expect_pixel(60, 50, 0x00FFFFFF, "horizontal line end");
    expect_pixel(35, 51, 0x00000000, "just off the horizontal line");

    gpu_draw_line(80, 10, 80, 60, 0x00FFFFFF); /* vertical */
    expect_pixel(80, 10, 0x00FFFFFF, "vertical line start");
    expect_pixel(80, 35, 0x00FFFFFF, "vertical line midpoint");
    expect_pixel(80, 60, 0x00FFFFFF, "vertical line end");

    gpu_draw_line(100, 100, 120, 120, 0x00FFFFFF); /* 45-degree diagonal */
    expect_pixel(100, 100, 0x00FFFFFF, "diagonal line start");
    expect_pixel(110, 110, 0x00FFFFFF, "diagonal line midpoint");
    expect_pixel(120, 120, 0x00FFFFFF, "diagonal line end");

    /* Single-point line (both endpoints equal) must draw exactly one
     * pixel, not loop forever. */
    gpu_draw_line(200, 200, 200, 200, 0x00ABCDEF);
    expect_pixel(200, 200, 0x00ABCDEF, "single-point line draws one pixel");

    /* 8. gpu_draw_quad_flat: two triangles sharing the (0,2) diagonal
     * should fill the whole quad, corners and center included. Simple
     * axis-aligned square for an easy hand check. */
    gpu_clear(0x00000000);
    gpu_draw_quad_flat(30, 30, 90, 30, 90, 90, 30, 90, 0x0012AB34);
    expect_pixel(30, 30, 0x0012AB34, "quad corner v0");
    expect_pixel(89, 30, 0x0012AB34, "quad corner v1 (edge-adjusted)");
    expect_pixel(89, 89, 0x0012AB34, "quad corner v2 (edge-adjusted)");
    expect_pixel(30, 89, 0x0012AB34, "quad corner v3 (edge-adjusted)");
    expect_pixel(60, 60, 0x0012AB34, "quad center");
    expect_pixel(10, 10, 0x00000000, "outside the quad");

    /* 9. gpu_draw_quad_textured: a small synthetic 4x4 checkerboard
     * texture (alternating red/blue 2x2 quadrants, RGBA8888 packed the
     * same way tools/texparse/tim.c's TIM_RGBA macro does -- R in the
     * lowest byte -- without depending on tim.h, since gpu_soft stays
     * dependency-free), mapped onto an axis-aligned square the same
     * shape as the gpu_draw_quad_flat test above so UV<->screen space
     * is easy to reason about by hand: u=[0,1] maps left-to-right,
     * v=[0,1] maps top-to-bottom, exactly onto the quad's x/y range. */
    gpu_clear(0x00000000);
    {
        /* clang-format off */
        static const uint32_t checker[4 * 4] = {
            /* row0: red | red | blue | blue */
            0x000000FFu | (0xFFu << 24), 0x000000FFu | (0xFFu << 24), 0x00FF0000u | (0xFFu << 24), 0x00FF0000u | (0xFFu << 24),
            0x000000FFu | (0xFFu << 24), 0x000000FFu | (0xFFu << 24), 0x00FF0000u | (0xFFu << 24), 0x00FF0000u | (0xFFu << 24),
            0x00FF0000u | (0xFFu << 24), 0x00FF0000u | (0xFFu << 24), 0x000000FFu | (0xFFu << 24), 0x000000FFu | (0xFFu << 24),
            0x00FF0000u | (0xFFu << 24), 0x00FF0000u | (0xFFu << 24), 0x000000FFu | (0xFFu << 24), 0x000000FFu | (0xFFu << 24),
        };
        /* clang-format on */
        /* checker[] values above are TIM_RGBA(r,g,b,255) written out by
         * hand: TIM_RGBA(255,0,0,255) (red) == 0x000000FFu|(0xFFu<<24)
         * i.e. R=0xFF in the lowest byte; TIM_RGBA(0,0,255,255) (blue)
         * == 0x00FF0000u|(0xFFu<<24) i.e. B=0xFF in the third byte. */
        gpu_draw_quad_textured(30, 30, 0.0f, 0.0f,
                                90, 30, 1.0f, 0.0f,
                                90, 90, 1.0f, 1.0f,
                                30, 90, 0.0f, 1.0f,
                                checker, 4, 4);

        /* Top-left quadrant of the checkerboard (u,v in [0,0.5)) is
         * red -- sample well inside the top-left quarter of the quad. */
        expect_pixel(45, 45, 0x00FF0000, "textured quad top-left quadrant (red)");
        /* Top-right quadrant (u in [0.5,1), v in [0,0.5)) is blue. */
        expect_pixel(75, 45, 0x000000FF, "textured quad top-right quadrant (blue)");
        /* Bottom-left quadrant (u in [0,0.5), v in [0.5,1)) is blue. */
        expect_pixel(45, 75, 0x000000FF, "textured quad bottom-left quadrant (blue)");
        /* Bottom-right quadrant (u,v in [0.5,1)) is red. */
        expect_pixel(75, 75, 0x00FF0000, "textured quad bottom-right quadrant (red)");
        /* Outside the quad entirely: untouched by the draw call. */
        expect_pixel(10, 10, 0x00000000, "outside the textured quad");
    }

    /* 10. gpu_draw_quad_textured with a fully-transparent texture
     * (alpha==0 everywhere) must be a safe no-op -- proves the
     * alpha==0 texels-skipped rule actually skips the framebuffer
     * write rather than drawing black. */
    gpu_clear(0x00123456);
    {
        static const uint32_t transparent[2 * 2] = {0, 0, 0, 0};
        gpu_draw_quad_textured(20, 20, 0.0f, 0.0f,
                                60, 20, 1.0f, 0.0f,
                                60, 60, 1.0f, 1.0f,
                                20, 60, 0.0f, 1.0f,
                                transparent, 2, 2);
        expect_pixel(40, 40, 0x00123456, "fully-transparent textured quad is a no-op");
    }

    if (failures == 0) {
        printf("-- all gpu_soft sanity checks passed --\n");
        return 0;
    }
    printf("-- %d gpu_soft sanity check(s) FAILED --\n", failures);
    return 1;
}
