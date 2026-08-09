/* obj_rro.h -- standalone (host-only, no PSX dependencies) PARTIAL
 * parser/documentation for Ridge Racer 1 (PS1, 1994, Namco)'s OBJ.RRO
 * scenery/object file.
 *
 * Found by reading PS1 function func_80012670 (rr-decomp asm/29E8.s,
 * ~address 0x80012670) instruction-by-instruction, the same technique
 * used for MAP.RRM's func_800125B4 last round. Traced from the same
 * boot load sequence as MAP.RRM: func_80032A54 reads OBJ.RRO into RAM
 * right after MAP.RRM, then calls func_80012670(a0 = buffer) on it.
 *
 * CONFIRMED (from a full register-level trace of func_80012670's ~50
 * instruction loop body -- NOT a guess):
 *   - Header: uint32 LE "object count" N at file offset 0 (this is a
 *     FULL 32-bit word, unlike MAP.RRM's 16-bit section count -- the
 *     real file has N=319). Also stored to a persistent global,
 *     D_80173318, for use elsewhere.
 *   - Immediately after the header (offset 4): N consecutive 16-byte
 *     "object directory" entries. Each entry is:
 *       offset 0-3  (int32): on-disk value observed to be 0 or a small
 *                    placeholder (1, 7, 8, 9, 10, ...) in the real
 *                    file for ~57% of entries -- func_80012670
 *                    OVERWRITES this field for every entry except
 *                    entry 0, replacing it with an absolute pointer
 *                    into the loaded buffer (a running accumulator
 *                    that starts right after the directory and adds
 *                    each entry's computed byte-size in turn -- see
 *                    below). Also stored globally: D_801D82E8 = the
 *                    buffer's directory base pointer (offset+4 of the
 *                    raw buffer), the OBJ.RRO analogue of MAP.RRM's
 *                    D_801D35F0 section table pointer.
 *       offset 4-5  (int16, "field_A"): multiplied by 40 (confirmed:
 *                    shift-by-3-then-add-shift-by-2 = *8+*32... exact
 *                    derivation: (field*5)<<3 = field*40)
 *       offset 6-7  (int16, "field_B"): multiplied by 48
 *       offset 8-9  (int16, "field_C"): multiplied by 32
 *       offset 10-11 (int16, "field_D"): multiplied by 64
 *       offset 12-13 (int16, "field_E"): multiplied by 72
 *       offset 14-15 (int16, "field_F"): multiplied by 56
 *     The six multipliers (32,40,48,56,64,72) are exactly 4*8..9*8 --
 *     a suspiciously clean progression strongly suggesting each field
 *     counts instances of a distinct fixed-size sub-structure (a
 *     classic "N sub-primitives of type K, each K bytes" object model
 *     layout), all sub-structure sizes being multiples of 8 bytes
 *     (consistent with GTE-friendly SVECTOR-based data). This is a
 *     STRONG hypothesis (the multiply-by-constant arithmetic itself is
 *     confirmed instruction-by-instruction; which conceptual "item
 *     type" each field counts is NOT confirmed).
 *   - Per-entry byte size = field_A*40 + field_B*48 + field_C*32 +
 *     field_D*64 + field_E*72 + field_F*56. This is added to a running
 *     pointer/accumulator that starts at (4 + N*16) [right after the
 *     directory] and is written into the FOLLOWING entry's offset-0
 *     field each iteration (i.e. entry i+1's offset-0, once parsed,
 *     holds the absolute start-of-data pointer for entry i+1's data
 *     blob) -- exactly analogous to MAP.RRM's func_800125B4 building a
 *     running per-section byte-offset table, just done in-place in the
 *     OBJ.RRO buffer itself instead of into a separate table.
 *
 * NOT CONFIRMED / KNOWN INCOMPLETE (this is the honest, important
 * caveat -- do NOT treat the format below as fully closed):
 *   - Replaying this exact arithmetic against the real OBJ.RRO
 *     (445348 bytes) gives: header(4) + directory(319*16=5104) +
 *     sum(per-entry sizes) = 5108 + 339320 = 344428 bytes -- SHORT of
 *     the real file size by exactly 100920 bytes (about 22.6% of the
 *     file). Unlike MAP.RRM, this does NOT close exactly. Things
 *     checked and ruled out this round:
 *       - Not a fixed per-object header/footer (100920 / 319 is not
 *         an integer: 316.36).
 *       - The two fields at directory offset 0-3 (before being
 *         overwritten) are sometimes non-zero on disk in ways that
 *         don't look like simple padding (observed values like 1, 7,
 *         8, 9, 10, and a few larger ones like 0x1D0140) -- these are
 *         NOT read by func_80012670 itself (only written), so their
 *         true meaning must come from a different, not-yet-traced
 *         function; they are plausible candidates for "the missing"
 *         bytes but this was not confirmed.
 *       - Hex-inspecting the file right at the computed 344428-byte
 *         boundary shows dense int16-sized data with a very different
 *         statistical character (many small magnitudes, some repeated
 *         sentinel-looking 0x0FFF values) than the directory region --
 *         consistent with a genuine, separate trailing section (a
 *         shared vertex pool or similar) that func_80012670 simply
 *         does not walk, rather than a bug in the size formula above.
 *   - Net assessment: the DIRECTORY STRUCTURE (16-byte entries, 6
 *     scaled sub-counts, running-pointer accumulation) is confirmed
 *     with high confidence from the instruction trace. The exact
 *     CONTENT/layout of each object's data blob, and the ~100KB
 *     trailing block the directory's arithmetic doesn't account for,
 *     are NOT decoded this round -- a real next step, not attempted
 *     yet: trace whichever function actually READS D_801D82E8 (this
 *     round only found the writer) to see how the per-object pointer
 *     and its data blob get consumed/rendered.
 *
 * This header intentionally only exposes the CONFIRMED directory
 * parse (count + per-entry raw fields + computed size), not a made-up
 * "full" object struct, so the parser can't be mistaken for more than
 * it actually is.
 *
 * ============================================================
 * Phase 5 round 3 additions (see objparse_main.c for the runnable
 * verification of both points below, computed from the user's own
 * file at runtime -- no raw bytes committed to this repo):
 *
 * 1. The pre-overwrite "ptr_field" directory column (offset 0-3,
 *    described above as "0 or a small placeholder for ~57% of
 *    entries") turns out to have MORE structure than previously
 *    characterized: of the real file's 319 entries, 137 are exactly 0,
 *    142 are small positive integers (1..1000), and the remaining 40
 *    are ALL (100% of that bucket, exhaustively checked) within a
 *    small tolerance of an exact multiple of 65536 -- e.g. the
 *    previously-noted "large" outlier 0x1D0140 is 29*65536 + 320,
 *    i.e. almost exactly 29.0049 in 16.16 fixed-point. This strongly
 *    suggests this field is NOT simple padding: it's plausibly a
 *    16.16 fixed-point small-integer value (LOD level? a scale
 *    factor? a category id?) for a specific subset of ~12.5% of
 *    objects, coexisting with plain small integers for everything
 *    else. NOT confirmed (no consumer function traced for this field
 *    this round either -- it remains write-only from
 *    func_80012670's perspective, same caveat as before), but a
 *    genuinely new, more precise lead than "placeholder, unclear".
 * 2. CORRECTION to a previous-round claim: last round's notes said the
 *    trailing (unaccounted) region's hex dump showed "0x0FFF sentinel
 *    values" as if that were a signature distinguishing it from the
 *    already-accounted per-object data blobs. Measured exhaustively
 *    this round (every int16 in both regions, not a hex-dump glance):
 *    accounted region 0.634% of int16s == 0x0FFF, trailing region
 *    0.404% -- comparable order of magnitude, NOT a useful
 *    distinguishing signature. The trailing region IS still clearly
 *    structured/non-random data (dense small-magnitude int16s, and a
 *    strong local period-6-byte repetition pattern in the 0x0FFF
 *    occurrence gaps, consistent with runs of 3x int16 vectors -- the
 *    same "vector" shape MAP.RRM's records use), just not distinguishable
 *    from the accounted region by this particular test. Exact record
 *    boundaries/typing of the trailing region are still NOT decoded.
 * ============================================================
 */
