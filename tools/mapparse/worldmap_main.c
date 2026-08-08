/* worldmap_main.c -- standalone CLI tool: combines MAP.RRM + IDX.HED
 * (both from the user's own legally-owned disc image) using the
 * empirically-best transform found this round (see idx_hed.h for the
 * full confirmed/hypothesis writeup) to plot an approximate world-space
 * top-down view of the track.
 *
 * Usage:
 *   worldmap_tool <path/to/MAP.RRM> <path/to/IDX.HED> --ppm out.ppm
 *
 * IMPORTANT, read before trusting the output: this transform is a
 * TRANSLATION ONLY (each MAP.RRM section is placed at
 * (mirrored_grid_col, grid_row) * 2048 world units, straight from
 * IDX.HED, then that section's raw local record coordinates are added
 * directly with NO rotation). Two rotation hypotheses were tried and
 * both made results worse, so they are NOT applied here -- see
 * idx_hed.h and project memory for details. The result is a REAL,
 * reproducible partial win (roughly half of the 258 sections -- the
 * straighter / more gently curved ones -- resolve into a clean,
 * recognizable road shape with correct near/far edge quad structure),
 * but the more tightly curved sections still overlap into a tangled
 * area because their true placement needs a rotation this round did
 * not find. Treat the output as "exciting evidence the grid+
 * translation model is basically right", not "the finished map".
 */
#include "map_rrm.h"
#include "idx_hed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    size_t read_bytes;

    if (f == NULL) {
        fprintf(stderr, "worldmap: could not open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);

    buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) { fclose(f); return NULL; }
    read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size) { free(buf); return NULL; }
    *out_size = (size_t)size;
    return buf;
}

typedef struct { double x, z; } Pt2;

static void draw_line(unsigned char *img, int w, int h, Pt2 p0, Pt2 p1,
                       double minx, double minz, double scale,
                       int r, int g, int b) {
    int x0 = (int)((p0.x - minx) * scale) + 20;
    int y0 = (int)((p0.z - minz) * scale) + 20;
    int x1 = (int)((p1.x - minx) * scale) + 20;
    int y1 = (int)((p1.z - minz) * scale) + 20;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
            unsigned char *p = img + ((size_t)y0 * w + x0) * 3;
            p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)b;
        }
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

int main(int argc, char **argv) {
    const char *map_path = NULL, *idx_path = NULL, *ppm_path = "worldmap.ppm";
    uint8_t *map_buf, *idx_buf;
    size_t map_size, idx_size;
    MapRrmFile mf;
    IdxHedFile idxf;
    int i, rc;
    size_t r;
    const int W = 1400, H = 1400;
    unsigned char *img;
    double minx = 1e18, maxx = -1e18, minz = 1e18, maxz = -1e18;
    double scale;
    Pt2 *corners; /* 4 per record, reused */

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) {
            ppm_path = argv[++i];
        } else if (map_path == NULL) {
            map_path = argv[i];
        } else if (idx_path == NULL) {
            idx_path = argv[i];
        } else {
            fprintf(stderr, "worldmap: unexpected argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if (map_path == NULL || idx_path == NULL) {
        fprintf(stderr, "usage: %s <MAP.RRM> <IDX.HED> [--ppm out.ppm]\n", argv[0]);
        return 2;
    }

    map_buf = read_whole_file(map_path, &map_size);
    idx_buf = read_whole_file(idx_path, &idx_size);
    if (!map_buf || !idx_buf) { free(map_buf); free(idx_buf); return 1; }

    rc = map_rrm_parse(map_buf, map_size, &mf);
    if (rc != MAP_RRM_OK) {
        fprintf(stderr, "worldmap: MAP.RRM parse failed (%d)\n", rc);
        return 1;
    }
    rc = idx_hed_parse(idx_buf, idx_size, &idxf);
    if (rc != IDX_HED_OK) {
        fprintf(stderr, "worldmap: IDX.HED parse failed (%d)\n", rc);
        return 1;
    }

    /* Pass 1: compute world-space bbox (type B records only -- road
     * surface; A/C are much sparser and were observed not to add much
     * signal, see project memory). */
    for (r = 0; r < mf.record_count; r++) {
        MapRrmTaggedRecord *tr = &mf.records[r];
        int32_t ox, oz;
        int c;
        if (tr->type != MAP_RRM_RECORD_TYPE_B) continue;
        if (idx_hed_section_world_origin(&idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;
        for (c = 0; c < 4; c++) {
            int16_t *v = (c == 0) ? tr->rec.v0 : (c == 1) ? tr->rec.v1 : (c == 2) ? tr->rec.v2 : tr->rec.v3;
            double wx = ox + v[0], wz = oz + v[2];
            if (wx < minx) minx = wx;
            if (wx > maxx) maxx = wx;
            if (wz < minz) minz = wz;
            if (wz > maxz) maxz = wz;
        }
    }
    if (maxx <= minx) maxx = minx + 1;
    if (maxz <= minz) maxz = minz + 1;
    scale = (double)(W - 40) / (maxx - minx + 1);
    if ((double)(H - 40) / (maxz - minz + 1) < scale) scale = (double)(H - 40) / (maxz - minz + 1);

    img = (unsigned char *)malloc((size_t)W * H * 3);
    for (r = 0; r < (size_t)W * H; r++) {
        img[r*3+0] = 10; img[r*3+1] = 10; img[r*3+2] = 20;
    }

    corners = (Pt2 *)malloc(4 * sizeof(Pt2));
    for (r = 0; r < mf.record_count; r++) {
        MapRrmTaggedRecord *tr = &mf.records[r];
        int32_t ox, oz;
        int rr, gg, bb;
        if (idx_hed_section_world_origin(&idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;
        corners[0].x = ox + tr->rec.v0[0]; corners[0].z = oz + tr->rec.v0[2];
        corners[1].x = ox + tr->rec.v1[0]; corners[1].z = oz + tr->rec.v1[2];
        corners[2].x = ox + tr->rec.v2[0]; corners[2].z = oz + tr->rec.v2[2];
        corners[3].x = ox + tr->rec.v3[0]; corners[3].z = oz + tr->rec.v3[2];
        switch (tr->type) {
            case MAP_RRM_RECORD_TYPE_A: rr=230; gg=90; bb=90; break;
            case MAP_RRM_RECORD_TYPE_B: rr=90; gg=220; bb=120; break;
            default: rr=110; gg=150; bb=255; break;
        }
        draw_line(img, W, H, corners[0], corners[1], minx, minz, scale, rr, gg, bb);
        draw_line(img, W, H, corners[2], corners[3], minx, minz, scale, rr, gg, bb);
        draw_line(img, W, H, corners[0], corners[2], minx, minz, scale, rr/2, gg/2, bb/2);
        draw_line(img, W, H, corners[1], corners[3], minx, minz, scale, rr/2, gg/2, bb/2);
    }
    free(corners);

    {
        FILE *f = fopen(ppm_path, "wb");
        if (f == NULL) {
            fprintf(stderr, "worldmap: could not open '%s' for writing\n", ppm_path);
        } else {
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            fwrite(img, 1, (size_t)W * H * 3, f);
            fclose(f);
            printf("wrote %s (%dx%d) -- PARTIAL world-space plot, translation-only transform, see file header comment\n", ppm_path, W, H);
        }
    }

    printf("sections placed: %d of %u (IDX.HED<->MAP.RRM bijection check)\n",
           idxf.max_section_seen + 1, (unsigned)mf.section_count);

    free(img);
    map_rrm_free(&mf);
    free(map_buf);
    free(idx_buf);
    return 0;
}
