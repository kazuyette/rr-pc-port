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
