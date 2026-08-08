/* objparse_main.c -- standalone CLI tool: parses an OBJ.RRO file (from
 * the user's own legally-owned disc image) and prints the CONFIRMED
 * directory-level summary. See obj_rro.h for the full writeup,
 * including the ~100KB of file content this round did NOT manage to
 * account for -- this tool's summary makes that gap visible rather
 * than hiding it.
 *
 * Usage: objparse_tool <path/to/OBJ.RRO>
 */
#include "obj_rro.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    size_t read_bytes;

    if (f == NULL) {
        fprintf(stderr, "objparse: could not open '%s'\n", path);
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

int main(int argc, char **argv) {
    uint8_t *buf;
    size_t buf_size;
    ObjRroFile of;
    int rc;
    uint32_t i;
    uint32_t nonzero_entries = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <path/to/OBJ.RRO>\n", argv[0]);
        return 2;
    }
    buf = read_whole_file(argv[1], &buf_size);
    if (buf == NULL) return 1;

    rc = obj_rro_parse(buf, buf_size, &of);
    if (rc != OBJ_RRO_OK) {
        fprintf(stderr, "objparse: parse failed (%d)\n", rc);
        free(buf);
        return 1;
    }

    for (i = 0; i < of.object_count; i++) {
        ObjRroDirEntry *e = &of.entries[i];
        if (e->field_a || e->field_b || e->field_c || e->field_d || e->field_e || e->field_f) {
            nonzero_entries++;
        }
    }

    printf("OBJ.RRO summary (directory-level only -- see obj_rro.h, data blobs NOT parsed)\n");
    printf("  file size:            %zu bytes\n", buf_size);
    printf("  object count (N):     %u\n", (unsigned)of.object_count);
    printf("  directory bytes:      %u (4 + %u*16)\n", of.data_start_offset, (unsigned)of.object_count);
    printf("  entries with data:    %u of %u (nonzero field_a..f)\n", nonzero_entries, (unsigned)of.object_count);
    printf("  sum computed sizes:   %llu bytes (field_a*40+field_b*48+field_c*32+field_d*64+field_e*72+field_f*56)\n",
           (unsigned long long)of.sum_computed_size);
    printf("  accounted bytes:      %llu (data_start + sum) vs real file size %zu -> %s%lld unaccounted\n",
           (unsigned long long)of.accounted_bytes, buf_size,
           ((long long)buf_size - (long long)of.accounted_bytes) >= 0 ? "" : "-",
           (long long)buf_size - (long long)of.accounted_bytes < 0
               ? -((long long)buf_size - (long long)of.accounted_bytes)
               : (long long)buf_size - (long long)of.accounted_bytes);
    printf("  NOTE: nonzero unaccounted bytes is EXPECTED this round -- the directory\n");
    printf("  structure and per-entry size formula are confirmed via instruction trace,\n");
    printf("  but a trailing section of the file (looks like vertex/coordinate data on\n");
    printf("  hex inspection) is not walked by func_80012670 and was not decoded.\n");

    obj_rro_free(&of);
    free(buf);
    return 0;
}
