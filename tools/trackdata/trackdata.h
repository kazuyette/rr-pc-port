/* trackdata.h -- standalone (host-only, no PSX dependencies) parser for
 * Ridge Racer 1 (PS1, 1994, Namco)'s two built-in "section-chain" course
 * geometry tables.
 *
 * Unlike MAP.RRM/OBJ.RRO/TEX*.TMS (separate files on the game CD, parsed
 * by the sibling tools/mapparse and tools/texparse), this data is NOT a
 * separate asset file -- it is baked directly into PSX.EXE's static data
 * segment as two fixed-address tables, read in-place by the game's live
 * race-physics code (never loaded from the CD at runtime). So this
 * parser's input is the user's own legally-owned PSX.EXE (a raw PS-EXE
 * binary with the standard 2048-byte PS-X EXE header) -- never committed
 * to this repo, exactly like every other real game asset used by this
 * project's tools.
 *
 * Reverse-engineered entirely by tracing rr-decomp (100%-matched
 * decompilation, https://github.com/kazuyette/rr-decomp), NOT via
 * Ghidra and NOT via a live emulator session -- every fact below comes
 * from reading the committed, ground-truth disassembly text directly
 * (asm/29E8.s, asm/data/49A30.data.s in rr-decomp). See that project's
 * `rr_pc_port_physics_round1.md` session notes (rounds 3-8) for the full
 * derivation; short version below.
 *
 * CONFIRMED (derived from exact instruction-level traces of two PS1
 * functions -- func_80017B58, the "project a world point into a
 * section's local frame" routine, and func_80017DF4, the "find which
 * section currently brackets the car + blend its steering/heading
 * reference" routine that calls it -- not guessed from statistics):
 *
 *   Each table is a flat array of fixed 20-byte ("0x14") records, one
 *   per track section, in course-driving order (the record chain is a
 *   closed loop -- the last record sits adjacent to the first). Two
 *   tables exist, for the two course-length variants the game supports
 *   (selected by `FUN_80014c2c`'s case-2 finish-line logic based on
 *   `DAT_8007c210 < 3`):
 *     - "course A": RAM address 0x8005A44C, 256 records (5120 bytes)
 *     - "course B": RAM address 0x8005CC4C, 368 records (7360 bytes)
 *
 *   Record layout (byte offsets within each 20-byte record):
 *     0x00  int32   section center X, 14-bit fixed point (real = raw/16384.0)
 *     0x04  int32   section center Z, 14-bit fixed point (real = raw/16384.0)
 *     0x08  int16   auxiliary per-section scalar (round 10 finding, via
 *                   func_80017DF4): read for both the bracketing
 *                   section and its successor, blended by the along-
 *                   track progress fraction (same 0..255 weight as
 *                   `along` below). Round 13 correction: round 10's
 *                   claim that the blended result is written into the
 *                   "live car struct" was wrong -- the pointer it's
 *                   written through (func_80017DF4's own a0 param) is
 *                   actually a small STACK-LOCAL scratch buffer that
 *                   FUN_8001C490 builds fresh before the call, not a
 *                   persistent car field, so nothing carries across
 *                   frames here. Round 13 traced its actual consumption
 *                   in FUN_8001C490 immediately after the call: it's
 *                   read once (as `blended - 8`), but ONLY when a
 *                   different per-car flag (checked via a short field
 *                   this round couldn't map to a named struct) is
 *                   nonzero, and is then compared against a locally
 *                   computed threshold; taking that branch zeroes two
 *                   globals, plays what looks like a sound/event
 *                   trigger, and calls two more unidentified functions.
 *                   That whole shape -- a conditionally-checked
 *                   threshold gating a one-shot event with a sound cue
 *                   -- reads much more like CHECKPOINT/LAP-VALIDATION
 *                   logic (e.g. "has the car reached the section that
 *                   counts as crossing checkpoint N") than a physics
 *                   term, revising round 10's "curvature/banking hint"
 *                   guess -- still NOT CONFIRMED (the gating flag and
 *                   the two called functions weren't identified), but
 *                   now backed by a full consumption trace rather than
 *                   just "it's read somewhere." Exposed here as
 *                   `aux_a_raw`, unscaled.
 *     0x0A  int16   heading, BAM angle units (4096 = one full turn;
 *                   confirmed both by func_80017B58 feeding it to the
 *                   PS1 sin/cos BAM12 lookup functions as `heading -
 *                   0xC00`, and empirically: decoding the real tables,
 *                   heading wraps cleanly from ~4046 to 0 between two
 *                   consecutive closed-loop records)
 *     0x0C  int16   a SECOND BAM-angle-like field (round 10 finding):
 *                   func_80017DF4 reads this for both bracketing
 *                   sections and blends it through the exact same
 *                   circular-interpolation routine (func_800177B8) used
 *                   for the main heading at +0x0A, then feeds the
 *                   blended angle into a sin lookup to compute part of
 *                   the function's output vector. Round 13: traced that
 *                   whole output computation byte-exact (ported as
 *                   src/physics.c's physics_steering_reference_raw) --
 *                   the blended aux_heading value is used BOTH as the
 *                   multiplier magnitude AND folded into the sin's
 *                   angle argument (output = magnitude * sin(2048 -
 *                   currentHeading - magnitude)), which reads much more
 *                   like a steering/target-heading GUIDANCE term (a
 *                   classic sin-nonlinearity P-controller shape, common
 *                   for smooth cornering correction that saturates
 *                   rather than overshoots at large errors) than a pure
 *                   banking/camber angle -- revising round 10's guess.
 *                   Still NOT CONFIRMED: how FUN_8001c490 actually uses
 *                   the resulting value wasn't traced. Exposed here as
 *                   `aux_heading_raw`, same BAM12 units as `heading_raw`.
 *     0x0E  int16   RIGHT-side half-width, 5-bit fixed point (real =
 *                   raw/32.0) -- used when the tested point's lateral
 *                   offset from the centerline is > 0
 *     0x10  int16   LEFT-side half-width, same 5-bit fixed point --
 *                   used when lateral offset is <= 0. Track width is
 *                   genuinely ASYMMETRIC per section (independently
 *                   observed in the real data: e.g. course A records
 *                   6-11 show the right width flat while the left width
 *                   climbs steadily, consistent with a one-sided track
 *                   widening -- a pit lane merge or a widening corner,
 *                   not decode noise).
 *     0x12  --      still unused by both func_80017B58 and
 *                   func_80017DF4 -- role not determined. May be read by
 *                   func_800178A0 (the section edge/kerb-point
 *                   function, also called by func_80017DF4 but not yet
 *                   traced instruction-by-instruction) or simply
 *                   unused padding to round the record to 20 bytes.
 *
 *   The "along-track projection" formula (used for lap/section-position
 *   tracking) and the "off-track" test, both confirmed the same way:
 *   given world point (x,z) and a candidate section, with dx/dz the
 *   point's offset from the section center and S/C = sin/cos(heading -
 *   0xC00):
 *     along   = clamp( ((S*dx + C*dz) >> 4) / 0x210, 0, 255 )   -- 8-bit
 *               progress fraction across the section (0x210 = 528 raw
 *               units = one full section traversed)
 *     lateral = round( (-dx*C + S*dz) >> 12 ) >> 1               -- signed
 *               perpendicular distance from the centerline
 *     off-track = abs(lateral) > (widthR if lateral > 0 else widthL)
 *   See trackdata_project_point() below for a host-native (double
 *   precision, not fixed-point) reimplementation of this same formula,
 *   and src/physics.h in this repo for where it's used by the port's
 *   physics code.
 *
 *   func_80017DF4 itself (round 10/11/12 finding; the section-finding
 *   HALF is ported -- see src/physics.c's physics_find_section_local_walk
 *   and ROADMAP.md Phase 7): locates the pair of adjacent sections
 *   (current, next) that brackets the car by a LINEAR search (walking
 *   the section index backward one step at a time via func_80017838's
 *   edge-crossing test until the test stabilizes, then forward the same
 *   way) -- not a binary search, and not the brute-force nearest-center
 *   search `physics_find_nearest_section` uses as a host-native fallback
 *   (see src/physics.h). Once bracketed, it blends both the main heading
 *   (+0x0A) and the secondary angle (+0x0C) between the two sections
 *   via func_800177B8, and the auxiliary scalar (+0x08) linearly by the
 *   same progress weight, writing results back into a stack-local
 *   scratch buffer (round 13 correction -- NOT the persistent car
 *   struct, see the +0x08 field entry above) and a caller-supplied
 *   output struct -- i.e. this is the game's real steering-reference /
 *   target-heading computation for the current frame, not merely an
 *   off-track check. Round 13: traced the output computation itself
 *   byte-exact and ported it as src/physics.c's
 *   physics_steering_reference_raw -- see the +0x0C field entry above
 *   and that function's doc comment for the formula and what's still
 *   not confirmed (mainly: how FUN_8001c490 consumes the result).
 *
 *   Round 12 finding -- the exact edge-crossing test func_80017DF4 calls
 *   (func_80017838), fully traced and CONFIRMED byte-exact: given two
 *   points A, B (each {x at +0x0, z at +0x8}) and a test point (px, pz),
 *   it computes
 *     side = (A.z - B.z)*px - (A.x - B.x)*pz + (A.x*B.z - B.x*A.z)
 *   and returns `side > 0`. Algebraically this is exactly the textbook
 *   2D cross-product "which side of the directed line A->B is point P
 *   on" test (cross(B-A, P-A) > 0). The caller, func_80017DF4, builds A
 *   and B per candidate section by calling func_800178A0 (below) to get
 *   that section's RIGHT-edge and LEFT-edge points in CAR-RELATIVE
 *   coordinates (car implicitly sits at the local origin), so passing
 *   (px,pz)=(0,0) in some call sites is literally "is the car on this
 *   side of the section's right-to-left gate line" -- the other call
 *   sites test the midpoint between two adjacent sections' right-edge
 *   points against the candidate section's own gate line instead of the
 *   raw origin, which reads as a stabilizing/anti-jitter measure. This
 *   confirms func_80017DF4 tests against each section's actual
 *   right-edge-to-left-edge "gate" line, not merely a perpendicular
 *   bisector between section centers -- `physics_find_section_local_walk`
 *   in src/physics.c deliberately still uses a simpler perpendicular-
 *   bisector-of-centers boundary test rather than porting this exactly,
 *   both because building real edge points requires func_800178A0's
 *   still-partially-unresolved formula (see below) and because, for the
 *   walk's actual purpose (finding *some* nearby correct section index),
 *   the two tests agree in every case that matters for reasonably-shaped
 *   track geometry -- see the comment on physics_find_section_local_walk
 *   in src/physics.h for the up to date reasoning.
 *
 *   Round 12 finding -- func_800178A0 (the "section edge/kerb-point"
 *   function), traced but only PARTIALLY confirmed. Structurally
 *   confirmed: given a car pointer and a section pointer, it computes
 *   TWO points -- the section's right-edge point (using width_right,
 *   offset 0x0E) and left-edge point (using width_left, offset 0x10) --
 *   each offset from the section center by (width * trig(heading_raw -
 *   0x800)) rotated 90 degrees from the along-track heading used by
 *   func_80017B58 (which instead uses `heading_raw - 0xC00`; 0xC00 -
 *   0x800 = 0x400 = 1024 BAM units = exactly 90 degrees, consistent with
 *   "along" needing the heading itself and "edge" needing the
 *   perpendicular-to-heading direction), then subtracts the car's own
 *   position -- making the result car-relative (car sits at local
 *   origin). The Z channel matches this exactly: edge.z = section_z_int
 *   - car.z. The X channel does NOT: edge.x = D_801733A0 - section_x_int
 *   - car.x, i.e. it includes an extra additive constant with no Z-axis
 *   counterpart. D_801733A0 is CONFIRMED to be a plain compile-time
 *   constant, 0xF000 (61440) -- it has exactly one write site in the
 *   whole codebase (func_80015CD4, an unrelated course-init routine) and
 *   is never reassigned elsewhere -- but *why* only the X channel gets
 *   this bias, or what real-world quantity 61440 represents, is NOT
 *   CONFIRMED. Best guess (not verified): some kind of world-space
 *   re-basing/wraparound scheme applied asymmetrically to X for a
 *   subsystem unrelated to this specific edge-point use (e.g. shared
 *   with a background-scroll or camera-sector system), coincidentally
 *   harmless here if its net effect cancels out elsewhere in the
 *   pipeline -- but that's speculation, not an RE finding. This
 *   asymmetry is why this port does not attempt a byte-exact
 *   reimplementation of func_800178A0 (see src/physics.c's TODO).
 *
 *   Round 12 finding -- the PS1 BAM12 trig lookup's fixed-point scale is
 *   now CONFIRMED: func_80044E2C (sin-like) and func_80044D0C (cos-like)
 *   both index into signed 16-bit lookup tables (e.g. D_80075FA0) with
 *   1024 entries covering one quarter-turn (angle 0..1023 of the 4096
 *   BAM12 units/turn), using standard quadrant-mirroring. Reading the
 *   actual table bytes: entry 1 (angle = 1/4096 turn) is 0x0006 = 6,
 *   and sin(2*pi*1/4096)*4096 = 6.28 -- confirming Q12 fixed point, i.e.
 *   raw/4096.0 = the real sin/cos value. This resolves the "confirm the
 *   BAM trig fixed-point scale" item that was open through round 11 --
 *   trackdata_project_point's double-precision sin()/cos() calls were
 *   already numerically equivalent to this scale (a real sin/cos call is
 *   the infinite-precision version of the same Q12 table), so no code
 *   change was needed here, just confirmation.
 *
 * NOT CONFIRMED / not yet decoded:
 *   - The exact real-world meaning/scale of `aux_a_raw` (offset 0x08)
 *     and `aux_heading_raw` (offset 0x0C) -- their presence and the
 *     fact they're actively read/blended/written every frame is
 *     confirmed; what gameplay effect they produce is not (see above).
 *   - The still-unused byte pair at offset 0x12.
 *   - D_801733A0's real-world purpose (see round 12 finding above) --
 *     its VALUE (61440) and sole write site are confirmed, its ROLE in
 *     func_800178A0's asymmetric X-channel formula is not.
 *   - func_800178A0's exact edge-point formula is therefore only
 *     partially confirmed (see round 12 finding above) -- not yet
 *     ported here.
 *   - No world-space *rendering* geometry (road surface polygons,
 *     scenery) comes from this table -- that's MAP.RRM/OBJ.RRO's job
 *     (see tools/mapparse). This table is used by the game purely for
 *     PHYSICS/gameplay: lap position tracking, off-track detection, and
 *     smoothing the car's target heading across section boundaries. It
 *     is a separate, coarser "driving-line" representation of the
 *     course, not the visual mesh.
 */
