/* psx_vram.h -- ROUND 45: a software recreation of the PS1's 1024x512
 * 16-bit VRAM, filled by blitting TEX*.TMS pages (and their CLUTs) at
 * the vram_x/vram_y destinations each TIM page declares -- exactly
 * what the console's DMA does at load time. With VRAM rebuilt, the
 * MAP.RRM per-record texture references decode naturally:
 *
 *   THE ROUND-45 BREAKTHROUGH (traced in func_8003486C, the track
 *   quad emitter called 16x by the track renderer func_800163E4):
 *   MAP.RRM's 16 undecoded record bytes (24-39) are copied VERBATIM
 *   into POLY_FT4 GPU packets:
 *     bytes 24-25 = u0,v0        bytes 26-27 = CLUT id
 *     bytes 28-29 = u1,v1        bytes 30-31 = TPAGE id
 *     bytes 32-33 = u2,v2        bytes 34-35 = ordering-table bias
 *     bytes 36-37 = u3,v3        bytes 38-39 = (still open)
 *   Validated against the real MAP.RRM: tpage values decode to valid
 *   4bpp VRAM pages exactly where the TEX banks load, CLUT ids to the
 *   y=480..509 palette rows the TMS files place there, and the UVs
 *   form clean axis-aligned rects. This closes Phase 5's "16 unknown
 *   bytes" and rounds 44's group_id/heading/unk hypotheses: group_id
 *   was the OT bias, "heading" was the CLUT id, unk_1e the tpage.
 */
#ifndef RR_PSX_VRAM_H
#define RR_PSX_VRAM_H

#include <stddef.h>
#include <stdint.h>

#define PSX_VRAM_W 1024
#define PSX_VRAM_H 512

typedef struct {
    uint16_t *hw; /* [PSX_VRAM_W * PSX_VRAM_H], owned */
} PsxVram;

/* Allocates a zeroed VRAM. Returns 0 on success. */
int psx_vram_init(PsxVram *v);
void psx_vram_free(PsxVram *v);

/* Blits every page (pixels AND CLUT rows) of a TEX*.TMS buffer into
 * VRAM at the destinations the file declares. Returns the number of
 * pages blitted (0 on parse failure -- same skip_len walk as tim.c). */
int psx_vram_load_tms(PsxVram *v, const uint8_t *buf, size_t buf_size);

/* Samples one texel like the PS1 GPU: tpage id (4bpp assumed, mode
 * bits ignored beyond position), CLUT id, and u,v in 0..255 texture
 * pixels. Returns RGBA8888 (R low byte), alpha 255, or 0 for the
 * fully-transparent raw-0 texel. */
uint32_t psx_vram_sample(const PsxVram *v, uint16_t tpage, uint16_t clut,
                         uint8_t u, uint8_t tv);

/* Renders the 256x256-texel page+clut combination into caller storage
 * (dst[256*256] RGBA) for fast page-cache use. */
void psx_vram_bake_page(const PsxVram *v, uint16_t tpage, uint16_t clut,
                        uint32_t *dst);

#endif /* RR_PSX_VRAM_H */
