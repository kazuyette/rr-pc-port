/* rr-pc-port -- phase 3, headless sanity check for the software
 * rasterizer. No SDL/display needed: exercises gpu_draw_triangle_flat()
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

    if (failures == 0) {
        printf("-- all gpu_soft sanity checks passed --\n");
        return 0;
    }
    printf("-- %d gpu_soft sanity check(s) FAILED --\n", failures);
    return 1;
}
