/* tim.h -- standalone (host-only, no PSX dependencies) parser/decoder
 * for Ridge Racer 1 (PS1, 1994, Namco)'s TEX0.TMS .. TEX4.TMS texture
 * files: containers of back-to-back PS1 "TIM"-format texture pages,
 * each prefixed with an extra 4-byte streaming-skip word (this game's
 * own CD-streaming convention, not part of the standard bare TIM
 * format).
 *
 * Reverse-engineered by tracing PS1 function FUN_80037d38 in the
 * decompiled binary and cross-checked byte-for-byte against the real
 * TEX0.TMS from a legally owned disc image (that file itself, and
 * every other TEX*.TMS, is never committed to this repo -- see
 * texparse_main.c).
 *
 * CONFIRMED file layout:
 *
 *   file := u32 outer_header       (purpose unconfirmed, seen 0x00000100
 *                                    in the real TEX0.TMS -- skipped/
 *                                    ignored by this parser)
 *           page* (repeated until a sentinel skip_len <= 0, a tim_id
 *                  magic mismatch, or the buffer runs out)
 *
 *   page := u32 skip_len            bytes to advance, MEASURED FROM THE
 *                                    START OF skip_len itself, to reach
 *                                    the NEXT page's skip_len field:
 *                                      next_page_off = this_page_off
 *                                                    + (skip_len & ~3) + 4
 *           u32 tim_id               literal 0x00000010 in every real
 *                                    page (classic PS1 TIM magic) --
 *                                    validated on every page to detect
 *                                    misalignment/corruption.
 *           u32 tim_flag              bits0-1 = pixel mode (0=4bpp CLUT,
 *                                    1=8bpp CLUT, 2=16bpp direct,
 *                                    3=24bpp direct -- NOT decoded by
 *                                    this parser, see below); bit3
 *                                    (0x8) = a CLUT block is present.
 *           clut_block?               present only if tim_flag & 8
 *           pixmap_block
 *
 *   clut_block := u32 len            total bytes of this block
 *                                    INCLUDING this 12-byte header
 *                 i16 vram_x, vram_y
 *                 u16 w, h            w = CLUT colors per row (16 for
 *                                    4bpp, 256 for 8bpp typically), h =
 *                                    number of stacked CLUT rows
 *                                    (multiple palettes for the same
 *                                    pixmap). This parser only decodes
 *                                    ROW 0 -- see LIMITATIONS below.
 *                 u16 colors[w*h]     PS1 16-bit color: bit15 = stp
 *                                    (semi-transparency flag), bits
 *                                    10-14 = B(5), bits5-9 = G(5),
 *                                    bits0-4 = R(5). Each 5-bit channel
 *                                    is expanded to 8-bit via
 *                                    (v<<3)|(v>>2).
 *
 *   pixmap_block := u32 len          declared total bytes INCLUDING
 *                                    this 12-byte header -- see the
 *                                    IMPORTANT CAVEAT below, this field
 *                                    is NOT used by this parser to size
 *                                    the pixel read.
 *                   i16 vram_x, vram_y
 *                   u16 w, h          w is in STORAGE units (16-bit
 *                                    halfwords per row): actual pixel
 *                                    width = w * (4 for 4bpp, 2 for
 *                                    8bpp, 1 for 16bpp). h is the true
 *                                    pixel row count.
 *                   u8 pixels[]       4bpp: 2 indices/byte, low nibble
 *                                    first. 8bpp: 1 index/byte. 16bpp:
 *                                    raw 5-5-5-1 colors, no CLUT lookup.
 *
 * IMPORTANT CAVEAT found this session by an independent hex/offset
 * sanity check against the real TEX0.TMS (per the task's instruction to
 * verify before trusting the write-up blindly):
 *
 *   The pixmap_block's declared `len` field does NOT match the actual
 *   number of pixel bytes present in the file. Measured directly (by
 *   computing the true byte gap between one page's pixmap-data start
 *   and the NEXT page's skip_len field, using the confirmed skip_len
 *   navigation formula, and finding zero slack bytes left over), the
 *   real, in-file pixel data size is exactly:
 *
 *       real_pixel_bytes = storage_w * height * 2
 *
 *   ...for all of modes 0/1/2 (this is also the classic/standard TIM
 *   formula: each row occupies storage_w 16-bit halfwords regardless of
 *   bpp). But the file's declared pixmap `len` field instead reports
 *   len-12 == storage_w * height * 4 -- exactly DOUBLE the real size,
 *   consistently, across every page checked (verified across all 41
 *   pages of the real TEX0.TMS's initial run). The cause is unknown
 *   (possibly a padded/doubled VRAM-footprint bookkeeping value from
 *   the original PS1 texture manager, unrelated to actual bytes stored
 *   on disc) -- but the practical upshot is: DO NOT use the pixmap
 *   `len` field to size the pixel read or to locate the next page.
 *   This parser instead:
 *     - navigates page-to-page purely via each page's own skip_len
 *       field (confirmed reliable: tim_id magic validates correctly at
 *       every computed next-page offset), and
 *     - sizes the pixel read as storage_w * height * 2 bytes (confirmed
 *       reliable: exactly fills the real gap to the next page with zero
 *       slack, and visually decodes as coherent texture art, not
 *       noise -- see texparse_main.c --dump-page output).
 *   The declared pixmap `len` is still exposed in TimPage (informational
 *   only, e.g. for display in the CLI summary table) but never trusted
 *   for parsing.
 *
 * LIMITATIONS (deliberate, proof-of-concept scope):
 *   - Multi-row CLUTs (h > 1, i.e. multiple stacked palettes sharing one
 *     pixmap) only ever use row 0. Real pages seen so far in TEX0.TMS
 *     all have CLUT h == 1, so this hasn't been observed to matter, but
 *     a page that legitimately needs palette selection at draw time
 *     would render with the wrong palette under this parser. Selecting
 *     the "active" row would need extra runtime context (which palette
 *     index the game selects per-material) that isn't reconstructed
 *     here -- documented, not solved.
 *   - Mode 3 (24bpp direct) pixel data is NOT decoded (packed weirdly,
 *     out of scope per the task) -- TimPage.rgba is left NULL for such
 *     pages, but the page's header fields (vram/w/h/mode) are still
 *     recorded so the CLI summary table can list it.
 *   - No semi-transparency (stp bit) or color-key transparency handling:
 *     every decoded pixel gets alpha=255. Real PS1 TIM CLUT convention
 *     often treats the raw 16-bit value 0x0000 as "fully transparent",
 *     but that's not implemented here to keep the decode path simple
 *     for this proof-of-concept.
 */
