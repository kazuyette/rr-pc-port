# rr-pc-port roadmap

This project is a from-scratch PC port scaffold derived from
[rr-decomp](https://github.com/kazuyette/rr-decomp), a completed(-ish) PS1
decompilation of Ridge Racer (1994, Namco): 948/949 functions matched,
99.93% of the original binary's bytes.

## The key scoping fact

That 99.93% number is a *byte-match* percentage, not a *portability*
percentage, and the two are very different things here. Of the matched
code:

- **~2.5%** is genuine, hand-written, readable C -- 24 small functions
  (state-flag getters/setters, an abs-diff helper, a couple of
  wrap-around distance calculators, etc.), all with no hardware
  dependency. **These are the functions ported into this repo in phase 1.**
- **~97.5%** is `__asm__`-block transcription: raw MIPS instructions
  copied byte-for-byte from the original disassembly, because old GCC
  2.7.2 at `-O2` doesn't reproduce the PSY-Q compiler's exact
  register allocation/instruction scheduling from natural-looking C.
  Matching the target binary at the byte level was the *decomp's* goal;
  it says nothing about whether the logic has been re-expressed in a
  form a different compiler/architecture can use. It hasn't -- it's PS1
  R3000 assembly with a C calling convention wrapper.

So "99.93% decompiled" does **not** mean "99.93% of the way to a PC
port". Turning each of those ~924 asm-locked functions into real,
portable C (by reading the assembly and re-deriving what it's doing,
the same way the *non*-asm 24 were originally found) is the actual bulk
of this project, and is realistically a multi-month-to-year undertaking,
function by function, the same way the original decomp match-hunt was
an iterative, escalating effort over many sessions.

On top of that, a playable port needs a full hardware abstraction layer
for subsystems that don't exist on a PC at all: the GTE (PS1 fixed-point
geometry coprocessor), GPU primitive rendering (needs a software or
GL/Vulkan rasterizer built from scratch), SPU audio, BIOS syscalls, and
CD-ROM/disc data streaming.

## Phase 1 -- DONE (this commit)

Infrastructure and a trivial vertical slice, nothing more:

- Repo scaffolding (`src/`, `src/psx_compat/`, CMake build).
- `src/psx_compat/`: type-alias and BIOS-stub headers that future ported
  code can `#include` against (`psx_types.h`, `psx_bios.h` +
  placeholder `.c`). No real hardware behaviour yet -- pure compile-time
  shims.
- The 24 genuine C functions from `rr-decomp`'s `src/globals.c` and
  `src/stubs.c` ported verbatim (same logic, adapted to compile
  standalone on host GCC/Clang instead of the PSX cross-compiler).
- `CMakeLists.txt` targeting host gcc/clang (not the PSX cross toolchain).
  SDL2 is detected optionally via `find_package`/`pkg-config`; if absent,
  the build degrades to a headless logic-only binary rather than failing.
