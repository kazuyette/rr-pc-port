/* obj_rro.c -- see obj_rro.h for the full confirmed/hypothesis writeup. */

#include "obj_rro.h"
#include <stdlib.h>
#include <string.h>

static int16_t read_i16le(const uint8_t *p) {
    uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
    return (int16_t)v;
}

static int32_t read_i32le(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (int32_t)v;
}

int obj_rro_parse(const uint8_t *buf, size_t buf_size, ObjRroFile *out) {
    uint32_t count, i;
    uint64_t dir_bytes, data_start;

    if (!buf || !out) {
        return OBJ_RRO_ERR_ARGS;
    }
    memset(out, 0, sizeof(*out));

    if (buf_size < OBJ_RRO_HEADER_SIZE) {
        return OBJ_RRO_ERR_TRUNCATED_HEADER;
    }
    count = (uint32_t)read_i32le(buf);

    dir_bytes = (uint64_t)count * OBJ_RRO_DIR_ENTRY_SIZE;
    if ((uint64_t)OBJ_RRO_HEADER_SIZE + dir_bytes > buf_size) {
        return OBJ_RRO_ERR_TRUNCATED_DIRECTORY;
    }
    data_start = OBJ_RRO_HEADER_SIZE + dir_bytes;

    out->entries = (ObjRroDirEntry *)calloc(count ? count : 1, sizeof(ObjRroDirEntry));
    if (out->entries == NULL) {
        return OBJ_RRO_ERR_ALLOC;
    }
    out->object_count = count;
    out->data_start_offset = (uint32_t)data_start;

    for (i = 0; i < count; i++) {
        const uint8_t *base = buf + OBJ_RRO_HEADER_SIZE + (size_t)i * OBJ_RRO_DIR_ENTRY_SIZE;
        ObjRroDirEntry *e = &out->entries[i];
        uint64_t size;

        e->on_disk_ptr_field = read_i32le(base + 0);
        e->field_a = read_i16le(base + 4);
        e->field_b = read_i16le(base + 6);
        e->field_c = read_i16le(base + 8);
        e->field_d = read_i16le(base + 10);
        e->field_e = read_i16le(base + 12);
        e->field_f = read_i16le(base + 14);

        size = (uint64_t)(uint16_t)e->field_a * OBJ_RRO_MULT_FIELD_A +
               (uint64_t)(uint16_t)e->field_b * OBJ_RRO_MULT_FIELD_B +
               (uint64_t)(uint16_t)e->field_c * OBJ_RRO_MULT_FIELD_C +
               (uint64_t)(uint16_t)e->field_d * OBJ_RRO_MULT_FIELD_D +
               (uint64_t)(uint16_t)e->field_e * OBJ_RRO_MULT_FIELD_E +
               (uint64_t)(uint16_t)e->field_f * OBJ_RRO_MULT_FIELD_F;
        e->computed_size = (uint32_t)size;
        out->sum_computed_size += size;
    }

    out->accounted_bytes = (uint64_t)out->data_start_offset + out->sum_computed_size;
    return OBJ_RRO_OK;
}

void obj_rro_free(ObjRroFile *f) {
    if (!f) return;
    free(f->entries);
    memset(f, 0, sizeof(*f));
}
