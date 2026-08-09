/* objparse_main.c -- standalone CLI tool: parses an OBJ.RRO file (from
 * the user's own legally-owned disc image) and prints the CONFIRMED
 * directory-level summary. See obj_rro.h for the full writeup,
 * including the ~100KB of file content this round did NOT manage to
 * account for -- this tool's summary makes that gap visible rather
 * than hiding it.
 *
 * Phase 5 round 3 additions (see obj_rro.h + project memory for the
 * full writeup): two extra analyses added to help characterize the
 * still-undecoded trailing region and the still-mysterious pre-
 * overwrite directory "ptr_field" column, WITHOUT printing/embedding
 * any raw bytes from the (copyrighted, never committed) game file --
 * only aggregate statistics computed at runtime from a file path the
 * user supplies.
 *   1. ptr_field clustering: splits the 319 on-disk (pre-overwrite)
 *      ptr_field values into "zero" / "small integer" (1..1000) /
 *      "large" (>1000) buckets, and for the "large" bucket reports how
 *      many are within a small tolerance of an exact multiple of
 *      65536 -- i.e. plausibly a 16.16 fixed-point encoding of a small
 *      integer (1, 4, 5, 7, 8, 16, 29, ... observed on the real file),
 *      rather than a byte offset/pointer as first assumed.
 *   2. 0x0FFF-sentinel frequency, compared between the "accounted"
 *      per-object data blobs and the still-unaccounted trailing
 *      region -- this corrects an overstated claim from the previous
 *      round's notes (that 0x0FFF looked distinctly more common in the
 *      trailing region): measured exhaustively, the two regions turn
 *      out to have a COMPARABLE 0x0FFF density, so it is NOT a useful
 *      distinguishing signature after all.
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

    /* --- Phase 5 round 3: ptr_field clustering --- */
    {
        uint32_t n_zero = 0, n_small = 0, n_large = 0, n_large_near_65536 = 0;
        for (i = 0; i < of.object_count; i++) {
            int32_t p = of.entries[i].on_disk_ptr_field;
            if (p == 0) {
                n_zero++;
            } else if (p > 0 && p <= 1000) {
                n_small++;
            } else if (p > 1000) {
                int32_t rem = p % 65536;
                int32_t dist_to_multiple = (rem <= 65536 - rem) ? rem : (65536 - rem);
                n_large++;
                if (dist_to_multiple <= 512) { /* small tolerance, ~0.8% of 65536 */
                    n_large_near_65536++;
                }
            }
        }
        printf("\n  ptr_field clustering (pre-overwrite on-disk value, offset 0-3 of each\n");
        printf("  directory entry -- see obj_rro.h; NOT read by func_80012670 itself):\n");
        printf("    zero:                    %u of %u\n", n_zero, (unsigned)of.object_count);
        printf("    small (1..1000):         %u of %u\n", n_small, (unsigned)of.object_count);
        printf("    large (>1000):           %u of %u, of which %u (%.0f%%) are within +/-512\n",
               n_large, (unsigned)of.object_count, n_large_near_65536,
               n_large ? (100.0 * n_large_near_65536 / n_large) : 0.0);
        printf("    of an exact multiple of 65536 (16.16 fixed-point small-integer hypothesis,\n");
        printf("    e.g. observed values like 0x1D0140 = 29 + 320/65536 -- NOT confirmed, but a\n");
        printf("    genuinely new lead this round: this field is NOT simple padding/placeholder.\n");
    }

    /* --- Phase 5 round 3: 0x0FFF sentinel-frequency comparison --- */
    {
        uint64_t acc_start = of.data_start_offset;
        uint64_t acc_end = of.accounted_bytes < buf_size ? of.accounted_bytes : buf_size;
        uint64_t trail_start = acc_end;
        uint64_t trail_end = buf_size;
        uint64_t acc_count16 = 0, acc_sentinel = 0;
        uint64_t trail_count16 = 0, trail_sentinel = 0;
        uint64_t off;

        for (off = acc_start; off + 1 < acc_end; off += 2) {
            uint16_t v = (uint16_t)(buf[off] | ((uint16_t)buf[off + 1] << 8));
            acc_count16++;
            if (v == 0x0FFFu) acc_sentinel++;
        }
        for (off = trail_start; off + 1 < trail_end; off += 2) {
            uint16_t v = (uint16_t)(buf[off] | ((uint16_t)buf[off + 1] << 8));
            trail_count16++;
            if (v == 0x0FFFu) trail_sentinel++;
        }
        printf("\n  0x0FFF int16 sentinel frequency (as a proxy for \"is the trailing region\n");
        printf("  statistically distinct from the accounted per-object data blobs\"):\n");
        printf("    accounted region:  %llu / %llu int16s (%.3f%%)\n",
               (unsigned long long)acc_sentinel, (unsigned long long)acc_count16,
               acc_count16 ? (100.0 * (double)acc_sentinel / (double)acc_count16) : 0.0);
        printf("    trailing region:   %llu / %llu int16s (%.3f%%)\n",
               (unsigned long long)trail_sentinel, (unsigned long long)trail_count16,
               trail_count16 ? (100.0 * (double)trail_sentinel / (double)trail_count16) : 0.0);
        printf("    -> comparable order of magnitude in both regions on the real file; NOT a\n");
        printf("    strong distinguishing signature (corrects an overstated claim from the\n");
        printf("    previous round's notes). The trailing region IS still structured/non-random\n");
        printf("    (small-magnitude int16s, period-6-byte local repetition consistent with\n");
        printf("    3x int16 vectors), just not via this particular sentinel-count test.\n");
    }

    obj_rro_free(&of);
    free(buf);
    return 0;
}
