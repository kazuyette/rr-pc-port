/* map_rrm.h -- standalone (host-only, no PSX dependencies) parser for
 * Ridge Racer 1 (PS1, 1994, Namco)'s MAP.RRM track-data file.
 *
 * This header/implementation are the result of reverse-engineering
 * MAP.RRM by tracing the PS1 boot/load code in rr-decomp
 * (https://github.com/kazuyette/rr-pc-port -- see the sibling
 * mapparse_main.c and the project notes for the full writeup of how
 * this was found). Summary of what is CONFIRMED vs GUESSED:
 *
 * CONFIRMED (byte-exact, verified against the real MAP.RRM on a legally
 * owned disc image -- file itself is never committed to this repo):
 *   - Header: uint16 LE "section count" N at file offset 0 (2 bytes
 *     unused/padding follow, offset 2-3).
 *   - Immediately after the header (offset 4): N consecutive 8-byte
 *     "section directory" entries, each 4 x uint16 LE
 *     {count_a, count_b, count_c, count_d}.
 *   - Immediately after the directory (offset 4 + N*8): a single flat
 *     "bulk data" region made of fixed 40-byte records. For each of the
 *     N sections in order, the bulk data holds count_a records of "type
 *     A", then count_b records of "type B", then count_c records of
 *     "type C" -- i.e. one linear stream, chunked per section per type,
 *     not three separate global arrays. count_d was observed to be 0 in
 *     every one of the 258 entries in the shipped file, so its role
 *     (a 4th record type that's simply unused by this particular track,
 *     vs. something else entirely) is NOT confirmed.
 *   - This layout was verified by replaying the exact accumulation
 *     arithmetic performed by PS1 function func_800125B4 (rr-decomp
 *     asm/29E8.s) against the real file: header(4) + directory(N*8) +
 *     sum(count_a+count_b+count_c across all N)*40 == the file's exact
 *     byte size (271548 bytes for the one course RR1 ships), with zero
 *     slack.
 *   - This data is read into RAM during boot by a load sequence found
 *     by tracing forward from the "\MAP.RRM;1" rodata string
 *     (D_800106F0) -> a 10-entry filename pointer table
 *     (D_800747BC..D_800747E0, MAP.RRM is entry 0 of that table) ->
 *     func_80032A54 (the "load all 8 CD-directory-resident data files"
 *     routine, called at boot) -> func_80032948 (CD-read-a-file-into-
 *     buffer-by-directory-entry) -> func_800125B4 (the actual MAP.RRM
 *     header/directory parser, called on the raw CD-read buffer; it
 *     builds an expanded 32-byte-per-section runtime table at a fixed
 *     RAM address, D_801D35F0, containing 3 running byte offsets + the
 *     3 raw counts per section -- i.e. exactly the cumulative-offset
 *     table this parser reconstructs below).
 *
 * NOT CONFIRMED (best-effort hypothesis from statistical/structural
 * analysis of the raw bytes only -- flagged individually below; treat
 * all of these as "plausible, not proven"):
 *   - The per-record 40-byte internal field layout. The first 24 bytes
 *     decode very cleanly as four int16[3] "vectors" (v0..v3) in a
 *     value range (roughly +/-32000, typically a few thousand) totally
 *     unlike the huge/noisy values you get interpreting the same bytes
 *     as int32 -- this is a strong signal the true field width is
 *     16-bit, not 32-bit. For a large fraction of type-B records
 *     (the most common record type, ~5420 of the file's 6737 total
 *     records), v0[1] == v1[1] and v2[1] == v3[1] (i.e. two of the four
 *     vectors always share their middle component), which is the
 *     signature you'd expect from "near-left/near-right" and
 *     "far-left/far-right" corners of a road-surface quad sharing a
 *     common height at each cross-section. This pairing does NOT hold
 *     for every record (breaks at what look like junctions/branches),
 *     and does NOT hold at all for the (much rarer) type-A records
 *     sampled, which still look like plausible local-space quads but
 *     without matched heights (candidate: banked wall/collision quads,
 *     unconfirmed).
 *   - The remaining 16 bytes (offsets 24-39) are individually much less
 *     understood; see the field comments on MapRrmRecord below for the
 *     specific (weak) hypotheses that came out of statistical analysis
 *     (a slowly-varying candidate heading angle, a stepped candidate
 *     group/material id, a near-constant candidate flags word). None of
 *     these were cross-checked against a consumer function the way the
 *     file-level layout was, so treat them as leads for a future round,
 *     not established fact.
 *   - No per-section coordinate TRANSFORM (translation/rotation) was
 *     found or decoded -- plotting the raw v0 corners top-down does NOT
 *     produce a recognisable single closed track outline, because
 *     (most likely) each section's vectors are in a local/section-
 *     relative frame that needs an external transform to place in
 *     world space. That transform, if the game applies one at all
 *     (it's also possible the values genuinely are already
 *     world-space and the track is simply laid out compactly / this
 *     analysis mis-identified something), was not located this round.
 *
 * This parser only reconstructs the CONFIRMED structural layout (header
 * + directory + record boundaries) precisely; the MapRrmRecord field
 * breakdown is provided as the best available decode of the 40 bytes,
 * clearly marked where uncertain, so a future round has a concrete
 * starting struct to refine rather than starting from raw hex again.
 *
 * Phase 5 round 3: re-verified count_d == 0 across ALL 258 directory
 * entries (exhaustive, not a sample) -- still no exception found, and
 * since count_a/count_b/count_c's arithmetic already accounts for the
 * file's exact byte size with zero slack, there is no room left in
 * this 8-byte directory entry to hide a rotation/orientation field
 * without breaking that confirmed accounting. See idx_hed.h for the
 * fuller rotation-hunt writeup (multiple new hypotheses tried and
 * ruled out this round, plus one genuinely new confirmed fact: the PS1
 * renderer DOES apply a real GTE hardware rotation matrix at render
 * time via `rtps`/`rtpt`, traced instruction-by-instruction -- so
 * rotation is real and applied dynamically, just not decoded from the
 * file alone yet).
 */