#ifndef RR_TRACKDATA_H
#define RR_TRACKDATA_H

#include <stddef.h>
#include <stdint.h>

/* RAM addresses and record counts of the two course-variant tables, as
 * found in the original PSX.EXE (see file header comment). Used to
 * locate the tables inside a loaded EXE image via ram_to_file_offset(). */
#define TRACKDATA_COURSE_A_RAM_ADDR 0x8005A44Cu
#define TRACKDATA_COURSE_A_COUNT    256
#define TRACKDATA_COURSE_B_RAM_ADDR 0x8005CC4Cu
#define TRACKDATA_COURSE_B_COUNT    368

#define TRACKDATA_RECORD_SIZE 20 /* 0x14 bytes/record, see header comment */

typedef struct {
    double x, z;           /* section center, world units (already /16384.0) */
    int16_t aux_a_raw;     /* offset 0x08, unscaled -- see file header, role NOT confirmed */
    int16_t heading_raw;   /* BAM units, 0..4095 = one turn (may appear negative if read signed near the wrap point) */
    int16_t aux_heading_raw; /* offset 0x0C, BAM units like heading_raw -- see file header, role NOT confirmed (candidate: banking/camber) */
    double width_right;    /* world units (already /32.0), used when lateral > 0 */
    double width_left;     /* world units (already /32.0), used when lateral <= 0 */
} TrackSection;

