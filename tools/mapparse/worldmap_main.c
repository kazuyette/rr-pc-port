/* worldmap_main.c -- standalone CLI tool: combines MAP.RRM + IDX.HED
 * (both from the user's own legally-owned disc image) using the
 * confirmed transform (see idx_hed.h / project memory rr_pc_port_round8.md
 * .. round10.md for the full writeup) to plot a world-space top-down
 * view of the track.
 *
 * Usage:
 *   worldmap_tool <path/to/MAP.RRM> <path/to/IDX.HED> --ppm out.ppm
 *
 * Phase 5 round 8-10 summary (see project memory for full detail):
 *   - Live dynamic debugging (PCSX-Redux GDB server) definitively proved
 *     the game applies NO per-section rotation -- track sections are
 *     placed by pure translation to their IDX.HED grid-cell anchor
 *     (col, row) * 2048 world units, then combined with ONE shared
 *     live camera-view rotation matrix per frame (round 8). This
 *     translation-only model is exactly what this tool implements.
 *   - Round 10: MAP.RRM record parsing verified byte-exact against live
 *     game memory (not just static file analysis). The current mirrored-
 *     column / axis convention was re-confirmed as optimal via
 *     exhaustive search (8 sign/mirror/swap combos x 6 axis-identity
 *     permutations) -- do not re-tune this without new evidence.
 *   - Round 10 MAJOR FINDING: rendering with a color gradient by section
 *     index shows a single, perfectly continuous loop -- MAP.RRM's
 *     section order already IS the real track traversal order, and the
 *     section-level placement is correct.
 *   - Round 10 ALSO FOUND: earlier tangled-looking renders (this tool's
 *     previous wireframe-only output) were largely a RENDERING artifact,
 *     not a placement bug -- drawing every type-B (road surface) quad as
 *     a wireframe outline (4 edges + 2 diagonals) creates dense visual
 *     crosshatching wherever many records sit close together on screen.
 *     Switching to FILLED polygons for type-B quads instead produces a
 *     single, coherent, closed track shape with a clear infield hole --
 *     this is what this tool now does. Small self-intersecting notches
 *     can still appear in areas with very high per-section record counts
 *     (96-154 records -- likely genuine complex junctions/forks such as
 *     a pit lane or start/finish grid, not simple road strips; the exact
 *     intra-section record ordering there is not yet fully decoded).
 *   - Sections with <=2 total records (about a quarter of the 258) are
 *     excluded from the render -- round 10 found these are very likely
 *     markers/junction nodes rather than ordinary road geometry, and
 *     including them adds noise without adding real track shape.
 *
 * Treat this as a good, legible approximation of the real track shape,
 * not a byte-perfect reproduction -- the intra-section record ordering
 * for very complex (high record-count) sections is still not fully
 * understood.
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

/* Scanline fill of an N-vertex polygon (given in screen-pixel space
 * already, via a standard active-edge-list algorithm). Works for any
 * simple polygon, including the slightly non-convex quads this data
 * occasionally produces. */
static void fill_polygon_px(unsigned char *img, int w, int h,
                             const int *xs, const int *ys, int n,
                             int r, int g, int b) {
    int miny = h, maxy = -1, i, y;
    double xints[16];

    if (n > 16) return; /* defensive: we only ever call this with 4 */
    for (i = 0; i < n; i++) {
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    if (miny < 0) miny = 0;
    if (maxy >= h) maxy = h - 1;

    for (y = miny; y <= maxy; y++) {
        int cnt = 0, k;
        for (i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int y0 = ys[i], y1 = ys[j];
            if (y0 == y1) continue;
            if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
                double t = (double)(y - y0) / (double)(y1 - y0);
                xints[cnt++] = xs[i] + t * (xs[j] - xs[i]);
            }
        }
        /* insertion sort (cnt is tiny, at most n) */
        for (i = 1; i < cnt; i++) {
            double key = xints[i];
            int j2 = i - 1;
            while (j2 >= 0 && xints[j2] > key) { xints[j2+1] = xints[j2]; j2--; }
            xints[j2+1] = key;
        }
        for (k = 0; k + 1 < cnt; k += 2) {
            int xa = (int)xints[k], xb = (int)xints[k+1], x;
            if (xa < 0) xa = 0;
            if (xb >= w) xb = w - 1;
            for (x = xa; x <= xb; x++) {
                unsigned char *p = img + ((size_t)y * w + x) * 3;
                p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)b;
            }
        }
    }
}

static void fill_quad(unsigned char *img, int w, int h, Pt2 p0, Pt2 p1, Pt2 p2, Pt2 p3,
                       double minx, double minz, double scale,
                       int r, int g, int b) {
    /* corners ordered v0,v1,v3,v2 (near-L, near-R, far-R, far-L) to
     * trace the quad's actual perimeter instead of bowtie-crossing it --
     * confirmed correct ordering, see round 10 project memory. */
    int xs[4], ys[4];
    xs[0] = (int)((p0.x - minx) * scale) + 20; ys[0] = (int)((p0.z - minz) * scale) + 20;
    xs[1] = (int)((p1.x - minx) * scale) + 20; ys[1] = (int)((p1.z - minz) * scale) + 20;
    xs[2] = (int)((p2.x - minx) * scale) + 20; ys[2] = (int)((p2.z - minz) * scale) + 20;
    xs[3] = (int)((p3.x - minx) * scale) + 20; ys[3] = (int)((p3.z - minz) * scale) + 20;
    fill_polygon_px(img, w, h, xs, ys, 4, r, g, b);
}

