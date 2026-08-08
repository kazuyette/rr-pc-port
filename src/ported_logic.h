/* Prototypes for src/ported_logic.c -- the Phase 2 round 1 batch of
 * functions re-derived as genuine C from rr-decomp's asm-locked pool
 * (previously __asm__ MIPS transcriptions, now proven byte-identical
 * when compiled with the project's GCC 2.7.2 PSX cross-compiler; see
 * rr-decomp's src/more_asm*.c for the verified originals and this
 * repo's project memory for how each was derived). Original decomp
 * function names kept as-is for traceability back to the PS1 binary's
 * symbol table. */
#ifndef RR_PORTED_LOGIC_H
#define RR_PORTED_LOGIC_H

#include "psx_compat/psx_types.h"

void func_800129A4(void);
void func_80018A94(void);
void func_8002E3F8(void);
void func_8003DE88(void);
void func_8004B528(void);

int func_80055800(void);
int func_80055808(void);

void func_8004A4CC(void);
int  func_80059040(void);
int  func_80045718(void);
int  func_80045728(void);
void func_8004984C(void);
void func_8004985C(s16 a0);
s16  func_8004D460(void);
void func_800534B8(s32 a0);

void func_80047B48(u8 *a0);
void func_80047B5C(u8 *a0);
void func_80047B70(u8 *a0);
void func_80047B84(u8 *a0);
void func_80047BE8(u8 *a0);
void func_80047BFC(u8 *a0);
void func_80047C10(u8 *a0);
void func_80047C24(u8 *a0);
void func_80047C38(u8 *a0);
void func_80047C4C(u8 *a0);
void func_80047C60(u8 *a0);
void func_80047C74(u8 *a0);
void func_80047C88(u8 *a0);
void func_80047C9C(u8 *a0);
void func_80047CB0(u8 *a0);

s32 func_8002D11C(void);
s32 func_80045738(s32 a0);
s32 func_80051BE4(s32 a0);
u16 func_80047920(s32 a0, s32 a1);
s32 func_800554EC(u32 a0);
s32 func_80057638(s32 a0);
void func_8005495C(s32 a0, s32 a1, s32 a2);
void func_80047B20(u8 *a0, s32 a1);
s32 func_80047D24(u8 *a0, u32 *a1);

#endif /* RR_PORTED_LOGIC_H */
