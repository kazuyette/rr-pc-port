/* mapparse_main.c -- standalone CLI tool: parses a MAP.RRM file (from
 * the user's own legally-owned Ridge Racer 1 disc image) and prints a
 * structural summary. Never bundles, embeds, or commits the asset
 * itself -- takes a filesystem path as an argument.
 *
 * Usage:
 *   mapparse <path/to/MAP.RRM> [--ppm out.ppm] [--csv out.csv]
 *
 *   --ppm FILE   writes a top-down (X,Z) scatter plot of every record's
 *                first corner (v0), color-coded red/green/blue for
 *                type A/B/C, as a binary PPM (P6). This is a raw-data
 *                sanity check, NOT a reconstruction of the track's
 *                world-space shape -- see map_rrm.h for why (no
 *                per-section transform has been decoded yet, so this
 *                plots each section's local-frame coordinates
 *                side-by-side rather than stitched into one path).
 *   --csv FILE   writes every parsed record as one CSV row (all fields
 *                including the unconfirmed ones), for external
 *                inspection/spreadsheet analysis.
 *
 * See map_rrm.h for the full format writeup (what's confirmed vs. a
 * hypothesis).
 */
#include "map_rrm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    size_t read_bytes;

    if (f == NULL) {
        fprintf(stderr, "mapparse: could not open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "mapparse: fseek failed on '%s'\n", path);
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "mapparse: ftell failed on '%s'\n", path);
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        fprintf(stderr, "mapparse: out of memory reading '%s' (%ld bytes)\n", path, size);
        fclose(f);
        return NULL;
    }
    read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size) {
        fprintf(stderr, "mapparse: short read on '%s' (%zu of %ld bytes)\n", path, read_bytes, size);
        free(buf);
        return NULL;
    }
    *out_size = (size_t)size;
    return buf;
}

static const char *type_name(MapRrmRecordType t) {
    switch (t) {
        case MAP_RRM_RECORD_TYPE_A: return "A";
        case MAP_RRM_RECORD_TYPE_B: return "B";
        case MAP_RRM_RECORD_TYPE_C: return "C";
        default: return "?";
    }
}

static void print_summary(const MapRrmFile *mf, size_t file_size) {
    size_t counts[3] = {0, 0, 0};
    size_t i;
    int16_t xmin = 32767, xmax = -32768, zmin = 32767, zmax = -32768;

    for (i = 0; i < mf->record_count; i++) {
        counts[(int)mf->records[i].type]++;
        if (mf->records[i].rec.v0[0] < xmin) xmin = mf->records[i].rec.v0[0];
        if (mf->records[i].rec.v0[0] > xmax) xmax = mf->records[i].rec.v0[0];
        if (mf->records[i].rec.v0[2] < zmin) zmin = mf->records[i].rec.v0[2];
        if (mf->records[i].rec.v0[2] > zmax) zmax = mf->records[i].rec.v0[2];
    }

    printf("MAP.RRM summary\n");
    printf("  file size:        %zu bytes\n", file_size);
    printf("  section count (N): %u\n", (unsigned)mf->section_count);
    printf("  header+directory: %u bytes (4 + %u*8)\n",
           (unsigned)(MAP_RRM_HEADER_SIZE + (size_t)mf->section_count * MAP_RRM_DIR_ENTRY_SIZE),
           (unsigned)mf->section_count);
    printf("  bulk-data records: %zu total (A=%zu B=%zu C=%zu), %zu bytes\n",
           mf->record_count, counts[0], counts[1], counts[2],
           mf->record_count * MAP_RRM_RECORD_SIZE);
    printf("  bytes_consumed:    %zu (%s file size)\n",
           mf->bytes_consumed, mf->bytes_consumed == file_size ? "==" : "!=");
    printf("  v0 corner range:   x=[%d,%d] z=[%d,%d] (raw int16 units, likely section-local, not world space)\n",
           xmin, xmax, zmin, zmax);
}