typedef struct {
    TrackSection *sections; /* [count], owned */
    size_t count;
} TrackData;

#define TRACKDATA_OK 0
#define TRACKDATA_ERR_ARGS (-1)
#define TRACKDATA_ERR_NOT_PSEXE (-2)     /* missing/invalid "PS-X EXE" magic */
#define TRACKDATA_ERR_OUT_OF_RANGE (-3)  /* requested RAM address falls outside the EXE's loaded .text range */
#define TRACKDATA_ERR_ALLOC (-4)

/* Standard PS-X EXE header fields needed for RAM<->file-offset
 * translation. The full header is 2048 bytes (0x800); everything past
 * these first few fields is region info / zero-filled, not needed here. */
typedef struct {
    uint32_t pc0;      /* initial $pc */
    uint32_t gp0;      /* initial $gp */
    uint32_t t_addr;   /* text section load (RAM) address */
    uint32_t t_size;   /* text section size in bytes */
} PsExeHeader;

/* Parses just enough of a raw PS-X EXE buffer's 2048-byte header to
 * locate its text segment. Returns TRACKDATA_OK, or
 * TRACKDATA_ERR_NOT_PSEXE if the "PS-X EXE" magic (first 8 bytes) isn't
 * present, or TRACKDATA_ERR_ARGS if buf_size < 2048. */
