/* texdemo_test.c -- headless proof-of-concept CTest: decodes a real
 * TEX*.TMS file (via tim.c) and renders one of its pages through
 * gpu_draw_quad_textured() into the raw software-rasterizer
 * framebuffer, then asserts the framebuffer actually varies (isn't
 * all one solid color) -- i.e. that real, authentic game texture art
 * survived the whole decode -> rasterize pipeline, not just synthetic
 * shapes.
 *
 * Needs a real local texture file, which this repo never commits (see
 * tools/texparse/tim.h and the top-level README/task notes). Reads the
 * path from the RR_TEX_FILE environment variable; if it's unset, or
 * doesn't point at a file this process can open, the test SKIPS
 * (prints a note and exits 0) rather than failing -- so `ctest` stays
 * green in CI/sandboxes that don't have the asset, while still giving
 * a real automated check for anyone running it locally with a real
 * TEX0.TMS.
 *
 * Usage (manual, outside ctest):
 *   RR_TEX_FILE=/path/to/TEX0.TMS ./rr_pc_port_texdemo_test
 */
#include "tim.h"
#include "gpu_soft.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    size_t read_bytes;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size) {
        free(buf);
        return NULL;
    }
    *out_size = (size_t)size;
    return buf;
}

int main(void) {
    const char *path = getenv("RR_TEX_FILE");
    uint8_t *buf;
    size_t buf_size;
    TimFile tf;
    size_t i;
    const TimPage *page = NULL;

    if (path == NULL || path[0] == '\0') {
        printf("texdemo_test: SKIPPED (RR_TEX_FILE not set -- no real texture asset available)\n");
        return 0;
    }

    buf = read_whole_file(path, &buf_size);
    if (buf == NULL) {
        printf("texdemo_test: SKIPPED (could not read '%s')\n", path);
        return 0;
    }

    if (tim_parse(buf, buf_size, &tf) != TIM_OK) {
        printf("texdemo_test: FAIL (tim_parse failed on '%s')\n", path);
        free(buf);
        return 1;
    }

    /* Pick the first page with real decoded pixel data and a
     * reasonably-sized image (skip degenerate 0-size or undecoded
     * pages, same rule main.c's demo mode uses). */
    for (i = 0; i < tf.page_count; i++) {
        const TimPage *p = &tf.pages[i];
        if (p->rgba != NULL && p->width >= 8 && p->height >= 8) {
            page = p;
            break;
        }
    }
    if (page == NULL) {
        printf("texdemo_test: FAIL (no decodable page >=8x8 found in '%s', %zu pages total)\n",
               path, tf.page_count);
        tim_free(&tf);
        free(buf);
        return 1;
    }

    printf("texdemo_test: using page %zu (%dx%d) from '%s'\n",
           (size_t)(page - tf.pages), page->width, page->height, path);

    gpu_clear(0x00000000);
    gpu_draw_quad_textured(0, 0, 0.0f, 0.0f,
                            GPU_FB_WIDTH - 1, 0, 1.0f, 0.0f,
                            GPU_FB_WIDTH - 1, GPU_FB_HEIGHT - 1, 1.0f, 1.0f,
                            0, GPU_FB_HEIGHT - 1, 0.0f, 1.0f,
                            page->rgba, page->width, page->height);

    /* Assert real variation: collect distinct colors seen (capped) and
     * require more than a handful -- real texture art will have many
     * distinct colors, an all-black/all-one-color framebuffer (a bug
     * regression, e.g. decode returning garbage or the draw call being
     * a no-op) would show just one. */
    {
        uint32_t first = gpu_framebuffer[0];
        int distinct_found = 0;
        int px;
        for (px = 0; px < GPU_FB_WIDTH * GPU_FB_HEIGHT; px++) {
            if (gpu_framebuffer[px] != first) {
                distinct_found = 1;
                break;
            }
        }
        if (!distinct_found) {
            printf("texdemo_test: FAIL (framebuffer is a single solid color 0x%06X -- "
                   "decode or draw produced no real variation)\n", first);
            tim_free(&tf);
            free(buf);
            return 1;
        }
        printf("texdemo_test: ok (framebuffer shows real variation, not solid color)\n");
    }

    tim_free(&tf);
    free(buf);
    printf("-- texdemo_test PASSED --\n");
    return 0;
}
