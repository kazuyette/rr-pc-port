/* idx_hed.h -- standalone (host-only, no PSX dependencies) parser for
 * Ridge Racer 1 (PS1, 1994, Namco)'s IDX.HED spatial index file, PLUS
 * the world-space transform this file implies for MAP.RRM sections.
 *
 * Found this round by tracing PS1 function func_80012C14 (rr-decomp
 * asm/29E8.s, ~address 0x80012C14), a 64-iteration "expanding ring"
 * search over nearby grid cells (delta tables D_8005944C/D_8005944D,
 * indexed by direction*256 + ring-step*2 -- a classic spiral/ring
 * neighbor-cell search used by collision/AI code to find the road cell
 * nearest to an arbitrary world position). That function reads the
 * IDX.HED buffer through global D_801D82D0, which is set by
 * func_80015CD4 -- the function this round's task description
 * hypothesized as "the IDX.HED parser". Turns out func_80015CD4 itself
 * does NOT parse IDX.HED's contents at all: it just stores the raw
 * buffer pointer into D_801D82D0 and resets a handful of unrelated
 * state globals (D_801733A0=0xF000 sentinel, D_801D7620=0,
 * D_8007C208=-0x20, D_801D82C8=4) -- read instruction-by-instruction,
 * fully confirmed, genuinely trivial (0x3C bytes / 15 instructions).
 * The REAL "shape" of IDX.HED only became apparent from how
 * func_80012C14 indexes into the buffer it points to.
 *
 * CONFIRMED (byte-exact, verified against the real IDX.HED on a
 * legally owned disc image -- file itself is never committed here):
 *   - IDX.HED is a flat array of 1024 x int16 LE values (2048 bytes
 *     total, matches the file's exact real size with zero slack).
 *   - It is a 32x32 grid, row-major (index = row*32 + col), 1024 =
 *     32*32. Confirmed from func_80012C14's own index arithmetic
 *     (v0 = s1*32 + ... - s0, where s1/s0 are both range-checked
 *     against 0x20 = 32 immediately before use) and from the file
 *     itself: exactly 258 of the 1024 entries are non -1, and treating
 *     the grid as row=index/32, col=index%32 and just plotting which
 *     cells are occupied (independent of anything from MAP.RRM) traces
 *     a visually obvious closed-loop RACETRACK SHAPE.
 *   - Each occupied cell's value is a MAP.RRM section index. Checked
 *     exhaustively against the real files: exactly 258 of 1024 cells
 *     are non -1, the 258 values are the full set {0..257} with no
 *     repeats and no gaps -- i.e. IDX.HED <-> MAP.RRM section is a
 *     PERFECT BIJECTION for this track. Empty cells hold -1 (0xFFFF).
 *   - func_80012C14 derives the *query* cell coordinates by taking a
 *     raw world position (D_801D9068 = world X, D_801D9070 = world Z,
 *     read as 32-bit here even though they're written as 16-bit
 *     elsewhere -- likely a packed/aliased struct field, not fully
 *     traced this round) and dividing by 2048 with round-to-nearest
 *     (the "+0x400 if >=0 else +0xBFF, then arithmetic-shift right by
 *     11" idiom -- standard PSX fixed-point rounding-divide-by-2048).
 *     This gives CELL_SIZE_WORLD_UNITS = 2048 as a CONFIRMED constant
 *     (read directly off the shift amount, not guessed).
 *
 * NOT FULLY CONFIRMED (empirically-tuned this round, see
 * tools/mapparse/README-ish comment in worldmap_main.c for the actual
 * A/B test results that motivated these choices):
 *   - Which grid axis is which, and orientation. Empirically, plotting
 *     MAP.RRM section records translated by (col, row) with col
 *     MIRRORED as (31 - raw_col) produces a visibly coherent partial
 *     road shape (clean quad strips, correct near/far edge pattern)
 *     for roughly the "straight track + one sweeping bend" half of the
 *     course; using raw_col unmirrored, or swapping row/col, both
 *     produce visibly worse (more tangled) results. This function
 *     applies that empirically-best (col, row) -> (31-col, row)
 *     convention and documents it as HYPOTHESIS, not confirmed via
 *     instruction trace (the exact axis convention used by
 *     func_80012C14's OWN callers -- i.e. how world X/Z map to which
 *     of s0/s1 -- was not fully traced this round).
 *   - There is NO per-section rotation applied here. Two different
 *     rotation hypotheses were tried this round (per-section rotation
 *     by the candidate "heading" record field; per-section rotation by
 *     the direction to the next/previous section's grid cell) and
 *     BOTH made the plotted result visibly worse, not better, so
 *     neither is included. This means the transform below is known to
 *     be INCOMPLETE: roughly half of the 258 sections (the tighter,
 *     more curved ones) still overlap into a tangled blob when
 *     plotted, because their true world placement needs a rotation
 *     component this round did not find. See project memory
 *     (rr_pc_port.md, "Phase 5 round 2") for the full list of things
 *     tried and ruled out.
 *
 * Net effect: this file gives a WORKING (if partial/approximate) way
 * to place MAP.RRM section-local record coordinates into a shared
 * world-ish coordinate space, which last round did not have at all.
 *
 * ============================================================
 * Phase 5 round 3 rotation hunt -- RULED OUT (2 more hypotheses) +
 * one genuinely new confirmed structural fact. Full writeup in
 * project memory (rr_pc_port.md, "Phase 5 round 3"); summary here so
 * a future round doesn't retry the same dead ends:
 *
 * 1. IDX.HED cell VALUE bit-packing (does the int16 grid cell hold
 *    more than a plain section index, e.g. high bits as a coarse
 *    orientation octant?) -- RULED OUT, exhaustively. All 258 occupied
 *    cells were checked: every value is EXACTLY the plain section
 *    index 0..257 with zero bits set outside that range (no stray
 *    high bits at all, not even one). There is no room in this field
 *    for packed orientation data.
 * 2. MAP.RRM section-directory count_d "always 0, maybe secretly a
 *    flags/orientation field via unused count_d or high bits of
 *    count_a/b/c" -- RULED OUT, exhaustively (re-checked all 258
 *    entries, not a sample: count_d is 0 in literally all of them,
 *    and count_a/b/c's arithmetic already accounts for the file's
 *    exact byte size with zero slack, so there is no room to reuse
 *    bits from those fields either without breaking the confirmed
 *    file-size accounting). See map_rrm.h for the mirrored note.
 * 3. Pure grid-topology dead-reckoning: chain each section's LOCAL
 *    entry edge (first record's near edge) to the PREVIOUS section's
 *    LOCAL exit edge (last record's far edge) via a rigid 2D
 *    transform (rotation + translation solved exactly from the two
 *    edge endpoints), propagated section-by-section from section 0
 *    around the full 258-section loop -- RULED OUT. Tried both sign
 *    conventions for "which edge points which way"; both produced a
 *    loop-closure gap (distance between the computed final exit point
 *    after section 257 and section 0's own entry point) of 42,000-
 *    88,000 world units -- bigger than the track's entire ~65,536-unit
 *    grid envelope (32 cells x 2048). Per-transition heading deltas
 *    were essentially random (swinging by hundreds of degrees between
 *    consecutive sections), confirming this isn't just accumulated
 *    rounding error but a wrong premise. Cross-checked WITHIN a single
 *    section first (chaining consecutive records inside one section's
 *    own local frame, no transform): that DOES work almost perfectly
 *    (corner-to-corner distances of ~10-30 world units, i.e. clean, on
 *    most consecutive record pairs within section 11 as a spot check)
 *    -- so per-section-internal geometry is self-consistent, but
 *    treating "last record of section i" / "first record of section
 *    i+1" as a reliable ACROSS-section join point is not valid in
 *    general (large multi-record sections, e.g. sections 9-12 with
 *    36-69 records each, are very likely branching/fanned clusters --
 *    forks, guardrail meshes, a start/finish complex -- not simple
 *    single paths, which breaks the entry/exit assumption globally).
 * 4. NEW CONFIRMED FACT (not a rotation decode, but a real structural
 *    finding): traced forward from the render-time consumer of the
 *    MAP.RRM section table (rr-decomp func_80035638, the ~560-
 *    instruction per-frame walk that reads D_801D35F0/D_801D82E8) into
 *    its helper functions func_8004006C and func_80040140 -- both
 *    CONFIRMED (instruction-level) to execute real PS1 GTE hardware
 *    `rtps`/`rtpt` perspective-transform instructions (with `ctc2`
 *    loading the GTE's resident 3x3 rotation-matrix control registers
 *    and `mtc2` loading the input vector data registers). This means
 *    rotation genuinely IS applied via a hardware matrix multiply at
 *    render time, not baked into the MAP.RRM file -- exactly the
 *    "matrix computed dynamically at runtime, never visible in a file
 *    dump" scenario this round was asked to check for. HOWEVER: the
 *    trace stops short of a full decode. Two nearby helper functions,
 *    func_80043738/func_80043794, were also checked (they run right
 *    around the same code path) and turned out to only NEGATE/mirror
 *    the CURRENTLY-loaded matrix's X/Z-related components (a sign-flip
 *    utility -- plausible use: L/R mirroring or a reflection pass),
 *    not build a fresh per-section matrix from a stored angle. The
 *    actual call site that constructs a NEW rotation matrix from some
 *    section-specific angle (which would need to read/derive a heading
 *    value and do a sin/cos-table lookup to fill the GTE control
 *    registers) was NOT located this round -- concrete next-round lead:
 *    find every `ctc2` write to GTE control regs 0-4 (not just the
 *    negate-in-place pattern already found) and trace its rotation
 *    values backward to their source.
 * ============================================================
 */

