/* tim.c -- see tim.h for the full format writeup (confirmed layout,
 * the pixmap-length caveat, and documented limitations).
 */
#include "tim.h"

#include <stdlib.h>
#include <string.h>

#define TIM_MAGIC 0x00000010u
#define TIM_MAX_CLUT_ROW_COLORS 256

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int16_t read_i16le(const uint8_t *p) {
    return (int16_t)read_u16le(p);
}

/* Expands a 5-bit channel to 8-bit, PS1-standard (v<<3)|(v>>2). */
static int expand5(int v) {
    return (v << 3) | (v >> 2);
}

/* Decodes one raw PS1 16-bit color (5-5-5-1) into an RGBA8888 texel.
 * Alpha is always 255 -- see tim.h LIMITATIONS re: no transparency-key
 * handling. */
static uint32_t decode_color16(uint16_t c) {
    int r = c & 0x1F;
    int g = (c >> 5) & 0x1F;
    int b = (c >> 10) & 0x1F;
    return TIM_RGBA(expand5(r), expand5(g), expand5(b), 255);
}

static int tim_pages_grow(TimFile *f, size_t *capacity) {
    size_t new_cap = (*capacity == 0) ? 8 : (*capacity * 2);
    TimPage *new_pages = (TimPage *)realloc(f->pages, new_cap * sizeof(TimPage));
    if (new_pages == NULL) {
        return 0;
    }
    f->pages = new_pages;
    *capacity = new_cap;
    return 1;
}

static void free_pages(TimFile *f) {
    size_t i;
    if (f->pages == NULL) {
        return;
    }
    for (i = 0; i < f->page_count; i++) {
        free(f->pages[i].rgba);
    }
    free(f->pages);
    f->pages = NULL;
    f->page_count = 0;
}

