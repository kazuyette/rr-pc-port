/* Ported from rr-decomp (github.com/kazuyette/rr-decomp) src/more_asm*.c.
 *
 * Phase 2, round 1: 39 functions that used to be raw __asm__ MIPS
 * transcriptions in rr-decomp (byte-exact matches to the original PSX
 * binary, but not portable C) were re-derived here as genuine,
 * hand-written C -- then proven byte-identical by compiling them with
 * the project's GCC 2.7.2 PSX cross-compiler and diffing against the
 * original object with objdiff-cli (fuzzy_match_percent == 100.0). That
 * proof lives in rr-decomp; this file carries the same portable C logic
 * into the PC port, with PSX RAM globals turned into plain host statics
 * (see psx_compat/ -- nothing here is hardware-dependent).
 *
 * ~65 candidates were attempted this round; the ~26 that resisted
 * matching (mostly GCC 2.7.2 -O2 instruction-scheduling quirks: hoisting
 * independent constant loads ahead of dependent ones, or folding a
 * symbol's low 16 bits into a load/store's immediate field instead of
 * materializing the full address via addiu) are still __asm__ in
 * rr-decomp and are NOT ported here -- only functions verified
 * byte-exact as real C are considered "ported".
 */

#include "psx_compat/psx_types.h"
#include "ported_logic.h"

/* --- backing storage for the PSX globals these functions touch --- */

static s16 D_800775F8;
static s32 D_8012CDA8;
static s32 D_80077378;
static s32 D_80077374;
static s16 D_80077460;
static s16 D_80077462;
static s16 D_80079B74;
static s32 D_80076E04;

static s32 D_80173148_target;
static s32 *D_80173148 = &D_80173148_target;

static s32 D_80077380;
static s32 D_80077474;
static s32 D_80077578;

/* Original global is a pointer to a short array (SPU voice table or
 * similar); give it real backing storage here so the port is usable
 * standalone. */
static u16 D_8007758C_backing[64];
static u16 *D_8007758C = D_8007758C_backing;

static s32 D_800776A8;
static s32 D_800776B0;
static s32 D_800776B8;

/* --- trivial no-op stubs (empty function bodies, 2-instruction
 * "jr ra / nop" in the original) --- */

void func_800129A4(void) {
}

void func_80018A94(void) {
}

void func_8002E3F8(void) {
}

void func_8003DE88(void) {
}

void func_8004B528(void) {
}

/* --- return-constant --- */

int func_80055800(void) {
    return 3;
}

int func_80055808(void) {
    return 1;
}

/* --- simple global getters/setters --- */

void func_8004A4CC(void) {
    D_800775F8 = 0;
}

int func_80059040(void) {
    return D_8012CDA8 == 0;
}

int func_80045718(void) {
    return D_80077378;
}

int func_80045728(void) {
    return D_80077374;
}

void func_8004984C(void) {
    D_80077460 = 0;
}

void func_8004985C(short a0) {
    D_80077462 = a0;
}

short func_8004D460(void) {
    return D_80079B74;
}

void func_800534B8(int a0) {
    D_80076E04 = a0;
}

/* --- struct byte-field setters (offsets 3 and 7): each configures a
 * small fixed-size struct/table entry with two constant byte values.
 * Shape recurs 15 times across the disassembly with different constants
 * -- almost certainly a per-item init table (mode/state id + a
 * secondary parameter byte). --- */

void func_80047B48(unsigned char *a0) {
    a0[3] = 0x4;
    a0[7] = 0x20;
}

void func_80047B5C(unsigned char *a0) {
    a0[3] = 0x7;
    a0[7] = 0x24;
}

void func_80047B70(unsigned char *a0) {
    a0[3] = 0x6;
    a0[7] = 0x30;
}

void func_80047B84(unsigned char *a0) {
    a0[3] = 0x9;
    a0[7] = 0x34;
}

void func_80047BE8(unsigned char *a0) {
    a0[3] = 0x3;
    a0[7] = 0x74;
}

void func_80047BFC(unsigned char *a0) {
    a0[3] = 0x3;
    a0[7] = 0x7c;
}

void func_80047C10(unsigned char *a0) {
    a0[3] = 0x4;
    a0[7] = 0x64;
}

void func_80047C24(unsigned char *a0) {
    a0[3] = 0x2;
    a0[7] = 0x68;
}

void func_80047C38(unsigned char *a0) {
    a0[3] = 0x2;
    a0[7] = 0x70;
}

void func_80047C4C(unsigned char *a0) {
    a0[3] = 0x2;
    a0[7] = 0x78;
}

void func_80047C60(unsigned char *a0) {
    a0[3] = 0x3;
    a0[7] = 0x60;
}

void func_80047C74(unsigned char *a0) {
    a0[3] = 0x3;
    a0[7] = 0x2;
}

void func_80047C88(unsigned char *a0) {
    a0[3] = 0x3;
    a0[7] = 0x40;
}

void func_80047C9C(unsigned char *a0) {
    a0[3] = 0x4;
    a0[7] = 0x50;
}

void func_80047CB0(unsigned char *a0) {
    a0[3] = 0x5;
    a0[7] = 0x48;
}

/* --- misc small helpers --- */

/* Pointer chase: D_80173148 is itself a pointer stored in RAM; return
 * what it points to. */
int func_8002D11C(void) {
    return *D_80173148;
}

/* Getter+setter swap: return the old value of the global, then store
 * the new one. */
int func_80045738(int a0) {
    int v0 = D_80077380;
    D_80077380 = a0;
    return v0;
}

int func_80051BE4(int a0) {
    int v0 = D_80077474;
    D_80077474 = a0;
    return v0;
}

/* Bit-pack two small values into one 16-bit word: a1 in the high bits,
 * the middle nibble of a0 in the low 6 bits. */
unsigned short func_80047920(int a0, int a1) {
    return (a1 << 6) | ((a0 >> 4) & 0x3F);
}

/* Range-checked global setter: stores a0 if it's < 0x20, returns 0 on
 * success / 1 if out of range. */
int func_800554EC(unsigned int a0) {
    if (a0 < 0x20) {
        D_80077578 = a0;
        return 0;
    }
    return 1;
}

/* Read from a global short array (index a0). */
int func_80057638(int a0) {
    return D_8007758C[a0];
}

/* Three independent global stores from three arguments. */
void func_8005495C(int a0, int a1, int a2) {
    D_800776A8 = a0;
    D_800776B0 = a1;
    D_800776B8 = a2;
}

/* Branchy byte-flag toggle: set or clear bit 0 of a0[7] depending on
 * a1. */
void func_80047B20(unsigned char *a0, int a1) {
    unsigned char v0;
    if (a1 != 0) {
        v0 = a0[7] | 1;
    } else {
        v0 = a0[7] & 0xFE;
    }
    a0[7] = v0;
}

/* Add two byte counters (a0[3] and a1's byte-3), clamp to < 0x21: on
 * success writes the new sum back into a0[3] and zeroes *a1, returns 0;
 * on overflow leaves both untouched and returns -1. */
int func_80047D24(unsigned char *a0, unsigned int *a1) {
    unsigned char *a1b = (unsigned char *)a1;
    unsigned char v0 = a0[3];
    unsigned char v1 = a1b[3];
    int sum;
    sum = v0 + v1 + 1;
    if (sum >= 0x21) {
        return -1;
    }
    a0[3] = (unsigned char)sum;
    *a1 = 0;
    return 0;
}
