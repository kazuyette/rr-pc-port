/* trackdata_main.c -- standalone CLI tool: extracts Ridge Racer 1's two
 * built-in section-chain course-geometry tables directly out of the
 * user's own legally-owned PSX.EXE and prints/exports them. Never
 * bundles, embeds, or commits the EXE itself -- takes a filesystem path
 * as an argument, same convention as every other tool in this repo.
 *
 * Usage:
 *   trackdata_tool <path/to/PSX.EXE> [--course a|b|both] [--csv out.csv]
 *
 * See trackdata.h for the full format writeup (what's confirmed vs. a
 * documented simplification) and src/physics.h for how this data feeds
 * the port's physics code.
 */
#include "trackdata.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    size_t read_bytes;

    if (f == NULL) {
        fprintf(stderr, "trackdata_tool: could not open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) { fclose(f); return NULL; }

    read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size) {
        fprintf(stderr, "trackdata_tool: short read on '%s'\n", path);
        free(buf);
        return NULL;
    }
    *out_size = (size_t)size;
    return buf;
}

static void print_summary(const char *name, const TrackData *td) {
    size_t i;
    double loop_dx, loop_dz, loop_dist;

    printf("== %s: %zu sections ==\n", name, td->count);
    for (i = 0; i < td->count && i < 8; i++) {
        const TrackSection *s = &td->sections[i];
        printf("  [%3zu] X=%9.2f Z=%9.2f heading=%6d widthR=%6.2f widthL=%6.2f "
               "aux_a=%6d aux_heading=%6d\n",
               i, s->x, s->z, (int)s->heading_raw, s->width_right, s->width_left,
               (int)s->aux_a_raw, (int)s->aux_heading_raw);
    }
    if (td->count > 8) printf("  ... (%zu more)\n", td->count - 8);

    if (td->count >= 2) {
        loop_dx = td->sections[td->count - 1].x - td->sections[0].x;
        loop_dz = td->sections[td->count - 1].z - td->sections[0].z;
        loop_dist = sqrt(loop_dx * loop_dx + loop_dz * loop_dz);
        printf("  loop-closure gap (last section -> first section): %.2f world units\n", loop_dist);
    }
}

static void write_csv(const char *path, const TrackData *td_a, const TrackData *td_b) {
    FILE *f = fopen(path, "w");
    size_t i;
    if (f == NULL) {
        fprintf(stderr, "trackdata_tool: could not write '%s'\n", path);
        return;
    }
    fprintf(f, "course,index,x,z,heading_raw,width_right,width_left,aux_a_raw,aux_heading_raw\n");
    if (td_a != NULL) {
        for (i = 0; i < td_a->count; i++) {
            const TrackSection *s = &td_a->sections[i];
            fprintf(f, "A,%zu,%.4f,%.4f,%d,%.4f,%.4f,%d,%d\n",
                    i, s->x, s->z, (int)s->heading_raw, s->width_right, s->width_left,
                    (int)s->aux_a_raw, (int)s->aux_heading_raw);
        }
    }
    if (td_b != NULL) {
        for (i = 0; i < td_b->count; i++) {
            const TrackSection *s = &td_b->sections[i];
            fprintf(f, "B,%zu,%.4f,%.4f,%d,%.4f,%.4f,%d,%d\n",
                    i, s->x, s->z, (int)s->heading_raw, s->width_right, s->width_left,
                    (int)s->aux_a_raw, (int)s->aux_heading_raw);
        }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv) {
    const char *exe_path = NULL;
    const char *csv_path = NULL;
    const char *course_arg = "both";
    uint8_t *exe_buf;
    size_t exe_size;
    TrackData td_a, td_b;
    int have_a = 0, have_b = 0;
    int rc;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (strcmp(argv[i], "--course") == 0 && i + 1 < argc) {
            course_arg = argv[++i];
        } else if (exe_path == NULL) {
            exe_path = argv[i];
        }
    }

    if (exe_path == NULL) {
        fprintf(stderr, "usage: trackdata_tool <path/to/PSX.EXE> [--course a|b|both] [--csv out.csv]\n");
        return 1;
    }

    exe_buf = read_whole_file(exe_path, &exe_size);
    if (exe_buf == NULL) return 1;

    memset(&td_a, 0, sizeof(td_a));
    memset(&td_b, 0, sizeof(td_b));

    if (strcmp(course_arg, "a") == 0 || strcmp(course_arg, "both") == 0) {
        rc = trackdata_parse(exe_buf, exe_size, TRACKDATA_COURSE_A_RAM_ADDR,
                              TRACKDATA_COURSE_A_COUNT, &td_a);
        if (rc != TRACKDATA_OK) {
            fprintf(stderr, "trackdata_tool: failed to parse course A (rc=%d) -- "
                             "wrong EXE version/region? table addresses are specific "
                             "to the original Japan Nov. 1994 build.\n", rc);
        } else {
            have_a = 1;
            print_summary("course A (D_8005A44C)", &td_a);
        }
    }
    if (strcmp(course_arg, "b") == 0 || strcmp(course_arg, "both") == 0) {
        rc = trackdata_parse(exe_buf, exe_size, TRACKDATA_COURSE_B_RAM_ADDR,
                              TRACKDATA_COURSE_B_COUNT, &td_b);
        if (rc != TRACKDATA_OK) {
            fprintf(stderr, "trackdata_tool: failed to parse course B (rc=%d)\n", rc);
        } else {
            have_b = 1;
            print_summary("course B (D_8005CC4C)", &td_b);
        }
    }

    if (csv_path != NULL) {
        write_csv(csv_path, have_a ? &td_a : NULL, have_b ? &td_b : NULL);
    }

    trackdata_free(&td_a);
    trackdata_free(&td_b);
    free(exe_buf);
    return (have_a || have_b) ? 0 : 1;
}