static void write_ppm(const MapRrmFile *mf, const char *path) {
    /* Fixed canvas; scales v0 (x,z) of every record into it. Colors:
     * type A = red, type B = green, type C = blue. This is purely a
     * raw-data sanity check (see file header comment) -- it shows
     * whether the points are structured/clustered (they are) rather
     * than random noise, not a reconstruction of the actual track
     * outline (no per-section transform has been decoded). */
    const int W = 800, H = 800;
    unsigned char *img;
    size_t i;
    int16_t xmin = 32767, xmax = -32768, zmin = 32767, zmax = -32768;
    FILE *f;

    for (i = 0; i < mf->record_count; i++) {
        int16_t x = mf->records[i].rec.v0[0];
        int16_t z = mf->records[i].rec.v0[2];
        if (x < xmin) xmin = x;
        if (x > xmax) xmax = x;
        if (z < zmin) zmin = z;
        if (z > zmax) zmax = z;
    }
    if (xmax <= xmin) xmax = (int16_t)(xmin + 1);
    if (zmax <= zmin) zmax = (int16_t)(zmin + 1);

    img = (unsigned char *)calloc((size_t)W * H * 3, 1);
    if (img == NULL) {
        fprintf(stderr, "mapparse: out of memory writing PPM\n");
        return;
    }

    for (i = 0; i < mf->record_count; i++) {
        const MapRrmRecord *r = &mf->records[i].rec;
        int px = (int)(((long)(r->v0[0] - xmin) * (W - 1)) / (xmax - xmin));
        int pz = (int)(((long)(r->v0[2] - zmin) * (H - 1)) / (zmax - zmin));
        int py = (H - 1) - pz; /* flip so +Z is "up" on the image */
        unsigned char *px_ptr;
        if (px < 0 || px >= W || py < 0 || py >= H) {
            continue;
        }
        px_ptr = img + ((size_t)py * W + px) * 3;
        switch (mf->records[i].type) {
            case MAP_RRM_RECORD_TYPE_A: px_ptr[0] = 255; break;
            case MAP_RRM_RECORD_TYPE_B: px_ptr[1] = 255; break;
            case MAP_RRM_RECORD_TYPE_C: px_ptr[2] = 255; break;
        }
    }

    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "mapparse: could not open '%s' for writing\n", path);
        free(img);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(img, 1, (size_t)W * H * 3, f);
    fclose(f);
    free(img);
    printf("wrote %s (%dx%d, red=typeA green=typeB blue=typeC, v0 corner only)\n", path, W, H);
}

static void write_csv(const MapRrmFile *mf, const char *path) {
    FILE *f = fopen(path, "w");
    size_t i;
    if (f == NULL) {
        fprintf(stderr, "mapparse: could not open '%s' for writing\n", path);
        return;
    }
    fprintf(f, "section,type,index_in_run,file_offset,"
               "v0x,v0y,v0z,v1x,v1y,v1z,v2x,v2y,v2z,v3x,v3y,v3z,"
               "unk_18,heading,unk_1c,unk_1e,unk_20,group_id,unk_24,flags\n");
    for (i = 0; i < mf->record_count; i++) {
        const MapRrmTaggedRecord *tr = &mf->records[i];
        const MapRrmRecord *r = &tr->rec;
        fprintf(f, "%u,%s,%u,%u,"
                   "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
                   "%d,%u,%d,%d,%d,%u,%d,%u\n",
                (unsigned)tr->section_index, type_name(tr->type), (unsigned)tr->index_in_run,
                (unsigned)tr->file_offset,
                r->v0[0], r->v0[1], r->v0[2], r->v1[0], r->v1[1], r->v1[2],
                r->v2[0], r->v2[1], r->v2[2], r->v3[0], r->v3[1], r->v3[2],
                r->unk_18, (unsigned)r->heading, r->unk_1c, r->unk_1e, r->unk_20,
                (unsigned)r->group_id, r->unk_24, (unsigned)r->flags);
    }
    fclose(f);
    printf("wrote %s (%zu rows)\n", path, mf->record_count);
}

int main(int argc, char **argv) {
    const char *in_path = NULL;
    const char *ppm_path = NULL;
    const char *csv_path = NULL;
    uint8_t *buf;
    size_t buf_size;
    MapRrmFile mf;
    int rc;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) {
            ppm_path = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (in_path == NULL) {
            in_path = argv[i];
        } else {
            fprintf(stderr, "mapparse: unexpected argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if (in_path == NULL) {
        fprintf(stderr, "usage: %s <path/to/MAP.RRM> [--ppm out.ppm] [--csv out.csv]\n", argv[0]);
        return 2;
    }

    buf = read_whole_file(in_path, &buf_size);
    if (buf == NULL) {
        return 1;
    }

    rc = map_rrm_parse(buf, buf_size, &mf);
    if (rc != MAP_RRM_OK) {
        fprintf(stderr, "mapparse: parse failed (error %d)\n", rc);
        free(buf);
        return 1;
    }

    print_summary(&mf, buf_size);
    if (ppm_path != NULL) {
        write_ppm(&mf, ppm_path);
    }
    if (csv_path != NULL) {
        write_csv(&mf, csv_path);
    }

    map_rrm_free(&mf);
    free(buf);
    return 0;
}