int main(int argc, char **argv) {
    const char *map_path = NULL, *idx_path = NULL, *ppm_path = "worldmap.ppm";
    uint8_t *map_buf, *idx_buf;
    size_t map_size, idx_size;
    MapRrmFile mf;
    IdxHedFile idxf;
    int i, rc;
    size_t r;
    const int W = 1600, H = 1600;
    unsigned char *img;
    double minx = 1e18, maxx = -1e18, minz = 1e18, maxz = -1e18;
    double scale;

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

    /* A section is drawn only if it has more than 2 total records
     * (round 10 finding: <=2-record sections are very likely markers/
     * junction nodes, not ordinary road geometry, and only add noise). */
#define IS_REAL_ROAD_SECTION(si) \
    ((si) < mf.section_count && \
     ((int)mf.sections[si].count_a + mf.sections[si].count_b + mf.sections[si].count_c) > 2)

    /* Pass 1: compute world-space bbox (type B records only -- road
     * surface; A/C are much sparser and were observed not to add much
     * signal, see project memory). */
    for (r = 0; r < mf.record_count; r++) {
        MapRrmTaggedRecord *tr = &mf.records[r];
        int32_t ox, oz;
        int c;
        if (tr->type != MAP_RRM_RECORD_TYPE_B) continue;
        if (!IS_REAL_ROAD_SECTION(tr->section_index)) continue;
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
        img[r*3+0] = 8; img[r*3+1] = 8; img[r*3+2] = 16;
    }

    /* Pass 2a: fill type-B (road surface) records as solid quads. This
     * is the key round-10 fix -- filled polygons merge into one
     * coherent track ribbon even where per-record ordering isn't
     * perfectly chained, whereas wireframe outlines created dense
     * crosshatching that looked like a placement bug but wasn't. */
    for (r = 0; r < mf.record_count; r++) {
        MapRrmTaggedRecord *tr = &mf.records[r];
        int32_t ox, oz;
        Pt2 p0, p1, p2, p3;
        if (tr->type != MAP_RRM_RECORD_TYPE_B) continue;
        if (!IS_REAL_ROAD_SECTION(tr->section_index)) continue;
        if (idx_hed_section_world_origin(&idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;
        p0.x = ox + tr->rec.v0[0]; p0.z = oz + tr->rec.v0[2];
        p1.x = ox + tr->rec.v1[0]; p1.z = oz + tr->rec.v1[2];
        p2.x = ox + tr->rec.v2[0]; p2.z = oz + tr->rec.v2[2];
        p3.x = ox + tr->rec.v3[0]; p3.z = oz + tr->rec.v3[2];
        /* perimeter order v0,v1,v3,v2 -- see fill_quad comment */
        fill_quad(img, W, H, p0, p1, p3, p2, minx, minz, scale, 60, 150, 90);
    }

    /* Pass 2b: thin wireframe overlay for type A/C records (rarer,
     * less understood -- kept as outlines only, not filled, so they
     * read as annotations on top of the solid road ribbon rather than
     * dominating the image). */
    for (r = 0; r < mf.record_count; r++) {
        MapRrmTaggedRecord *tr = &mf.records[r];
        int32_t ox, oz;
        int rr, gg, bb;
        Pt2 corners[4];
        if (tr->type == MAP_RRM_RECORD_TYPE_B) continue;
        if (!IS_REAL_ROAD_SECTION(tr->section_index)) continue;
        if (idx_hed_section_world_origin(&idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;
        corners[0].x = ox + tr->rec.v0[0]; corners[0].z = oz + tr->rec.v0[2];
        corners[1].x = ox + tr->rec.v1[0]; corners[1].z = oz + tr->rec.v1[2];
        corners[2].x = ox + tr->rec.v2[0]; corners[2].z = oz + tr->rec.v2[2];
        corners[3].x = ox + tr->rec.v3[0]; corners[3].z = oz + tr->rec.v3[2];
        if (tr->type == MAP_RRM_RECORD_TYPE_A) { rr=230; gg=90; bb=90; }
        else { rr=110; gg=150; bb=255; }
        draw_line(img, W, H, corners[0], corners[1], minx, minz, scale, rr, gg, bb);
        draw_line(img, W, H, corners[2], corners[3], minx, minz, scale, rr, gg, bb);
    }

    {
        FILE *f = fopen(ppm_path, "wb");
        if (f == NULL) {
            fprintf(stderr, "worldmap: could not open '%s' for writing\n", ppm_path);
        } else {
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            fwrite(img, 1, (size_t)W * H * 3, f);
            fclose(f);
            printf("wrote %s (%dx%d) -- filled-polygon world-space plot, translation-only transform, see file header comment\n", ppm_path, W, H);
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
