/* psx_compat/psx_bios.c
   *
   * Trivial host-side stand-ins for psx_bios.h. Exist only so the compat
 * layer links; none of this is meant to be behaviourally correct yet.
   * See ROADMAP.md phase 4.
 */
#include "psx_bios.h"
#include <time.h>

int PsxBios_VSync(int mode) {
      (void)mode;
    return 0;
}

u32 PsxBios_GetTicks(void) {
      return (u32)clock();
}

u32 PsxBios_ReadPad(int port) {
      (void)port;
    return 0; /* no buttons held */
}

void PsxBios_GpuInit(void) {
      /* no-op: real rasterizer is roadmap phase 3 */
}

void PsxBios_GpuSubmit(const void *primitive) {
      (void)primitive;
    /* no-op */
}
