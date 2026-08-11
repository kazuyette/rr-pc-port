/* psx_vram.c -- see psx_vram.h. The TMS walk mirrors tim.c's confirmed
 * navigation (skip_len chaining, real pixel size = storage_w*h*2). */
#include "psx_vram.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }

int psx_vram_init(PsxVram *v)
{
    v->hw = calloc(PSX_VRAM_W * PSX_VRAM_H, sizeof(uint16_t));
    return v->hw ? 0 : -1;
}

void psx_vram_free(PsxVram *v)
{
    free(v->hw);
    v->hw = NULL;
}

static void blit_block(PsxVram *v, int x, int y, int w_hw, int h,
                       const uint8_t *pix, size_t avail)
{
    int row, col;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w_hw; col++) {
            size_t src = ((size_t)row * w_hw + col) * 2;
            int dx = x + col, dy = y + row;
            if (src + 1 >= avail) return;
            if (dx < 0 || dx >= PSX_VRAM_W || dy < 0 || dy >= PSX_VRAM_H)
                continue;
            v->hw[dy * PSX_VRAM_W + dx] = rd16(pix + src);
        }
    }
}

int psx_vram_load_tms(PsxVram *v, const uint8_t *buf, size_t buf_size)
{
    size_t off = 4; /* outer header */
    int pages = 0;
    while (off + 12 <= buf_size) {
        int32_t skip = (int32_t)rd32(buf + off);
        uint32_t magic = rd32(buf + off + 4);
        uint32_t flag;
        size_t p;
        if (skip <= 0 || magic != 0x10)
            break;
        flag = rd32(buf + off + 8);
        p = off + 12;
        if (flag & 8) { /* CLUT block: len, x, y, w, h, colors */
            uint32_t clen = rd32(buf + p);
            int cx = rds16(buf + p + 4), cy = rds16(buf + p + 6);
            int cw = rd16(buf + p + 8), ch = rd16(buf + p + 10);
            if (p + 12 <= buf_size)
                blit_block(v, cx, cy, cw, ch, buf + p + 12,
                           buf_size - (p + 12));
            p += clen;
        }
        if (p + 12 <= buf_size) { /* pixmap block */
            int px = rds16(buf + p + 4), py = rds16(buf + p + 6);
            int pw = rd16(buf + p + 8), ph = rd16(buf + p + 10);
            blit_block(v, px, py, pw, ph, buf + p + 12,
                       buf_size - (p + 12));
            pages++;
        }
        off += ((size_t)skip & ~3u) + 4u;
    }
    return pages;
}

static uint32_t c15_to_rgba(uint16_t c)
{
    uint32_t r5 = c & 0x1F, g5 = (c >> 5) & 0x1F, b5 = (c >> 10) & 0x1F;
    uint32_t r = (r5 << 3) | (r5 >> 2);
    uint32_t g = (g5 << 3) | (g5 >> 2);
    uint32_t b = (b5 << 3) | (b5 >> 2);
    if (c == 0)
        return 0; /* PS1 convention: raw 0 = fully transparent */
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

uint32_t psx_vram_sample(const PsxVram *v, uint16_t tpage, uint16_t clut,
                         uint8_t u, uint8_t tv)
{
    int page_x = (tpage & 0xF) * 64;
    int page_y = ((tpage >> 4) & 1) * 256;
    int clut_x = (clut & 0x3F) * 16;
    int clut_y = (clut >> 6) & 0x1FF;
    /* 4bpp: 4 texels per halfword */
    int hx = page_x + (u >> 2);
    int hy = page_y + tv;
    uint16_t hw, idx, col;
    if (hx < 0 || hx >= PSX_VRAM_W || hy < 0 || hy >= PSX_VRAM_H)
        return 0;
    hw = v->hw[hy * PSX_VRAM_W + hx];
    idx = (uint16_t)((hw >> ((u & 3) * 4)) & 0xF);
    if (clut_x + idx >= PSX_VRAM_W || clut_y >= PSX_VRAM_H)
        return 0;
    col = v->hw[clut_y * PSX_VRAM_W + clut_x + idx];
    return c15_to_rgba(col);
}

void psx_vram_bake_page(const PsxVram *v, uint16_t tpage, uint16_t clut,
                        uint32_t *dst)
{
    int u, tv;
    for (tv = 0; tv < 256; tv++)
        for (u = 0; u < 256; u++)
            dst[tv * 256 + u] =
                psx_vram_sample(v, tpage, clut, (uint8_t)u, (uint8_t)tv);
}