int trackdata_read_exe_header(const uint8_t *buf, size_t buf_size, PsExeHeader *out);

/* Converts a RAM address within the EXE's loaded text range to a byte
 * offset into the raw EXE file buffer (accounting for the 2048-byte
 * header). Returns TRACKDATA_ERR_OUT_OF_RANGE if `ram_addr` falls
 * outside [t_addr, t_addr+t_size). */
int trackdata_ram_to_file_offset(const PsExeHeader *hdr, uint32_t ram_addr, size_t *out_offset);

/* Parses `count` consecutive 20-byte section records starting at RAM
 * address `ram_addr` (normally one of the TRACKDATA_COURSE_*_RAM_ADDR
 * constants) out of a loaded PSX.EXE buffer. On success (TRACKDATA_OK),
 * *out is fully populated and owns allocated memory that must be
 * released with trackdata_free(). On failure, *out is left zeroed. */
int trackdata_parse(const uint8_t *exe_buf, size_t exe_size, uint32_t ram_addr,
                     size_t count, TrackData *out);

/* Frees memory owned by *td (if any) and zeroes it. Safe to call
 * multiple times or on an all-zero TrackData. */
void trackdata_free(TrackData *td);

/* Host-native (double-precision) reimplementation of func_80017B58's
 * confirmed along/lateral projection + off-track test (see file header
 * comment for the formula). `dx`/`dz` are the raw world-space offset of
 * the tested point from `sec`'s center (i.e. already `x - sec->x` / `z -
 * sec->z`, NOT yet rotated into the section's local frame -- this
 * function does that rotation internally using sec->heading_raw). Writes
 * the along-track progress (0..255, clamped) to *out_along and the
 * signed lateral offset to *out_lateral; returns nonzero if the point is
 * outside the section's track width (off-track). */
int trackdata_project_point(const TrackSection *sec, double dx, double dz,
                             double *out_along, double *out_lateral);

#endif /* RR_TRACKDATA_H */
