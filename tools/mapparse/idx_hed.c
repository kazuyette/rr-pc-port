/* idx_hed.c -- see idx_hed.h for the full confirmed/hypothesis writeup. */

#include "idx_hed.h"
#include <string.h>

int idx_hed_parse(const uint8_t *buf, size_t buf_size, IdxHedFile *out) {
    int row, col, i;

    if (!buf || !out) {
        return IDX_HED_ERR_NULL_ARG;
    }
    if (buf_size != IDX_HED_EXPECTED_FILE_SIZE) {
        return IDX_HED_ERR_BAD_SIZE;
    }

    memset(out, 0, sizeof(*out));
    for (i = 0; i < 512; i++) {
        out->section_to_col[i] = -1;
        out->section_to_row[i] = -1;
    }
    out->max_section_seen = -1;

    for (row = 0; row < IDX_HED_GRID_DIM; row++) {
        for (col = 0; col < IDX_HED_GRID_DIM; col++) {
            size_t idx = (size_t)(row * IDX_HED_GRID_DIM + col);
            uint16_t lo = buf[idx * 2 + 0];
            uint16_t hi = buf[idx * 2 + 1];
            int16_t v = (int16_t)(lo | (hi << 8));
            out->grid[row][col] = v;
            if (v != IDX_HED_EMPTY_CELL && v >= 0 && v < 512) {
                out->section_to_col[v] = col;
                out->section_to_row[v] = row;
                if (v > out->max_section_seen) {
                    out->max_section_seen = v;
                }
            }
        }
    }
    out->section_to_cell_valid = 1;
    return IDX_HED_OK;
}

int idx_hed_section_cell_raw(const IdxHedFile *f, int section, int *col, int *row) {
    if (!f || section < 0 || section >= 512) {
        return IDX_HED_ERR_SECTION_OUT_OF_RANGE;
    }
    if (f->section_to_col[section] < 0) {
        return IDX_HED_ERR_SECTION_NOT_FOUND;
    }
    if (col) *col = f->section_to_col[section];
    if (row) *row = f->section_to_row[section];
    return IDX_HED_OK;
}

int idx_hed_section_world_origin(const IdxHedFile *f, int section,
                                  int32_t *world_x, int32_t *world_z) {
    int col, row, mirrored_col;
    int status = idx_hed_section_cell_raw(f, section, &col, &row);
    if (status != IDX_HED_OK) {
        return status;
    }
    /* HYPOTHESIS (empirically best of 6 tried combinations this round):
     * mirror the column axis, do not swap axes, no rotation. */
    mirrored_col = (IDX_HED_GRID_DIM - 1) - col;
    if (world_x) *world_x = (int32_t)mirrored_col * IDX_HED_CELL_SIZE_WORLD_UNITS;
    if (world_z) *world_z = (int32_t)row * IDX_HED_CELL_SIZE_WORLD_UNITS;
    return IDX_HED_OK;
}
