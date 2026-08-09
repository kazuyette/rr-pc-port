/* texparse_main.c -- standalone CLI tool: parses a TEX*.TMS file (from
 * the user's own legally-owned Ridge Racer 1 disc image) and prints a
 * structural summary of every texture page found. Never bundles,
 * embeds, or commits the asset itself -- takes a filesystem path as an
 * argument, same pattern as tools/mapparse/mapparse_main.c.
 *
 * Usage:
 *   texparse_tool <path/to/TEX0.TMS> [--dump-page N --out page.ppm]
 *
 *   With no flags, prints a summary table (index, vram x/y, w, h, bpp
 *   mode, has-clut) for every page found.
 *
 *   --dump-page N --out FILE   decodes page N and writes it as a
 *                              binary PPM (P6) so it can be opened and
 *                              eyeballed to confirm the decode looks
 *                              like real texture art. Both flags are
 *                              required together.
 *
 * See tim.h for the full TEX*.TMS/TIM format writeup.
 */
#include "tim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    size_t read_bytes;

    if (f == NULL) {
        fprintf(stderr, "texparse: could not open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "texparse: fseek failed on '%s'\n", path);
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "texparse: ftell failed on '%s'\n", path);
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        fprintf(stderr, "texparse: out of memory reading '%s' (%ld bytes)\n", path, size);
        fclose(f);
        return NULL;
    }
    read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size) {
        fprintf(stderr, "texparse: short read on '%s' (%zu of %ld bytes)\n", path, read_bytes, size);
        free(buf);
        return NULL;
    }
    *out_size = (size_t)size;
    return buf;
}

static const char *mode_name(TimPixelMode m) {
    switch (m) {
        case TIM_MODE_4BPP_CLUT: return "4bpp";
        case TIM_MODE_8BPP_CLUT: return "8bpp";
        case TIM_MODE_16BPP_DIRECT: return "16bpp";
        case TIM_MODE_24BPP_DIRECT: return "24bpp(unsupported)";
        default: return "?";
    }
}

static void print_summary(const TimFile *tf, size_t file_size) {
    size_t i;
    printf("TEX*.TMS summary\n");
    printf("  file size:  %zu bytes\n", file_size);
    printf("  page count: %zu\n", tf->page_count);
    printf("  %-4s %-9s %-9s %-6s %-6s %-20s %-6s %-10s\n",
           "idx", "vram_x", "vram_y", "w", "h", "mode", "clut", "decoded");
    for (i = 0; i < tf->page_count; i++) {
        const TimPage *pg = &tf->pages[i];
        printf("  %-4zu %-9d %-9d %-6d %-6d %-20s %-6s %-10s\n",
               i, pg->vram_x, pg->vram_y, pg->width, pg->height,
               mode_name(pg->mode), pg->has_clut ? "yes" : "no",
               pg->rgba != NULL ? "yes" : "no");
    }
}

static void dump_page_ppm(const TimFile *tf, size_t page_index, const char *out_path) {
    const TimPage *pg;
    FILE *f;
    unsigned char *row;
    int x, y;

    if (page_index >= tf->page_count) {
        fprintf(stderr, "texparse: --dump-page %zu out of range (file has %zu pages)\n",
                page_index, tf->page_count);
        return;
    }
    pg = &tf->pages[page_index];
    if (pg->rgba == NULL || pg->width <= 0 || pg->height <= 0) {
        fprintf(stderr, "texparse: page %zu has no decoded pixel data (unsupported mode or truncated)\n",
                page_index);
        return;
    }

    f = fopen(out_path, "wb");
    if (f == NULL) {
        fprintf(stderr, "texparse: could not open '%s' for writing\n", out_path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", pg->width, pg->height);

    row = (unsigned char *)malloc((size_t)pg->width * 3);
    if (row == NULL) {
        fprintf(stderr, "texparse: out of memory writing PPM\n");
        fclose(f);
        return;
    }
    for (y = 0; y < pg->height; y++) {
        for (x = 0; x < pg->width; x++) {
            uint32_t texel = pg->rgba[(size_t)y * pg->width + x];
            row[x * 3 + 0] = (unsigned char)(texel & 0xFF);
            row[x * 3 + 1] = (unsigned char)((texel >> 8) & 0xFF);
            row[x * 3 + 2] = (unsigned char)((texel >> 16) & 0xFF);
        }
        fwrite(row, 1, (size_t)pg->width * 3, f);
    }
    free(row);
    fclose(f);
    printf("wrote %s (%dx%d, page %zu, mode=%s)\n", out_path, pg->width, pg->height, page_index, mode_name(pg->mode));
}

int main(int argc, char **argv) {
    const char *in_path = NULL;
    const char *out_path = NULL;
    long dump_page = -1;
    uint8_t *buf;
    size_t buf_size;
    TimFile tf;
    int rc;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-page") == 0 && i + 1 < argc) {
            dump_page = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (in_path == NULL) {
            in_path = argv[i];
        } else {
            fprintf(stderr, "texparse: unexpected argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if (in_path == NULL) {
        fprintf(stderr, "usage: %s <path/to/TEX0.TMS> [--dump-page N --out page.ppm]\n", argv[0]);
        return 2;
    }
    if ((dump_page >= 0) != (out_path != NULL)) {
        fprintf(stderr, "texparse: --dump-page and --out must be given together\n");
        return 2;
    }

    buf = read_whole_file(in_path, &buf_size);
    if (buf == NULL) {
        return 1;
    }

    rc = tim_parse(buf, buf_size, &tf);
    if (rc != TIM_OK) {
        fprintf(stderr, "texparse: parse failed (error %d)\n", rc);
        free(buf);
        return 1;
    }

    print_summary(&tf, buf_size);
    if (dump_page >= 0) {
        dump_page_ppm(&tf, (size_t)dump_page, out_path);
    }

    tim_free(&tf);
    free(buf);
    return 0;
}