#ifndef RR_MAP_RRM_H
#define RR_MAP_RRM_H

#include <stddef.h>
#include <stdint.h>

#define MAP_RRM_RECORD_SIZE 40u
#define MAP_RRM_DIR_ENTRY_SIZE 8u
#define MAP_RRM_HEADER_SIZE 4u

/* One 8-byte "section directory" entry, verbatim from the file (all
 * fields confirmed by replaying the loader's arithmetic against the
 * real file -- see header comment). Counts are in RECORDS, not bytes. */
typedef struct {
    uint16_t count_a;
    uint16_t count_b;
    uint16_t count_c;
    uint16_t count_d; /* always 0 in the one shipped RR1 course; purpose unconfirmed */
} MapRrmSectionDir;

/* One 40-byte bulk-data record. See the big header comment above for
 * which fields are confirmed vs. guessed. */
/* ROUND 45 UPDATE -- THE 16 "UNKNOWN" BYTES ARE DECODED (CONFIRMED):
 * traced the PS1 track renderer func_800163E4 -> its quad emitter
 * func_8003486C, which copies bytes 24-39 of each 40-byte entry
 * VERBATIM into POLY_FT4 GPU packets:
 *   bytes 24-25 (unk_18)  = u0,v0 texture coords (one byte each)
 *   bytes 26-27 (heading) = CLUT id  (NOT a heading -- the round-44
 *                            "slowly varying" observation was palette
 *                            changes along track zones)
 *   bytes 28-29 (unk_1c)  = u1,v1
 *   bytes 30-31 (unk_1e)  = TPAGE id (the "small slowly-varying int")
 *   bytes 32-33 (unk_20)  = u2,v2
 *   bytes 34-35 (group_id)= ordering-table depth bias (the "stepped
 *                            material id" -- it IS material-correlated,
 *                            via sort layering: bridges above roads)
 *   bytes 36-37 (unk_24)  = u3,v3
 *   bytes 38-39 (flags)   = still open (candidate prim-type marker)
 * Validated against the real MAP.RRM: every tpage decodes to a valid
 * 4bpp VRAM page where the TEX banks load, every CLUT id to the
 * y=480..509 palette rows, and the UV pairs form clean axis-aligned
 * rects. See tools/texparse/psx_vram.{h,c} for the VRAM recreation
 * that renders them. Field NAMES below are kept for source stability;
 * read them via this mapping. */
