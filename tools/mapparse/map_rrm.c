/* map_rrm.c -- see map_rrm.h for the full format writeup. This file
 * implements the CONFIRMED part of the format: header + section
 * directory + record-boundary accounting, replaying the same
 * accumulation arithmetic as the original PS1 loader (func_800125B4 in
 * rr-decomp's asm/29E8.s) so that the byte offsets this parser computes
 * are exactly the ones the original game computes.
 */
#include "map_rrm.h"

#include <stdlib.h>
#include <string.h>

static uint16_t read_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int16_t read_s16le(const uint8_t *p) {
    return (int16_t)read_u16le(p);
}

static void parse_record(const uint8_t *p, MapRrmRecord *out) {
    int i;
    for (i = 0; i < 3; i++) {
        out->v0[i] = read_s16le(p + 0 + i * 2);
        out->v1[i] = read_s16le(p + 6 + i * 2);
        out->v2[i] = read_s16le(p + 12 + i * 2);
        out->v3[i] = read_s16le(p + 18 + i * 2);
    }
    out->unk_18 = read_s16le(p + 24);
    out->heading = read_u16le(p + 26);
    out->unk_1c = read_s16le(p + 28);
    out->unk_1e = read_s16le(p + 30);
    out->unk_20 = read_s16le(p + 32);
    out->group_id = read_u16le(p + 34);
    out->unk_24 = read_s16le(p + 36);
    out->flags = read_u16le(p + 38);
}

void map_rrm_free(MapRrmFile *f) {
    if (f == NULL) {
        return;
    }
    free(f->sections);
    free(f->records);
    memset(f, 0, sizeof(*f));
}

int map_rrm_parse(const uint8_t *buf, size_t buf_size, MapRrmFile *out) {
    uint16_t section_count;
    size_t dir_bytes;
    size_t data_start;
    MapRrmSectionDir *sections = NULL;
    MapRrmTaggedRecord *records = NULL;
    size_t total_records = 0;
    size_t i;
    uint64_t running_offset; /* byte offset within the bulk-data region */
    size_t record_write_idx;

    if (out == NULL || buf == NULL) {
        return MAP_RRM_ERR_ARGS;
    }
    memset(out, 0, sizeof(*out));

    if (buf_size < MAP_RRM_HEADER_SIZE) {
        return MAP_RRM_ERR_TRUNCATED_HEADER;
    }
    section_count = read_u16le(buf + 0);
    /* bytes 2-3 of the header are unused/padding in every observed file. */

    dir_bytes = (size_t)section_count * MAP_RRM_DIR_ENTRY_SIZE;
    if (buf_size < MAP_RRM_HEADER_SIZE + dir_bytes) {
        return MAP_RRM_ERR_TRUNCATED_DIRECTORY;
    }
    data_start = MAP_RRM_HEADER_SIZE + dir_bytes;

    sections = (MapRrmSectionDir *)malloc(sizeof(MapRrmSectionDir) * (section_count ? section_count : 1));
    if (sections == NULL) {
        return MAP_RRM_ERR_ALLOC;
    }

    /* First pass: read the directory and total up the record count,
     * exactly mirroring func_800125B4's arithmetic (3 running-offset
     * accumulations of count*RECORD_SIZE per section, in a/b/c order). */
    for (i = 0; i < section_count; i++) {
        const uint8_t *e = buf + MAP_RRM_HEADER_SIZE + i * MAP_RRM_DIR_ENTRY_SIZE;
        MapRrmSectionDir d;
        d.count_a = read_u16le(e + 0);
        d.count_b = read_u16le(e + 2);
        d.count_c = read_u16le(e + 4);
        d.count_d = read_u16le(e + 6);
        sections[i] = d;
        total_records += (size_t)d.count_a + d.count_b + d.count_c;
    }

    if (data_start + (uint64_t)total_records * MAP_RRM_RECORD_SIZE > buf_size) {
        free(sections);
        return MAP_RRM_ERR_TRUNCATED_DATA;
    }

    records = (MapRrmTaggedRecord *)malloc(sizeof(MapRrmTaggedRecord) * (total_records ? total_records : 1));
    if (records == NULL) {
        free(sections);
        return MAP_RRM_ERR_ALLOC;
    }

    /* Second pass: walk the bulk-data region in the same per-section
     * A-run/B-run/C-run order the loader does, tagging each record. */
    running_offset = 0;
    record_write_idx = 0;
    for (i = 0; i < section_count; i++) {
        const MapRrmSectionDir *d = &sections[i];
        int t;
        uint16_t counts[3];
        counts[0] = d->count_a;
        counts[1] = d->count_b;
        counts[2] = d->count_c;
        for (t = 0; t < 3; t++) {
            uint16_t j;
            uint16_t cnt = counts[t];
            for (j = 0; j < cnt; j++) {
                uint32_t file_off = (uint32_t)(data_start + running_offset);
                MapRrmTaggedRecord *tr = &records[record_write_idx++];
                tr->section_index = (uint16_t)i;
                tr->type = (MapRrmRecordType)t;
                tr->index_in_run = j;
                tr->file_offset = file_off;
                parse_record(buf + file_off, &tr->rec);
                running_offset += MAP_RRM_RECORD_SIZE;
            }
        }
    }

    out->section_count = section_count;
    out->sections = sections;
    out->records = records;
    out->record_count = total_records;
    out->bytes_consumed = data_start + (size_t)running_offset;
    return MAP_RRM_OK;
}