- `src/main.c`: proves the ported functions link and run correctly
  (prints their outputs against expected values), and -- when SDL2 is
  available -- opens a window, clears it, and runs a short event loop
  that exits on window-close or after a ~2s safety timeout (so it never
  hangs in a headless/CI environment; use `SDL_VIDEODRIVER=dummy` when
  there's no real display).
- No game asset files (disc image, PSX.EXE, textures, audio, track data)
  are or ever will be committed to this repo.
- Optional CI (`.github/workflows/build.yml`): `cmake . && make` on
  ubuntu-latest, to catch build breaks going forward.

Explicitly **out of scope** for phase 1: porting any asm-locked
function, GTE/GPU implementation, real rendering of game geometry, audio.

## Phase 2 -- Reimplement more genuine logic functions

Go back to the ~924 asm-locked functions in rr-decomp and, one at a time,
read the transcribed assembly + surrounding context (variable names,
call sites, Ghidra decompilation where available) and re-derive
plausible, behaviourally-equivalent portable C -- the same reverse
engineering process used to find the original 24, just applied at scale.
Prioritize: functions with few/no GTE or GPU register touches (pure
game-logic/state-machine code), since those port cleanly without needing
phases 3/4 first. Track progress the same way rr-decomp did (a
matched/total function counter), but the metric here is "ported to
portable C and unit-tested against the asm reference's behaviour", not
byte-match.

## Phase 3 -- Minimal software rasterizer + GPU primitive stubs

Implement enough of a PS1 GPU emulation (as a software rasterizer first;
consider an OpenGL backend later) to accept the primitive packet types
the game submits (flat/gouraud tris & quads, sprites, lines) and draw
something recognizable to an SDL2 texture/window.

### Phase 3, milestone 1 -- DONE: hardcoded flat-triangle rasterization

- `src/gpu/gpu_soft.h` + `gpu_soft.c`: a raw `uint32_t
  gpu_framebuffer[GPU_FB_WIDTH * GPU_FB_HEIGHT]` (320x240, packed
  0x00RRGGBB), `gpu_clear(color)`, and `gpu_draw_triangle_flat(x0, y0,
  x1, y1, x2, y2, color)` -- a standard barycentric/edge-function
  scanline fill, correctness over speed, no SDL dependency (touches
  only the raw pixel array, so it's testable and backend-agnostic).
- `src/gpu/gpu_soft_test.c`: standalone headless sanity check (own
  CMake executable, `rr_pc_port_gpu_test`, wired into `ctest` via
  `add_test`) asserting specific inside/outside pixels for a
  hand-computed right triangle, a second non-overlapping triangle
  (proves no bounding-box leakage), and a degenerate zero-area triangle
  (proves it's a safe no-op). All checks pass headless, no display
  needed -- this is how correctness was verified in this sandbox, which
  has no display.
- `src/main.c`'s SDL loop now does the real presentation pipeline every
  frame: `gpu_clear()` + three hardcoded `gpu_draw_triangle_flat()`
  calls (one centered red triangle, two smaller corner triangles in
  green/blue so it's visually obvious multiple primitives are being
  rasterized) into `gpu_framebuffer`, then `SDL_UpdateTexture` on an
  `SDL_TEXTUREACCESS_STREAMING` texture (`SDL_PIXELFORMAT_RGB888`,
  matching the packed 0x00RRGGBB layout) + `SDL_RenderCopy` +
  `SDL_RenderPresent`. This replaces the old flat `SDL_RenderClear` to
  a solid color. No real game geometry yet -- purely a hardcoded proof
  that framebuffer -> texture -> window works end to end, ready for
  phase 5 to eventually fill `gpu_framebuffer` from real geometry
  instead of hardcoded shapes.

### Phase 3, still to do

- **Gouraud-shaded triangles**: per-vertex color + interpolation
  (barycentric weights already computed in `gpu_draw_triangle_flat`,
  extending to `gpu_draw_triangle_gouraud` with 3 colors is the natural
  next step).
- **Textured polygons**: UV coordinates per vertex, a texture/CLUT
  sampling model matching the PS1 GPU's 4/8/16-bit texture page +
  palette system (this needs Phase 5's TEX asset loading to have
  anything real to sample from -- a checkerboard/solid placeholder
  texture is enough to prove the sampling code path first).
- **Quads and sprites**: the PS1 GPU natively submits flat/gouraud
  quads (two triangles) and sprites (axis-aligned rects, often with a
  fixed size); wrap `gpu_draw_triangle_flat`/`_gouraud` rather than
  duplicating the fill loop.
- **Lines**: simple Bresenham, lower priority (mostly used for debug
  overlays and a few UI elements, not core track/car rendering).
- **A `psx_bios.h` `PsxBios_GpuSubmit`-shaped entry point** that
  receives PS1-style GPU primitive packets (the actual on-disc/RAM
  packet layout the game code builds and submits, once traced from
  `rr-decomp`) and dispatches to the right `gpu_draw_*` call -- this is
  the real integration point between ported game-logic C and this
  rasterizer, still to be built.
- **Real geometry from Phase 5's asset loading**: once MAP.RRM/OBJ.RRO
  are decoded, feed actual track/object vertex data through the same
  primitive calls instead of the current hardcoded triangles. This is
  the point where "hardcoded triangle on screen" becomes "the actual
  game rendering something."
- `SVECTOR` / `VECTOR` / `MATRIX` in `psx_compat/psx_types.h` aren't
  used by the rasterizer yet (everything so far is 2D screen-space
  integers, matching what the real PS1 GPU primitive packets carry
  post-transform) -- they'll matter once the GTE-driven transform
  pipeline (project 3D model-space vertices to screen space) is ported,
  which is downstream of this milestone.

## Phase 4 -- BIOS / input / timing stubs

Flesh out `psx_compat/psx_bios.c` for real: `VSync` tied to actual
frame timing, SDL2 keyboard/gamepad mapped to the PS1 pad bit layout,
and enough of the BIOS syscall surface (mostly memory/thread/callback
management) that ported game-logic code can call into it without
special-casing.

## Phase 5 -- Asset loading (MAP.RRM / OBJ.RRO)

Once the track/object binary formats are reverse-engineered (tracked
separately in the track-editor feasibility investigation), add loaders
that turn on-disc MAP.RRM/OBJ.RRO data into the in-memory structures the
ported game logic and phase-3 rasterizer expect. Extracted assets
themselves are never committed here -- only the loader code, tested
against the user's own legally-owned disc image locally.

### Phase 5, round 1 -- MAP.RRM file-level structure decoded, standalone parser tool added

`tools/mapparse/` (`map_rrm.h`/`.c` + `mapparse_main.c`) is a standalone
host CLI (no PSX/game dependencies, not linked into `rr_pc_port`) that
parses a locally-supplied MAP.RRM file. It was written after tracing the
real PS1 boot-time load path in rr-decomp: the `\MAP.RRM;1` rodata
string -> a 10-entry CD filename pointer table -> `func_80032A54` (loads
all 8 CD-directory-resident data files at boot) -> `func_80032948`
(CD-read one file by directory entry) -> `func_800125B4` (the actual
MAP.RRM header/directory parser, called on the freshly CD-read buffer).

**Confirmed, byte-exact** (replayed `func_800125B4`'s accumulation
arithmetic against the real file and it consumes it with zero slack):
MAP.RRM is `uint16 section_count` (258 for RR1's one course) + that many
8-byte directory entries (`{count_a, count_b, count_c, count_d}`,
`count_d` observed always 0) + a single flat bulk-data stream of fixed
40-byte records, laid out per-section as `count_a` type-A records then
`count_b` type-B then `count_c` type-C, section by section. 258 sections
-> 6737 total records (622 A + 5420 B + 695 C) -> exactly fills the
remaining 269480 bytes of the 271548-byte file.

**Best-effort, not confirmed**: the 40-byte record's internal fields.
The first 24 bytes decode as four `int16[3]` vectors; for most type-B
records (by far the most common type) the 1st and 2nd vectors share
their middle component, and so do the 3rd/4th, matching the signature of
a road-surface quad's near-edge/far-edge corner pairs sharing a common
height. The remaining 16 bytes have weaker hypotheses only (candidate
heading angle, candidate group/material id, candidate flags word) -- see
the header comment in `map_rrm.h` for the full, explicitly-hedged
writeup, including what was tried and didn't confirm (no per-section
world-space transform was found, so a top-down plot of the raw v0
corners is a single dense local-frame blob, not a track outline -- this
was checked and is an honest negative result, not a bug).

`mapparse_tool` (new CMake target, `tools/mapparse/`, unrelated to the
main `rr_pc_port`/`rr_pc_port_gpu_test` targets, builds cleanly alongside
them with zero warnings under `-Wall -Wextra`) prints this summary and
can optionally dump a CSV of every parsed record or a color-coded PPM
scatter plot for visual sanity-checking. Verified locally against the
real MAP.RRM (asset never committed): `bytes_consumed == file size`
exactly, matching the byte-exact math above.

Not yet done: OBJ.RRO (scenery objects) and IDX.HED were not
investigated this round (loader functions `func_80012670` and
`func_80015CD4` were located and IDX.HED's stride at least skimmed
during this round -- see the fuller project notes -- but not decoded to
the same level as MAP.RRM); the 16 not-yet-understood bytes of the
MAP.RRM record; and no per-section transform, so nothing here is wired
into the phase-3 rasterizer yet. That's the natural next round.

### Phase 5, round 2 -- IDX.HED decoded as a world-space grid, partial MAP.RRM world transform, partial OBJ.RRO directory decode

**Big find: IDX.HED is a 32x32 spatial hash grid, CONFIRMED byte-exact,
and it is the missing MAP.RRM world-placement key.** Found by reading
`func_80012C14` (a 64-step expanding-ring nearest-cell search used by
gameplay/collision code) instruction-by-instruction: it indexes IDX.HED
as `int16 grid[32][32]` (1024 entries * 2 bytes = 2048 bytes = the
file's exact real size), where each occupied cell holds a MAP.RRM
section index and empty cells hold `-1`. Checked exhaustively against
the real files: exactly 258 of 1024 cells are occupied, and the 258
values are the complete set `{0..257}` with **no repeats and no
gaps -- a perfect bijection** between IDX.HED grid cells and MAP.RRM
sections. Just plotting which cells are occupied (nothing from MAP.RRM
involved at all) traces an unmistakable closed-loop racetrack shape, and
color-coding by section index shows the section order flows
continuously around that loop. `func_80012C14` also confirms the grid's
cell size: world position is divided by 2048 (`>>11` with round-to-
nearest) to get a cell coordinate -- a directly-read, not guessed,
constant.

Also cleared up a wrong assumption from last round's task framing:
`func_80015CD4` (hypothesized as "the IDX.HED parser") turns out **not**
to parse IDX.HED's contents at all -- it's a 15-instruction init
function that just stores the raw buffer pointer into a global
(`D_801D82D0`) and resets a few unrelated state globals. The real shape
of IDX.HED only became clear from tracing `func_80012C14`, which reads
through that pointer.

**Partial, empirically-tuned world transform**: placing each MAP.RRM
section's raw local record coordinates at `(mirrored_grid_col,
grid_row) * 2048` (translation only, no rotation) produces a plot with
real, recognizable road structure -- correct near/far quad edges, a
clean sweeping curve and a long straight -- for roughly half the
course. The remainder (tighter, more curved sections) still overlaps
into a tangle. Two rotation hypotheses were tried (rotating each
section by its records' candidate "heading" field; rotating by the
direction to the next/previous section's grid cell) and **both made the
result visibly worse**, so neither is applied -- the missing rotation
component is a confirmed open problem, not yet found. New tool
`worldmap_tool` (`tools/mapparse/idx_hed.h/.c` + `worldmap_main.c`)
combines MAP.RRM + IDX.HED using this transform and exports a PPM; see
`idx_hed.h`'s header comment for the full confirmed/hypothesis
breakdown.

**OBJ.RRO: directory structure decoded, data blobs not yet.** Read
`func_80012670` (the OBJ.RRO parser) the same way. Confirmed: `uint32`
object count (319 in the real file) + that many 16-byte directory
entries, each holding 6 `int16` sub-counts multiplied by fixed
per-item-type byte sizes (32/40/48/56/64/72 -- a clean `n*8` progression
for `n=4..9`) that sum to a per-object byte size, accumulated into a
running pointer written back into the following entry (same pattern as
MAP.RRM's section-offset table, just done in-place in the OBJ.RRO
buffer). Unlike MAP.RRM, this does **not** close exactly: the computed
total (344428 bytes: 5108-byte directory + 339320 bytes of per-object
data) falls short of the real 445348-byte file by 100920 bytes. Hex
inspection at that boundary shows a distinct block that looks like
vertex/coordinate data, so the leading hypothesis is a separate trailing
vertex pool that `func_80012670` doesn't walk -- not confirmed, and the
function that actually reads the per-object pointers this one writes
(and would reveal what the data blobs contain) was not traced this
round. New tool `objparse_tool` (`tools/mapparse/obj_rro.h/.c` +
`objparse_main.c`) parses and reports the confirmed directory-level
summary, including the unaccounted-bytes gap (surfaced, not hidden).

Next round: (1) find the missing per-section rotation (candidates not
yet tried: a per-*record*, not per-section, incremental heading chain;
searching for a rotation/orientation field elsewhere in the section
directory or IDX.HED itself); (2) trace whoever reads OBJ.RRO's
per-object pointer field to decode the actual object data format; (3)
once the transform is solid, wire real track geometry into the phase-3
rasterizer.

### Phase 5, round 3 -- rotation hunt: 3 more hypotheses ruled out + GTE render-time rotation confirmed; OBJ.RRO ptr_field re-characterized

**Rotation still NOT decoded from the file, but the search space is now
much smaller and one important new fact is nailed down.** Four things
tried this round (full reasoning + numbers in `idx_hed.h`'s header
comment and project memory):

1. Re-checked IDX.HED cell values across all 258 occupied cells for
   packed bits beyond a plain section index -- **ruled out**, zero
   stray high bits found, no room for orientation data there.
2. Re-checked MAP.RRM's `count_d` across all 258 directory entries (not
   a sample) -- still 0 everywhere, and since `count_a/b/c`'s
   arithmetic already accounts for the file's exact size with zero
   slack, there is no room to hide a rotation field in that 8-byte
   directory entry either -- **ruled out**.
3. A new pure-geometry "dead reckoning" chain solve: derive each
   section's rotation+translation purely from matching its first
   record's entry edge to the previous section's last record's exit
   edge (both edge orientations tried), propagated around the full
   258-section loop -- **ruled out**. The loop failed to close by
   42,000-88,000 world units (the whole track only spans ~65,536
   units), and per-transition heading deltas were essentially random.
   Sanity check confirmed *within*-section record chaining (no
   transform) DOES work cleanly, so the problem is specifically that
   "last record of section i / first record of section i+1" isn't a
   reliable join point in general -- likely because the large
   multi-record sections (9-12, with 36-69 records each) are branching
   clusters, not simple paths.
4. **New confirmed fact**: traced the render-time consumer of the
   MAP.RRM section table (`func_80035638`) into its helper functions
   `func_8004006C`/`func_80040140` and confirmed, instruction-by-
   instruction, that they execute real PS1 GTE `rtps`/`rtpt` hardware
   perspective-transform instructions with a `ctc2`-loaded rotation
   matrix. **Rotation genuinely is applied via a hardware matrix
   multiply at render time**, not stored in the file -- but the trace
   didn't reach the call site that actually *constructs* a fresh
   per-section matrix (two nearby functions checked, `func_80043738`/
   `func_80043794`, turned out to only sign-flip/mirror the
   already-loaded matrix, not build a new one). Concrete next-round
   lead: find every `ctc2` write into GTE control regs 0-4 and trace
   its source value backward.

**OBJ.RRO**: the "ptr_field" directory column (previously "0 or a small
placeholder") is now known to split cleanly into 3 buckets on the real
file -- 137 zero, 142 small integers (1-1000), and 40 that are all
(100%, exhaustively checked) within a small tolerance of an exact
multiple of 65536, i.e. plausibly 16.16 fixed-point small integers
(1, 4, 5, 7, 8, 16, 29 observed) -- not simple padding after all,
though still write-only from `func_80012670`'s perspective (no
consumer traced yet). Also corrected an overstated claim from last
round: the trailing unaccounted region's 0x0FFF-sentinel frequency
(0.404%) is actually comparable to the already-accounted region's
(0.634%), not a distinguishing signature -- the trailing region is
still clearly structured data (period-6-byte local repetition
consistent with 3x int16 vectors), just not decoded exactly. Both new
analyses are runnable, verifiable stats (no raw game bytes committed)
added to `objparse_tool`.

Zero regressions: full build (`rr_pc_port`, `rr_pc_port_gpu_test`,
`mapparse_tool`, `worldmap_tool`, `objparse_tool`) still clean under
`-Wall -Wextra`, `ctest` still 1/1 pass, `worldmap_tool` still reports
"sections placed: 258 of 258". No rasterizer wiring this round --
correctly held back per the standing rule of only wiring in a
validated transform, and the transform is still incomplete.

## Phase 6 -- Audio

SPU emulation or a from-scratch replacement audio engine, once there's
enough of the game loop running to actually need sound cues. Lowest
priority; a fully silent, correctly-playing game loop is a fine
milestone before this.

## Later / unscheduled

Save states, higher internal resolution / widescreen, proper input
remapping UI, packaging/distribution. None of this matters until phases
2-4 produce something that actually plays.
