/* trackdata.c -- see trackdata.h for the full format writeup. */
#include "trackdata.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PSEXE_HEADER_SIZE 2048u
#define PSEXE_MAGIC "PS-X EXE"

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int16_t rd_s16le(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t rd_s32le(const uint8_t *p) {
    return (int32_t)rd_u32le(p);
}

int trackdata_read_exe_header(const uint8_t *buf, size_t buf_size, PsExeHeader *out) {
    if (buf == NULL || out == NULL) return TRACKDATA_ERR_ARGS;
    memset(out, 0, sizeof(*out));
    if (buf_size < PSEXE_HEADER_SIZE) return TRACKDATA_ERR_ARGS;
    if (memcmp(buf, PSEXE_MAGIC, 8) != 0) return TRACKDATA_ERR_NOT_PSEXE;

    /* Standard PS-X EXE header layout (all little-endian):
     *   0x00  8 bytes  "PS-X EXE" magic
     *   0x08  8 bytes  zero-filled
     *   0x10  u32      pc0 (initial $pc)
     *   0x14  u32      gp0 (initial $gp)
     *   0x18  u32      t_addr (text load address)
     *   0x1C  u32      t_size (text size in bytes)
     * (fields past this point -- data_addr/size, bss, sp/fp base, etc.
     * -- aren't needed for this tool.) */
    out->pc0 = rd_u32le(buf + 0x10);
    out->gp0 = rd_u32le(buf + 0x14);
    out->t_addr = rd_u32le(buf + 0x18);
    out->t_size = rd_u32le(buf + 0x1C);
    return TRACKDATA_OK;
}

int trackdata_ram_to_file_offset(const PsExeHeader *hdr, uint32_t ram_addr, size_t *out_offset) {
    if (hdr == NULL || out_offset == NULL) return TRACKDATA_ERR_ARGS;
    if (ram_addr < hdr->t_addr || (ram_addr - hdr->t_addr) >= hdr->t_size) {
        return TRACKDATA_ERR_OUT_OF_RANGE;
    }
    *out_offset = (size_t)PSEXE_HEADER_SIZE + (size_t)(ram_addr - hdr->t_addr);
    return TRACKDATA_OK;
}

int trackdata_parse(const uint8_t *exe_buf, size_t exe_size, uint32_t ram_addr,
                     size_t count, TrackData *out) {
    PsExeHeader hdr;
    size_t file_off;
    size_t i;
    int rc;

    if (out == NULL) return TRACKDATA_ERR_ARGS;
    memset(out, 0, sizeof(*out));
    if (exe_buf == NULL || count == 0) return TRACKDATA_ERR_ARGS;

    rc = trackdata_read_exe_header(exe_buf, exe_size, &hdr);
    if (rc != TRACKDATA_OK) return rc;

    rc = trackdata_ram_to_file_offset(&hdr, ram_addr, &file_off);
    if (rc != TRACKDATA_OK) return rc;

    if (file_off + count * TRACKDATA_RECORD_SIZE > exe_size) {
        return TRACKDATA_ERR_OUT_OF_RANGE;
    }

    out->sections = (TrackSection *)calloc(count, sizeof(TrackSection));
    if (out->sections == NULL) return TRACKDATA_ERR_ALLOC;
    out->count = count;

    for (i = 0; i < count; i++) {
        const uint8_t *rec = exe_buf + file_off + i * TRACKDATA_RECORD_SIZE;
        TrackSection *sec = &out->sections[i];

        sec->x = (double)rd_s32le(rec + 0x00) / 16384.0;
        sec->z = (double)rd_s32le(rec + 0x04) / 16384.0;
        sec->aux_a_raw = rd_s16le(rec + 0x08);
        sec->heading_raw = rd_s16le(rec + 0x0A);
        sec->aux_heading_raw = rd_s16le(rec + 0x0C);
        sec->width_right = (double)rd_s16le(rec + 0x0E) / 32.0;
        sec->width_left = (double)rd_s16le(rec + 0x10) / 32.0;
    }

    return TRACKDATA_OK;
}

void trackdata_free(TrackData *td) {
    if (td == NULL) return;
    free(td->sections);
    td->sections = NULL;
    td->count = 0;
}

int trackdata_project_point(const TrackSection *sec, double dx, double dz,
                             double *out_along, double *out_lateral) {
    /* heading_raw is in BAM12 units (4096 = one full turn); the PS1 code
     * feeds `heading - 0xC00` (a fixed 3072-unit / 270-degree axis
     * realignment) to its sin/cos lookups before rotating -- reproduced
     * here in radians instead of the original's fixed-point BAM tables.
     * See trackdata.h's header comment: this function reproduces the
     * CONFIRMED structure (rotate the offset into the section's local
     * forward/right frame, compare the right-of-center component
     * against the section's asymmetric width) using plain real-world
     * doubles rather than PS1 fixed-point, since the exact fixed-point
     * SCALE of the original trig lookup (func_80044D0C/func_80044E2C)
     * was not independently confirmed this round -- only the shape of
     * the formula was. The along-track progress is reported as a real
     * signed distance (not the original's clamped 0..255 byte encoding,
     * a PS1 storage-size convenience the PC port has no reason to
     * replicate). */
    const double bam_to_rad = 6.283185307179586 / 4096.0;
    double angle = ((double)sec->heading_raw - 3072.0) * bam_to_rad;
    double s = sin(angle);
    double c = cos(angle);
    /* forward = (sin, cos), right = (cos, -sin) -- matches this port's
     * own convention (src/physics.c's physics_car_integrate: x +=
     * sin(heading)*speed, z += cos(heading)*speed, so heading 0 faces
     * +Z and +X is to the right of that). This sign choice is this
     * port's own, not a verbatim copy of the PS1 code's register order
     * (which was traced faithfully for the *shape* of the formula, see
     * trackdata.h, but not for which operand sign the original happened
     * to assign to "left" vs "right" in its own registers). */
    double along = dx * s + dz * c;
    double lateral = dx * c - dz * s;
    double width = (lateral > 0.0) ? sec->width_right : sec->width_left;

    if (out_along) *out_along = along;
    if (out_lateral) *out_lateral = lateral;

    return (lateral < 0.0 ? -lateral : lateral) > width;
}