#ifndef RR_PC_PORT_IDX_HED_H
#define RR_PC_PORT_IDX_HED_H

#include <stdint.h>
#include <stddef.h>

#define IDX_HED_GRID_DIM 32                 /* confirmed: 32x32 */
#define IDX_HED_GRID_CELLS (IDX_HED_GRID_DIM * IDX_HED_GRID_DIM) /* 1024 */
#define IDX_HED_EXPECTED_FILE_SIZE (IDX_HED_GRID_CELLS * 2)      /* 2048 */
#define IDX_HED_EMPTY_CELL (-1)

/* Confirmed constant: world units per grid cell (read off the >>11
 * shift amount in func_80012C14). */
#define IDX_HED_CELL_SIZE_WORLD_UNITS 2048

typedef enum {
    IDX_HED_OK = 0,
    IDX_HED_ERR_BAD_SIZE = -1,
    IDX_HED_ERR_NULL_ARG = -2,
    IDX_HED_ERR_SECTION_OUT_OF_RANGE = -3,
    IDX_HED_ERR_SECTION_NOT_FOUND = -4
} IdxHedStatus;

typedef struct {
    /* grid[row][col], row = 0..31 (world Z cell), col = 0..31 (world X
     * cell, RAW orientation as stored on disk -- see idx_hed_section_cell()
     * for the empirically-best mirrored convention used for plotting). */
    int16_t grid[IDX_HED_GRID_DIM][IDX_HED_GRID_DIM];
    int section_to_cell_valid; /* 1 if section_to_cell[] below is populated */
    /* Inverse lookup: MAP.RRM section index (0..N-1) -> (col,row).
     * Only valid entries (section actually present in the grid) are
     * filled; others left at (-1,-1). Sized generously (511) since
     * MAP.RRM's section_count is a uint16 in principle, even though the
     * shipped track uses 258. */
    int section_to_col[512];
    int section_to_row[512];
    int max_section_seen;
} IdxHedFile;