#ifndef RR_TIM_H
#define RR_TIM_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TIM_MODE_4BPP_CLUT = 0,
    TIM_MODE_8BPP_CLUT = 1,
    TIM_MODE_16BPP_DIRECT = 2,
    TIM_MODE_24BPP_DIRECT = 3, /* not decoded, see LIMITATIONS above */
} TimPixelMode;

/* One decoded TIM page. `rgba` is width*height uint32_t texels packed
 * as TIM_RGBA(r,g,b,a) (see macro below) -- byte order R,G,B,A when
 * read little-endian, i.e. R in the lowest byte. NULL if this page's
 * mode is unsupported (24bpp) or the pixel data didn't fit in the
 * source buffer (truncated file). */
typedef struct {
    uint32_t page_offset; /* absolute byte offset of this page's skip_len field, for debugging */
    TimPixelMode mode;
    int has_clut;
    int clut_vram_x, clut_vram_y;
    int clut_colors_per_row; /* CLUT w */
    int clut_rows;           /* CLUT h; only row 0 is decoded/used */
    int vram_x, vram_y;      /* pixmap placement, informational only */
    int width, height;       /* actual pixel dimensions (already expanded from storage units) */
    uint32_t declared_pixmap_len; /* raw len field from the file -- informational only, see IMPORTANT CAVEAT above, do NOT use for sizing */
    uint32_t *rgba;          /* [width*height], owned, or NULL -- see field doc above */
} TimPage;

typedef struct {
    TimPage *pages;   /* [page_count], owned */
    size_t page_count;
} TimFile;

#define TIM_OK 0
#define TIM_ERR_TRUNCATED_HEADER (-1) /* buffer too small to even hold the 4-byte outer header */
#define TIM_ERR_ALLOC (-2)
#define TIM_ERR_ARGS (-3)

/* Packs an RGBA8888 texel: R in the lowest byte, A in the highest
 * (little-endian byte order R,G,B,A). Used both by tim.c's decoder and
 * by anything sampling a TimPage.rgba buffer (e.g. gpu_draw_quad_textured). */
#define TIM_RGBA(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* Parses a TEX*.TMS buffer already loaded into memory (e.g. via
 * fread()). Walks pages via skip_len navigation until a sentinel
 * (skip_len <= 0), a tim_id magic mismatch, or the buffer is
 * exhausted -- any of these cleanly ends the page stream rather than
 * being treated as a hard error, since malformed/truncated inputs are
 * expected during exploration. Genuinely degenerate inputs (buffer
 * can't even hold the 4-byte outer header, or a NULL buf) return an
 * error and leave *out zeroed.
 *
 * On success (TIM_OK), *out is fully populated (possibly with zero
 * pages, if nothing valid was found) and owns allocated memory that
 * must be released with tim_free(). On failure, *out is left zeroed
 * (safe to pass to tim_free() regardless). */
int tim_parse(const uint8_t *buf, size_t buf_size, TimFile *out);

/* Frees memory owned by *f (including every page's rgba buffer) and
 * zeroes it. Safe to call multiple times or on an all-zero TimFile. */
void tim_free(TimFile *f);

#endif /* RR_TIM_H */