#ifndef RR_OBJ_RRO_H
#define RR_OBJ_RRO_H

#include <stddef.h>
#include <stdint.h>

#define OBJ_RRO_DIR_ENTRY_SIZE 16u
#define OBJ_RRO_HEADER_SIZE 4u

/* Confirmed multipliers, in on-disk field order (offsets 4,6,8,10,12,14). */
#define OBJ_RRO_MULT_FIELD_A 40u
#define OBJ_RRO_MULT_FIELD_B 48u
#define OBJ_RRO_MULT_FIELD_C 32u
#define OBJ_RRO_MULT_FIELD_D 64u
#define OBJ_RRO_MULT_FIELD_E 72u
#define OBJ_RRO_MULT_FIELD_F 56u

typedef struct {
    int32_t on_disk_ptr_field;  /* offset 0-3, raw on-disk value (pre-overwrite) */
    int16_t field_a, field_b, field_c, field_d, field_e, field_f; /* offsets 4,6,8,10,12,14 */
    uint32_t computed_size;     /* field_a*40 + field_b*48 + ... (see multipliers above) */
} ObjRroDirEntry;

typedef struct {
    uint32_t object_count;         /* header uint32 @ offset 0 */
    ObjRroDirEntry *entries;       /* [object_count], owned */
    uint32_t data_start_offset;    /* 4 + object_count*16 */
    uint64_t sum_computed_size;    /* sum of entries[i].computed_size */
    uint64_t accounted_bytes;      /* data_start_offset + sum_computed_size */
    /* accounted_bytes vs the real file size is the "still incomplete"
     * gap documented above -- callers should NOT assume they're equal. */
} ObjRroFile;

typedef enum {
    OBJ_RRO_OK = 0,
    OBJ_RRO_ERR_TRUNCATED_HEADER = -1,
    OBJ_RRO_ERR_TRUNCATED_DIRECTORY = -2,
    OBJ_RRO_ERR_ALLOC = -3,
    OBJ_RRO_ERR_ARGS = -4
} ObjRroStatus;

/* Parses only the CONFIRMED header+directory portion of an OBJ.RRO
 * buffer (does not attempt to parse the per-object data blobs -- see
 * file header comment for why that's not decoded yet). */
int obj_rro_parse(const uint8_t *buf, size_t buf_size, ObjRroFile *out);
void obj_rro_free(ObjRroFile *f);

#endif /* RR_OBJ_RRO_H */
