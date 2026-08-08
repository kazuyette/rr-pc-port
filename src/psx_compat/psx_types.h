// psx_compat/psx_types.h
//
// Minimal PSY-Q-flavoured type aliases so that ported decomp source can be
// dropped in with as few textual changes as possible. This is NOT an
// emulation layer -- it only exists to make host GCC/Clang happy about the
// integer type names the original PSX toolchain (and the decomp project's
// naming conventions) used.
//
// Nothing in here is hardware-accurate; it is purely a compile-time shim.
#ifndef RR_PSX_COMPAT_TYPES_H
#define RR_PSX_COMPAT_TYPES_H

#include <stdint.h>

// NOTE: we deliberately do NOT alias u_char/u_short/u_long here -- those
// names are already defined by glibc's <sys/types.h> (pulled in
// transitively by SDL2 and other system headers) and redefining them
// causes a typedef conflict (u_long ends up as uint32_t here vs
// `unsigned long` from glibc). Nothing in this project's ported source
// actually uses u_char/u_short/u_long -- only the s8/u8/... names below --
// so they were unused aliases and are safe to drop.

typedef int8_t   s8;
typedef uint8_t  u8;
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
typedef int64_t  s64;
typedef uint64_t u64;

// PSY-Q GTE/GPU code commonly uses these short vector/matrix aggregate
// names. Declared here (bodies TBD) so headers that reference pointers to
// them can compile before the real GTE port (roadmap phase 3) lands.
typedef struct { s16 vx, vy, vz, pad; } SVECTOR;
typedef struct { s32 vx, vy, vz, pad; } VECTOR;
typedef struct { s16 m[3][3]; s16 pad; VECTOR t; } MATRIX;

#endif // RR_PSX_COMPAT_TYPES_H
