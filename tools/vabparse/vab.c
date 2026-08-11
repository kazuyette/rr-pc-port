/* vab.c -- see vab.h for the format writeup (confirmed round 58). */
#include "vab.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int vab_parse(const uint8_t *vh, size_t vh_size, VabHeader *out)
{
    size_t toff, voff;
    int p, t, active = 0, i;
    if (vh_size < 0x20 + 128 * 16 || memcmp(vh, "pBAV", 4) != 0)
        return -1;
    memset(out, 0, sizeof *out);
    out->tones = rd16(vh + 0x14);
    out->vags = rd16(vh + 0x16);
    for (p = 0; p < 128; p++)
        out->prog_tone_count[p] = vh[0x20 + p * 16];
    toff = 0x20 + 128 * 16;
    for (p = 0; p < 128; p++) {
        int nt = out->prog_tone_count[p];
        if (nt == 0)
            continue;
        if (toff + (size_t)(active + 1) * 16 * 32 > vh_size)
            return -2;
        for (t = 0; t < nt && t < 16; t++) {
            const uint8_t *e = vh + toff + (size_t)active * 16 * 32 + (size_t)t * 32;
            out->tone[p][t].prio = e[0];
            out->tone[p][t].mode = e[1];
            out->tone[p][t].vol = e[2];
            out->tone[p][t].pan = e[3];
            out->tone[p][t].center = e[4];
            out->tone[p][t].shift = e[5];
            out->tone[p][t].note_min = e[6];
            out->tone[p][t].note_max = e[7];
            out->tone[p][t].vag = rd16(e + 22);
        }
        active++;
    }
    out->programs = active;
    voff = toff + (size_t)active * 16 * 32;
    if (voff + (size_t)(out->vags + 1) * 2 > vh_size)
        return -3;
    {
        uint32_t acc = 0;
        for (i = 1; i <= out->vags && i < 64; i++) {
            uint32_t len = (uint32_t)rd16(vh + voff + (size_t)i * 2) * 8u;
            out->vag_off[i] = acc;
            out->vag_len[i] = len;
            acc += len;
        }
    }
    return 0;
}

size_t vab_decode_vag(const VabHeader *h, int vag_index,
                      const uint8_t *vb, size_t vb_size,
                      int16_t *pcm, size_t max_samples)
{
    static const int f0[5] = { 0, 60, 115, 98, 122 };
    static const int f1[5] = { 0, 0, -52, -55, -60 };
    size_t off, end, n = 0;
    int h1 = 0, h2 = 0;
    if (vag_index < 1 || vag_index >= 64 || h->vag_len[vag_index] == 0)
        return 0;
    off = h->vag_off[vag_index];
    end = off + h->vag_len[vag_index];
    if (end > vb_size)
        end = vb_size;
    for (; off + 16 <= end; off += 16) {
        int shift = vb[off] & 0xF;
        int filt = (vb[off] >> 4) & 0xF;
        int i;
        if (filt > 4)
            filt = 0;
        for (i = 0; i < 28 && n < max_samples; i++) {
            int nib = (vb[off + 2 + i / 2] >> ((i & 1) * 4)) & 0xF;
            int s = nib << 12;
            int v;
            if (s & 0x8000)
                s -= 0x10000;
            s >>= shift;
            v = s + (h1 * f0[filt] + h2 * f1[filt]) / 64;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            pcm[n++] = (int16_t)v;
            h2 = h1;
            h1 = v;
        }
    }
    return n;
}