typedef struct {
    int16_t v0[3]; /* bytes 0-5:   corner/vector 0 */
    int16_t v1[3]; /* bytes 6-11:  corner/vector 1 (often shares v0[1], i.e. height, for type-B road-surface records) */
    int16_t v2[3]; /* bytes 12-17: corner/vector 2 */
    int16_t v3[3]; /* bytes 18-23: corner/vector 3 (often shares v2[1] the same way v0/v1 do) */
    int16_t unk_18;    /* bytes 24-25: unconfirmed, high record-to-record variance */
    uint16_t heading;  /* bytes 26-27: unconfirmed hypothesis -- BAMS-style angle
                           (0..65535 == 0..360 deg); observed to vary slowly and
                           near-monotonically across consecutive records within a
                           run, consistent with a road heading that changes
                           smoothly along a curve */
    int16_t unk_1c;    /* bytes 28-29: unconfirmed, high variance */
    int16_t unk_1e;    /* bytes 30-31: unconfirmed hypothesis -- small integer,
                           slowly varying within a run (candidate: bank angle or
                           a coarse curvature category) */
    int16_t unk_20;    /* bytes 32-33: unconfirmed, high variance */
    uint16_t group_id; /* bytes 34-35: unconfirmed hypothesis -- takes a small
                           number of distinct values that stay constant across
                           runs of several consecutive records before stepping
                           to a new value (candidate: sub-section/material/
                           curve-segment id) */
    int16_t unk_24;    /* bytes 36-37: unconfirmed, high variance */
    uint16_t flags;    /* bytes 38-39: unconfirmed hypothesis -- near-constant
                           within a run (0x0202 typical for interior records in
                           samples inspected), sometimes a different constant
                           (0x0303 observed) specifically at the first record of
                           a run -- candidate record-type/continuation marker */
} MapRrmRecord;

typedef enum {
    MAP_RRM_RECORD_TYPE_A = 0,
    MAP_RRM_RECORD_TYPE_B = 1,
    MAP_RRM_RECORD_TYPE_C = 2,
} MapRrmRecordType;

/* A flattened, tagged record: which section it came from, which of the
 * per-section A/B/C runs, and its index within that run. Records are
 * stored in the same order they appear in the file (section 0's A run,
 * then section 0's B run, then section 0's C run, then section 1's A
 * run, ...). */
typedef struct {
    uint16_t section_index;
    MapRrmRecordType type;
    uint16_t index_in_run;
    uint32_t file_offset; /* absolute byte offset of this record in the source buffer */
    MapRrmRecord rec;
} MapRrmTaggedRecord;

typedef struct {
    uint16_t section_count;      /* header count N */
    MapRrmSectionDir *sections;  /* [section_count], owned */
    MapRrmTaggedRecord *records; /* flattened bulk-data records, in file order, owned */
    size_t record_count;         /* sum(count_a+count_b+count_c) across all sections */
    size_t bytes_consumed;       /* header + directory + sum(records)*40 */
} MapRrmFile;

/* Error codes returned by map_rrm_parse. */
#define MAP_RRM_OK 0
#define MAP_RRM_ERR_TRUNCATED_HEADER (-1)
#define MAP_RRM_ERR_TRUNCATED_DIRECTORY (-2)
#define MAP_RRM_ERR_TRUNCATED_DATA (-3)
#define MAP_RRM_ERR_ALLOC (-4)
#define MAP_RRM_ERR_ARGS (-5)

/* Parses a MAP.RRM buffer already loaded into memory (e.g. via
 * fread()). Does not require buf_size to exactly equal the amount of
 * data the header/directory declare -- only that everything declared
 * fits inside buf_size (a well-formed file, like RR1's real MAP.RRM,
 * will consume it exactly; mismatch is reported via
 * out->bytes_consumed vs. buf_size, left for the caller to compare and
 * warn about, since a strict equality check inside the parser would
 * make it less useful for exploring malformed/truncated inputs).
 *
 * On success (MAP_RRM_OK), *out is fully populated and owns allocated
 * memory that must be released with map_rrm_free(). On failure, *out
 * is left zeroed (safe to pass to map_rrm_free() regardless). */
int map_rrm_parse(const uint8_t *buf, size_t buf_size, MapRrmFile *out);

/* Frees memory owned by *f and zeroes it. Safe to call multiple times
 * or on an all-zero MapRrmFile. */
void map_rrm_free(MapRrmFile *f);

#endif /* RR_MAP_RRM_H */
