/* psx_compat/psx_bios.h
   *
   * Stub declarations for the handful of PS1 BIOS / SDK entry points that
 * asm-locked decomp functions call out to (VSync, pad reads, GPU DMA
 * kicks, etc). None of these are implemented yet -- this header exists so
 * that future ported .c files have something to '#include' and link
 * against while the real host-side implementations (roadmap phase 4) are
 * being written.
 *
 * Every stub currently either no-ops or returns a fixed placeholder value.
 * Do not rely on any of these for real behaviour yet.
 */
#ifndef RR_PSX_COMPAT_BIOS_H
#define RR_PSX_COMPAT_BIOS_H

#include "psx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Timing */
int   PsxBios_VSync(int mode);          /* real BIOS VSync(mode) */
u32   PsxBios_GetTicks(void);           /* host wall-clock substitute */

/* Pad input (placeholder bitfield, PSX SIO layout TBD) */
u32   PsxBios_ReadPad(int port);

/* GPU (real rasterizer is roadmap phase 3 -- these are no-ops for now) */
void  PsxBios_GpuInit(void);
void  PsxBios_GpuSubmit(const void *primitive);

#ifdef __cplusplus
}
#endif

#endif /* RR_PSX_COMPAT_BIOS_H */