int tim_parse(const uint8_t *buf, size_t buf_size, TimFile *out) {
    size_t capacity = 0;
    uint32_t off;

    if (out == NULL || buf == NULL) {
        return TIM_ERR_ARGS;
    }
    memset(out, 0, sizeof(*out));

    if (buf_size < 4) {
        return TIM_ERR_TRUNCATED_HEADER;
    }

    /* Outer header (4 bytes, purpose unconfirmed) -- skipped. */
    off = 4;

    for (;;) {
        uint32_t skip_len, tim_id, tim_flag;
        uint32_t p;
        TimPage page;
        int clut_row0[TIM_MAX_CLUT_ROW_COLORS];
        int clut_row0_count = 0;

        if ((uint64_t)off + 8 > buf_size) {
            break; /* not enough room left for even skip_len+tim_id -- clean end */
        }
        skip_len = read_u32le(buf + off);
        if ((int32_t)skip_len <= 0) {
            break; /* sentinel: end of page stream */
        }
        tim_id = read_u32le(buf + off + 4);
        if (tim_id != TIM_MAGIC) {
            break; /* misaligned or trailing garbage -- stop, keep what we found */
        }

        p = off + 8;
        if ((uint64_t)p + 4 > buf_size) {
            break;
        }
        tim_flag = read_u32le(buf + p);
        p += 4;

        memset(&page, 0, sizeof(page));
        page.page_offset = off;
        page.mode = (TimPixelMode)(tim_flag & 3u);
        page.has_clut = (tim_flag & 8u) ? 1 : 0;

        if (page.has_clut) {
            uint32_t clut_len, colors_off;
            int i;

            if ((uint64_t)p + 12 > buf_size) {
                break;
            }
            clut_len = read_u32le(buf + p);
            page.clut_vram_x = read_i16le(buf + p + 4);
            page.clut_vram_y = read_i16le(buf + p + 6);
            page.clut_colors_per_row = read_u16le(buf + p + 8);
            page.clut_rows = read_u16le(buf + p + 10);

            colors_off = p + 12;
            clut_row0_count = page.clut_colors_per_row;
            if (clut_row0_count > TIM_MAX_CLUT_ROW_COLORS) {
                clut_row0_count = TIM_MAX_CLUT_ROW_COLORS; /* defensive clamp, not expected in real data */
            }
            if (clut_row0_count > 0 && (uint64_t)colors_off + (uint64_t)clut_row0_count * 2 <= buf_size) {
                for (i = 0; i < clut_row0_count; i++) {
                    clut_row0[i] = read_u16le(buf + colors_off + (uint32_t)i * 2);
                }
            } else {
                clut_row0_count = 0; /* CLUT row 0 didn't fit -- can't shade indexed pixels below */
            }

            if ((uint64_t)p + clut_len > buf_size) {
                break; /* declared CLUT block runs past the buffer */
            }
            p += clut_len;
        }

        /* Pixmap block header. */
        if ((uint64_t)p + 12 > buf_size) {
            break;
        }
        {
            uint32_t storage_w, height, data_off, pixel_bytes;
            int pixel_width;

            page.declared_pixmap_len = read_u32le(buf + p);
            page.vram_x = read_i16le(buf + p + 4);
            page.vram_y = read_i16le(buf + p + 6);
            storage_w = read_u16le(buf + p + 8);
            height = read_u16le(buf + p + 10);
            data_off = p + 12;

            switch (page.mode) {
                case TIM_MODE_4BPP_CLUT:  pixel_width = (int)storage_w * 4; break;
                case TIM_MODE_8BPP_CLUT:  pixel_width = (int)storage_w * 2; break;
                case TIM_MODE_16BPP_DIRECT: pixel_width = (int)storage_w; break;
                default: pixel_width = (int)storage_w; break; /* 24bpp: not decoded, width left in storage units */
            }
            page.width = pixel_width;
            page.height = (int)height;

            /* Real in-file pixel data size -- NOT the declared pixmap
             * `len` field, see tim.h IMPORTANT CAVEAT. Holds for modes
             * 0/1/2 uniformly (classic TIM formula: storage_w
             * halfwords per row). */
            pixel_bytes = storage_w * height * 2u;

            if (page.mode != TIM_MODE_24BPP_DIRECT && pixel_width > 0 && height > 0 &&
                (uint64_t)data_off + pixel_bytes <= buf_size) {
                size_t npix = (size_t)pixel_width * (size_t)height;
                uint32_t *rgba = (uint32_t *)malloc(npix * sizeof(uint32_t));
                if (rgba == NULL) {
                    free_pages(out);
                    return TIM_ERR_ALLOC;
                }
                if (page.mode == TIM_MODE_4BPP_CLUT) {
                    size_t idx;
                    for (idx = 0; idx < npix; idx++) {
                        uint8_t byte = buf[data_off + idx / 2];
                        int nibble = (idx & 1) ? (byte >> 4) & 0xF : byte & 0xF;
                        uint16_t c = (nibble < clut_row0_count) ? (uint16_t)clut_row0[nibble] : 0;
                        rgba[idx] = decode_color16(c);
                    }
                } else if (page.mode == TIM_MODE_8BPP_CLUT) {
                    size_t idx;
                    for (idx = 0; idx < npix; idx++) {
                        uint8_t index = buf[data_off + idx];
                        uint16_t c = (index < clut_row0_count) ? (uint16_t)clut_row0[index] : 0;
                        rgba[idx] = decode_color16(c);
                    }
                } else { /* TIM_MODE_16BPP_DIRECT */
                    size_t idx;
                    for (idx = 0; idx < npix; idx++) {
                        uint16_t c = read_u16le(buf + data_off + idx * 2);
                        rgba[idx] = decode_color16(c);
                    }
                }
                page.rgba = rgba;
            } else {
                page.rgba = NULL; /* unsupported mode or truncated -- see tim.h LIMITATIONS */
            }
        }

        if (capacity == out->page_count) {
            if (!tim_pages_grow(out, &capacity)) {
                free_pages(out);
                return TIM_ERR_ALLOC;
            }
        }
        out->pages[out->page_count] = page;
        out->page_count++;

        off = off + (skip_len & ~3u) + 4u;
    }

    return TIM_OK;
}

void tim_free(TimFile *f) {
    if (f == NULL) {
        return;
    }
    free_pages(f);
    memset(f, 0, sizeof(*f));
}
