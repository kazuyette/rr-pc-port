/* Ported from rr-decomp src/stubs.c: small, already-understood functions
 * that don't yet have a better home. */

/* Empty callback / no-op hook. In the original PS1 binary this immediately
 * precedes _start; exact purpose not yet identified, but the body (jr $ra;
 * nop) is trivial and matches byte-for-byte in the decomp, so it's kept
    * here as a genuine (if trivial) ported function rather than raw asm. */
void func_8003FA94(void) {
}