/* Parses a raw in-memory IDX.HED buffer. buf_size must be exactly
 * IDX_HED_EXPECTED_FILE_SIZE (2048) -- any other size is rejected as
 * IDX_HED_ERR_BAD_SIZE, since (unlike MAP.RRM/OBJ.RRO) there is no
 * header/count to sanity-check against, just the fixed grid. */
int idx_hed_parse(const uint8_t *buf, size_t buf_size, IdxHedFile *out);

/* Look up the (raw, unmirrored) grid cell for a MAP.RRM section index.
 * Returns IDX_HED_OK and fills out-col / out-row, or
 * IDX_HED_ERR_SECTION_NOT_FOUND if that section never appears in the grid
 * (shouldn't happen for a
 * well-formed track pair, but the real file was found to be a perfect
 * bijection so this is mostly a defensive check). */
int idx_hed_section_cell_raw(const IdxHedFile *f, int section, int *col, int *row);

/* Returns the empirically-best-guess WORLD ORIGIN (in MAP.RRM record
 * coordinate units) for a section's local record coordinates, i.e. the
 * (world_x, world_z) to ADD to every local x/z in that section's
 * records. Applies the mirrored-column convention documented above
 * (HYPOTHESIS, not instruction-trace-confirmed) and the confirmed 2048
 * cell size. No rotation is applied (see file header comment -- known
 * incomplete). Returns IDX_HED_OK / IDX_HED_ERR_SECTION_NOT_FOUND. */
int idx_hed_section_world_origin(const IdxHedFile *f, int section,
                                  int32_t *world_x, int32_t *world_z);

#endif /* RR_PC_PORT_IDX_HED_H */
