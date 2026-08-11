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

### Phase 3, milestone 2 -- DONE: gouraud shading, lines, quad helper, animated demo scene

A lighter, direct-coding round (deliberately not track-format RE work):
extended `gpu_soft` with the primitive types milestone 1 deferred, and
made the demo scene actually move instead of sitting static.

- **`gpu_draw_triangle_gouraud(x0,y0,x1,y1,x2,y2, color0,color1,color2)`**
  (`src/gpu/gpu_soft.h`/`.c`): per-vertex packed 0x00RRGGBB color,
  linearly interpolated per-pixel via the same barycentric
  edge-function weights `gpu_draw_triangle_flat` already computes for
  its inside test (`w0/area`, `w1/area`, `w2/area` are the actual
  barycentric coordinates for vertices 0/1/2; blend each RGB channel
  by those weights, integer math throughout). Same scanline structure,
  same degenerate-triangle no-op behavior, as the flat version -- a
  real PS1 POLY_G3/G4-shaped primitive, not a demo-only shortcut.
- **`gpu_draw_line(x0,y0,x1,y1,color)`**: standard integer Bresenham,
  handles all octants plus the horizontal/vertical/single-point edge
  cases (matches the PS1 GPU's LINE_F2 primitive).
- **`gpu_draw_quad_flat(x0,y0,x1,y1,x2,y2,x3,y3,color)`**: thin wrapper
  around two `gpu_draw_triangle_flat` calls sharing the (v0,v2)
  diagonal -- groundwork for wiring in real MAP.RRM road quads later,
  since that record format is quad-shaped per-section geometry.
- **Animated demo scene** (`src/main.c`'s `draw_animated_scene`,
  replacing the old static `draw_hardcoded_scene`): a gouraud-shaded
  triangle orbits and spins around the screen center (`SDL_GetTicks()`-
  driven angle, per-vertex colors cycling via a phase-offset cosine
  blend so the shading itself animates, not just the position); the
  original two flat corner triangles are unchanged in shape but now
  gently pulse in size (scaled from their own centroid) and color-cycle
  instead of sitting static, proving `gpu_draw_triangle_flat` still
  works unmodified; a faint background grid and a center crosshair
  exercise `gpu_draw_line`. Everything derives from a single
  `now_ms` time source passed in from the SDL loop so it all stays in
  sync. The window-stays-open-until-closed/Escape behavior and the
  headless/CI 2-second safety cap (`SDL_VIDEODRIVER=dummy`) are both
  unchanged.
- `src/gpu/gpu_soft_test.c`: extended with gouraud-triangle checks
  (near-vertex dominant-channel checks with a small tolerance rather
  than exact color, since barycentric weight isn't exactly 1 one pixel
  off a vertex; an even-blend midpoint check; degenerate-gouraud
  no-op), line checks (horizontal/vertical/45-degree endpoints +
  midpoints, a just-off-the-line negative check, and a single-point
  line producing exactly one pixel with no infinite loop), and quad
  checks (all four corners plus center filled, outside untouched).
  Still fully headless, no display needed, still wired into `ctest`
  (`gpu_soft_sanity`) alongside the original flat-triangle checks,
  which are unchanged and still pass. `CMakeLists.txt` now also links
  `libm` for `rr_pc_port` (the animated scene's `sin`/`cos` calls),
  found via `find_library` so it's a no-op on platforms where math
  functions are already part of libc.
- Verified in this sandbox (no SDL2 dev headers available here, same
  as milestone 1): the headless `rr_pc_port`/`rr_pc_port_gpu_test`
  build clean under `-Wall -Wextra` with zero warnings, `ctest` passes,
  and all 63 previously-ported Phase 1/2 logic functions still produce
  their expected outputs. The `HAVE_SDL2` code path (including the new
  `draw_animated_scene`) was additionally syntax- and runtime-checked
  by compiling/linking `src/main.c` against a minimal stand-in SDL2
  header + stub implementation reproducing the same call sequence a
  real SDL2 would make (including a "window open, ticking forward"
  stub that actually executes `draw_animated_scene` every frame) --
  it compiles warning-free and runs to a clean exit with no crash. This
  is not a substitute for testing against real SDL2 (the user's WSL
  build is the real verification), just extra confidence given this
  sandbox has no SDL2 dev package available.

### Phase 3, still to do

- **Textured polygons**: UV coordinates per vertex, a texture/CLUT
  sampling model matching the PS1 GPU's 4/8/16-bit texture page +
  palette system (this needs Phase 5's TEX asset loading to have
  anything real to sample from -- a checkerboard/solid placeholder
  texture is enough to prove the sampling code path first).
- **Sprites**: axis-aligned rects, often with a fixed size; likely
  another thin wrapper, same spirit as `gpu_draw_quad_flat`.
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

### Phase 5, round 4 -- full `ctc2` census (rotation still not decoded, but the search reframed with real new evidence)

**Rotation still NOT decoded, but this round eliminated an entire wrong
framing and dug one level deeper into the call graph.** Full reasoning
in `idx_hed.h`'s header comment and project memory ("Phase 5 round 4").

1. Enumerated every `ctc2` in the whole disassembly: 161 total, all in
   `asm/29E8.s` (the data files have zero). Only 12 functions write
   GTE control regs 0-4 (the rotation matrix); all 12 were read
   instruction-by-instruction and are confirmed to be **generic**
   PSY-Q-style matrix-library primitives (compose-two-matrices via
   `mvmva`, a save/apply-temp-matrix/restore utility, a direct 3-word
   loader) that take an *already-built* MATRIX struct pointer as an
   argument -- none of them compute matrix cells from an angle via a
   sin/cos table. **Reframing**: if this binary builds a rotation
   matrix from an angle at all, that construction can't be found by
   grepping for `ctc2` -- it would use plain multiply/shift + memory
   stores to a MATRIX struct, only loaded into hardware *later* by one
   of these 12 generic functions.
2. New structural finding: `func_80035638` (called "the render walker"
   since round 3) is one of **13 sibling functions** in a contiguous
   ~14 KB code block (`func_80033BFC`..`func_80037478`) sharing the
   identical mirror-flip + `rtps`/`rtpt` inner pattern -- this whole
   block is the game's shared 3D geometry draw module. 6 of the 13
   (including `func_80035638`) have zero static callers anywhere in
   the disassembly and are reached only through runtime-computed
   `jalr` dispatch (confirmed: none of the file's 59 `jalr` sites build
   their target from a static literal address, and no matching pointer
   exists in either data file) -- not resolvable by text search alone.
3. Traced one concrete link successfully: `func_80012E44` (7 call
   sites, each exactly once per invocation, no loop -- "draw one
   batch/frame" not "once per section") calls
   `func_80043470(a0=D_801E91F0)` (one of the 12 rotation-writing
   functions) immediately before calling the first two members of the
   13-function draw module. `D_801E91F0` turned out to be a heavily
   shared (150+ xref) scratch matrix touched by clearly unrelated
   subsystems too (roadside-object visibility culling against the
   already-known OBJ.RRO count global and a 2048-unit world-cell
   bitmask) -- a general "currently active transform" register, not a
   track-section-dedicated field. The write site that actually
   populates its rotation cells from section/heading data was not
   found this round (ran out of budget on this thread, not ruled out).
4. A live Ghidra MCP bridge was tried specifically to resolve the
   indirect `jalr` targets from point 2, but was not connected on the
   user's device this round -- reconnecting it is the top tool
   prerequisite for the next round, since Ghidra's own dataflow
   analysis can resolve indirect calls that pure disassembly grep
   structurally cannot.

Zero regressions: this round was investigation + documentation only
(no functional code changes), full build/test suite untouched and
still passing. No rasterizer wiring -- rotation still unresolved.

### Phase 5, rounds 5-6 -- Ghidra-assisted rotation hunt (investigation only, no code changes; see project notes)

Two further rounds traced the render-time GTE rotation call graph using a
live Ghidra MCP bridge (not available in every session): the section
table's scratchpad pointer (`DAT_1F80003C`) has **zero static reads**
anywhere in the binary, and a full 41-caller xref census of the BAMS trig
table found no call site that builds a rotation matrix from MAP.RRM
section data -- confirming the road renderer is reached exclusively
through a runtime function-pointer dispatch invisible to static
analysis. Round 6 also ran an exhaustive 64-combination brute-force
search over every plausible sign/axis convention for the existing
translation-only transform: all 64 combinations tie for best score, and
the best case still only gets 5.9% of grid-adjacent section pairs to a
clean (<200 unit) junction -- proving numerically that a *real*
per-section rotation is structurally required, not a convention bug.
Neither round changed any code in this repo (investigation only); full
detail in project memory, not duplicated here.

### Phase 5, round 7b -- empirical/statistical rotation reconstruction attempt: NEGATIVE result, but a stronger and more direct one than rounds 3-6

With 6 rounds of static binary analysis exhausted (see above), this
round tried a completely different approach: instead of finding the
game's exact mechanism, **least-squares fit** a per-section rotation
directly from the geometry -- i.e. build a working reconstruction even
without decoding the original algorithm. New standalone tool:
`tools/rotation_fit/rotation_fit.py` (Python + numpy, deliberately
outside the CMake build -- a research tool, not production parsing
code). Run it yourself against your own legally-owned files:

```
python3 tools/rotation_fit/rotation_fit.py <MAP.RRM> <IDX.HED> --figure report.png
```

**New structural finding along the way**: the previously-assumed "type-B
records chain end-to-end within a section" model needed correcting --
the real near/far edge correspondence is `record[k].v0,v1 ~=
record[k+1].v2,v3` (the opposite direction from earlier rounds' guess),
and it only holds within short runs a few records long, broken by
`group_id` boundaries *and* periodic resets even within one `group_id`
(most likely tiled/repeating geometry, e.g. guardrail segments, each
restarting from its own local origin) -- a section's type-B run is not
one single path, contrary to what was assumed for the round-3 dead-
reckoning attempt.

**Two independent rotation-fitting methodologies were tried:**

1. A chained per-section rotation fit propagated around the full
   258-section index-order loop (closed-form optimal-rotation-only
   alignment at each step). The final wraparound angular closure gap
   was small-looking (~12-18 degrees), but the *per-transition* fit
   quality along the way was bad -- median ~4500 world-unit corner
   mismatch, only ~18% of transitions within one grid cell (worse than
   the existing translation-only baseline's 85.9%). A small closing
   angle built from a chain of individually bad fits isn't trustworthy
   on its own.
2. **The methodologically decisive test**: independently (not chained)
   brute-force fit the best rotation for each of ~300 IDX.HED-grid-
   adjacent section pairs, then check **cycle consistency** around the
   98 real 2x2 "small loops" in the grid graph (four sections where all
   four pairwise edges are grid-adjacent) -- if a single true
   per-section rotation existed, the four independently-fit relative
   rotations around each loop should sum to ~0 degrees. They do not:
   median closure error 65-66 degrees (mean ~70), barely better than
   the ~90 degrees expected from unrelated/random angles, and only 8.2%
   of loops close within 10 degrees. A control test against random
   unrelated section pairs confirms the underlying "nearest-corner"
   metric isn't simply too permissive to be meaningful (random pairs do
   much worse), so this is a real negative finding about the model
   shape, not a metric artifact.

**Conclusion**: a single rigid rotation per section (about the section's
local coordinate origin, combined with the existing IDX.HED grid-cell
translation) is **not** a coherent model of this track's true geometry.
This is a stronger result than round 3's "dead reckoning accumulates
error" finding, because the cycle-consistency test uses local,
non-accumulated evidence (four one-hop fits, not a walk around a large
chain) and still fails -- ruling out not just "the fitting/chaining
algorithm has bugs" but the basic shape of the model itself. Also
tested: no correlation (all \|Pearson r\| < 0.1) between the fitted
rotation and any of the candidate `heading`/`unk_1e`/`group_id`/etc.
record fields, under both methodologies -- consistent with round 2's
earlier exclusion of `heading` as a direct rotation value, now checked
against real per-transition deltas too.

Per the standing project rule, **no track geometry was wired into the
Phase 3 rasterizer this round** (gated on a working transform, which
this round did not produce). Honest next-round recommendation: a
richer model is needed before trying rotation-fitting again -- most
plausibly a continuous/per-record orientation (the road curves
smoothly; one angle per whole section is too coarse) and/or a free
translation offset within the grid cell rather than a literal
corner-anchor. A "smarter" global least-squares/pose-graph optimization
over the *same* single-rotation-per-section model is not expected to
help, since the cycle test shows the underlying per-pair constraints
are already mutually inconsistent at the local level, not just noisily
chained over distance.

Zero regressions: full build (`rr_pc_port`, `rr_pc_port_gpu_test`,
`mapparse_tool`, `worldmap_tool`, `objparse_tool`) still clean,
`ctest` still 1/1 pass. `tools/rotation_fit/` is pure-Python and not
part of the CMake build, so it can't break the C build.

## Phase 5, round 8 -- the section-rotation mystery resolved via live dynamic debugging

After 7 rounds of static analysis (6 RE + 1 empirical least-squares
attempt) failed to locate any code that builds a per-section rotation
matrix from MAP.RRM data, this round finally set up live dynamic
debugging: PCSX-Redux's built-in GDB server (`Configuration ->
Emulation -> Enable GDB Server`, port 3333) plus `gdb-multiarch`
(connected via the WSL gateway IP, not `localhost`, which timed out --
almost certainly a Windows Firewall interaction with the WSL virtual
adapter). A hardware **read** watchpoint (`rwatch`) on the scratchpad
pointer `DAT_1F80003C` -- identified back in round 6 as having zero
static reads anywhere in the binary -- broke immediately and
repeatedly during real gameplay, at PC `0x800437F4`.

Tracing the call chain from that address (now documented in Ghidra,
`DrawSectionRecords_fromDirTable_TranslateOnly` at `0x800437AC`) up
through its caller's `$ra`, and one more hop up to the actual static
caller `FUN_80012e44`, resolves the mystery completely: that function
calls `SetRotMatrix(&DAT_801e91f0)` where `DAT_801e91f0` is
*the live camera view matrix* (confirmed by xref -- it's written only
by `BuildCameraViewMatrix_fromLiveEuler`, the same camera-Euler chain
characterized in round 5), then draws each nearby section using a pure
translation (`grid_cell*2048 - car_position`, rotated into camera
space) with **no additional per-section rotation applied anywhere**.

**Conclusion: there is no per-section rotation in this game's track
rendering.** All 7 prior rounds were searching for a mechanism that
does not exist in this form. What remains open (and is *not* explained
by this finding) is why the round-6 "translation only" offline
reconstruction still failed 94% of grid-adjacent junction checks --
since the game itself only translates, the bug is presumably a scale
or coordinate-interpretation mismatch in the offline parser/tools, not
a missing rotation. Concrete next step for a future round: read the
real in-memory record bytes right before the `RTPT` in
`DrawSectionRecords_fromDirTable_TranslateOnly` via the same GDB setup
and diff them against what `mapparse_tool` computes offline for the
same section -- the discrepancy should point directly at the bug (a
likely suspect: the game left-shifts the translation tuple by 2
(`<<2`) before use, which may not be mirrored in the offline tooling).

This is also the first round where the dynamic-debugging setup
(PCSX-Redux GDB server + gdb-multiarch + a targeted watchpoint) proved
itself as a fast, reusable technique for this project -- it resolved
in one session what 7 rounds of pure static analysis could not.

## Phase 3 milestone 3 -- real game textures decoded and rendered natively

Reverse-engineered the `TEX0-4.TMS` texture container format (PS1 TIM
pages with an extra 4-byte CD-streaming skip-length prefix per page)
from the binary's texture-upload routine and cross-checked it against
the real local files byte-for-byte. New standalone tool
`tools/texparse/` (`tim.h`/`tim.c` decoder + `texparse_tool` CLI,
mirroring the `tools/mapparse/` pattern -- reads a user-local
`TEX*.TMS` path at runtime, nothing committed) decodes CLUT-indexed
(4bpp/8bpp) and 16bpp-direct pages to RGBA. Verified against real data:
TEX0.TMS parses cleanly into 143 pages, several visually confirmed
(dumped locally, never committed) as recognizable game art -- a HUD
font/text atlas, the Mappy mascot sprite, a NAMCO logo/plate texture.

`src/gpu/gpu_soft.c` gained `gpu_draw_quad_textured()` (affine
barycentric UV, nearest-neighbor sampling, alpha-keyed transparency),
covered by new headless tests in `gpu_soft_test.c`. `src/main.c` now
accepts an optional texture-file path argument and, when given one,
decodes and renders a real texture page full-screen instead of the
synthetic animated demo -- proving the full pipeline (real PS1 texture
bytes -> decoded RGBA -> software-rasterized on a real PC window) end
to end. No game assets committed anywhere; `rr_pc_port_texdemo_test`
in CTest exercises the same path headlessly and no-ops cleanly in CI
where no local asset path is configured.

## Phase 6 -- Audio

SPU emulation or a from-scratch replacement audio engine, once there's
enough of the game loop running to actually need sound cues. Lowest
priority; a fully silent, correctly-playing game loop is a fine
milestone before this.

## Phase 7 -- Physics reverse-engineering + a first real-physics driving mode

Prompted by wanting the `--track` drive mode to eventually feel like the
real PS1 game instead of a free-flying camera. Full RE writeup lives in
rr-decomp's project session notes (`rr_pc_port_physics_round1.md`,
rounds 1-8) -- summary of what landed in this repo:

- Found and confirmed the PS1 game's actual live race/gameplay state
  (`FUN_80014c2c`, one of the main dispatch loop's 40 states -- the
  other 39 turned out to be menus, an idle "press start" preview, or
  canned replay/attract-demo playback, not player-driven gameplay) and
  its per-car physics update (`FUN_8001c490`): a 6-speed gearbox,
  surface/pitch-influenced acceleration, position integration, a
  dedicated steering/track-reference subroutine, and wall/car collision
  with a bounce-and-slow response.
- Decoded the game's two built-in course-geometry tables (`D_8005A44C`
  / `D_8005CC4C` in the original PSX.EXE, 256 and 368 track sections
  respectively) down to the exact byte layout, verified by an
  instruction-level trace of the game's own point-projection function
  (not guessed from statistics) -- see `tools/trackdata/trackdata.h`.
  Confirmed the decode is correct via independent sanity checks: both
  tables form closed loops, headings wrap cleanly at the BAM12 boundary,
  and the (newly-discovered) asymmetric left/right track width varies
  in a way that reads as real level design (a one-sided track widening,
  e.g. a pit-lane merge), not decode noise.
- Added `tools/trackdata/` (parser + CLI, same pattern as
  `tools/mapparse`/`tools/texparse`): extracts both course tables
  directly from the user's own PSX.EXE (never committed here -- exactly
  like every other real asset this repo's tools touch).
- Added `src/physics.c`/`physics.h`: a portable-C physics core --
  gear-shift thresholds and the track off-track/lateral-offset test are
  faithful ports of the confirmed PS1 formulas; forward acceleration/
  braking/steering feel is an original, clearly-labeled approximation
  (the exact accel curve and turn-rate constants weren't extracted this
  round -- see the TODO at the bottom of `physics.c`). Covered by a
  headless test (`rr_pc_port_physics_test`, wired into `ctest`) that
  exercises the gearbox thresholds, the asymmetric off-track test, and
  the BAM12 heading-wraparound blend against hand-built synthetic data.
- Wired a first cut into `src/main.c`'s drive mode: pressing **C**
  toggles "real physics" driving (continuous throttle/brake/steer via
  held arrow keys, hold shift to shift gears manually, periodic
  gear/speed console readout) in place of the free-cam step movement.
  An optional `--physicsdata <PSX.EXE>` flag loads course A's real
  geometry into the running drive loop, so the periodic console readout
  reports the car's actual section index, lateral offset, and
  on/off-track status against the real course, not just free-cam
  position.
- Round 10: traced `func_80017DF4` in full and decoded two more
  per-record track-section fields (`aux_a_raw`, a second BAM heading
  `aux_heading_raw`, role not yet confirmed for either).
- Round 11: `func_80017DF4` turned out NOT to search all sections every
  frame -- it walks locally (backward then forward) from the previous
  frame's section index. Ported that same local-walk *spirit* (not a
  byte-exact copy of its edge-crossing test) as
  `physics_find_section_local_walk` in `src/physics.c`, unit tested
  against a hand-built 8-section closed loop (cold start, forward step,
  backward step, wraparound at the loop-closure boundary all covered),
  and wired it into `src/main.c`'s physics-mode HUD update in place of
  the old full-scan `physics_find_nearest_section`, seeded with a
  persistent section index across frames like the original.
- Round 12: traced `func_80017838` (the edge-crossing boundary test) and
  `func_80044D0C`/`func_80044E2C` (the BAM12 sin/cos lookups) fully.
  `func_80017838` is now CONFIRMED byte-exact: a plain 2D cross-product
  "which side of the directed line A-B is point P on" test. The BAM trig
  scale is now CONFIRMED Q12 (raw table value / 4096.0), verified
  against the actual lookup table bytes -- resolving that long-open TODO
  with no code change needed (this port's real `sin()`/`cos()` calls
  were already numerically equivalent). Also traced `func_800178A0` (the
  edge/kerb-point builder): its structure is confirmed (a section's
  right-edge and left-edge points, in car-relative coordinates, built
  from `heading_raw` rotated 90 degrees and scaled by the per-side
  width), but its X-channel carries an extra additive constant
  (`D_801733A0`, confirmed to be the plain value 61440 with one write
  site) that the Z-channel doesn't have, for reasons not established --
  so this round did NOT port `func_800178A0`/`func_80017838` into
  `physics_find_section_local_walk`; see `src/physics.h`'s updated
  comment for why the current center-to-center midpoint boundary test
  is being kept deliberately rather than swapped for a not-fully-
  understood "more original" one. Full writeup in
  `tools/trackdata/trackdata.h`'s file header.
- Round 13: traced `func_80017DF4`'s final OUTPUT computation (the
  value it hands back to `FUN_8001c490` for steering) byte-exact and
  ported the formula shape as `physics_steering_reference_raw()` in
  `src/physics.c`, with 4 new unit tests. Corrected round 10's claim
  that the aux scalar (+0x08) gets written into the "live car struct" --
  it's actually a stack-local scratch buffer, not a persistent field;
  traced its real one-shot conditional consumption in `FUN_8001C490`,
  which reads much more like checkpoint/lap-validation logic than a
  physics term. Also revised the `aux_heading_raw` (+0x0C) guess: the
  traced formula uses it as both a magnitude AND an angle-offset in a
  sine-based correction, which reads as steering/target-heading guidance
  rather than banking/camber. Attempted `FUN_8001bd9c`/`FUN_8001b374`
  (collision) next but stopped short: both operate on a large per-car
  state struct (two distinct pointers, dozens of fields by raw offset)
  that isn't mapped yet, and a ~700-instruction trace against an
  unmapped struct risked an inaccurate port -- flagged as the natural
  focus for a dedicated future round instead of guessing. Checked
  `D_801733A0`'s other call sites (5 total across the codebase): all
  apply the same X-only asymmetry, raising confidence it's deliberate,
  but its purpose still isn't established.
- Round 14: corrected round 13's "two distinct pointers" claim --
  `FUN_8001c490`'s `$s1`/`$s2` are actually ONE struct (`$s2 = $s1 +
  0x58`), not two. Mapped and CONFIRMED several of its fields (gear at
  +0x82, RPM accumulator at +0x84, the already-known-from-round-3 accel
  accumulator at +0xAC, plus a state flag at +0xB4, a ramp timer at
  +0xA0, and two button-press flags at +0xC8/+0xCA) -- see the new
  struct-map comment at the top of `src/physics.c`'s file-level TODO
  block. Along the way, substantially resolved `DAT_80077140`: traced
  both its write site (`func_80019CF4`, driven by the +0xC8/+0xCA
  button flags) and its read site (the accel-accumulator ramp in
  `FUN_8001c490`) fully -- it's a GLOBAL one-pole low-pass filter over a
  button-triggered signal, i.e. most likely the game's boost/turbo
  mechanic's "kick" curve, not a friction or grip term as earlier
  guessed. Also caught a genuine field-reuse trap: +0x82 means gear
  ONLY when another flag (+0x80) is zero; otherwise the same two bytes
  are a different counter entirely -- documented so a future round
  doesn't get bitten by it. No functional code changed this round
  (still can't confidently port DAT_80077140's effect without knowing
  which button triggers it and its real-world scale) -- documentation
  and struct-mapping only, verified by an unchanged (still green) test
  suite.

- Round 15: first "actually feels like a racing game" gameplay change --
  off-track status now feeds the live physics EVERY FRAME (previously
  `physics_find_section_local_walk`/`physics_track_project` only ran
  for the 0.5s HUD printout, cosmetic only). `physics_car_integrate`
  gained an `off_track` parameter that scales down top speed and
  acceleration when the car is off the track surface, and bleeds
  existing speed down toward the new (lower) cap on top of just
  clamping future acceleration -- so driving onto the grass at pace
  now visibly slows the car, not just stops it from a further speed-up.
  Explicitly labeled this port's OWN arcade-feel convention (not an
  RE'd formula -- see physics.c's comment) since the original's actual
  off-track *response* lives inside `FUN_8001c490`'s not-yet-traced
  surface-influenced accel path; only the off-track *detection* itself
  is the confirmed part (already ported since round 9). 2 new unit
  tests. `src/main.c`'s drive loop restructured so the local-walk +
  off-track check run every physics step, not just twice a second.

- Round 16: structurally traced both collision functions in full
  (instruction-by-instruction through both, see physics.c's TODO block
  for the complete writeup). Corrected an implicit assumption from
  earlier rounds -- these are two SEPARATE sibling probes, not one
  "collision function": `func_8001B374` is CAR-vs-CAR (a struct-of-
  arrays "other slot" table, up to 12 entries, each checked against 6
  fixed local hull points via full OBB-in-other's-frame transforms,
  CONFIRMED box thresholds ~51 wide x ~101 long asymmetric-forward);
  `func_8001BD9C` is CAR-vs-WALL (4 probe points sampled around the car
  via a 4-direction lookup table, each checked against the track
  boundary through the same edge-crossing primitive family as the
  round-12-confirmed `func_80017838`). Both trigger sound effects via
  already-known functions (`func_80032F50`, `func_8003A958`) and write
  hit data into globals, but what CONSUMES those globals to actually
  bounce/slow the car was not reached this round, nor were their two
  callees (`func_8001BAFC`, `func_80017B58`) traced, nor the two
  lookup tables' real byte values read out. `func_8001B374` also can't
  be ported without a multi-car simulation this port doesn't have yet.
  No functional code changed -- documentation-only round, tests still
  green.

- Round 17 ("enlève tous les obstacles"): cleared most of round 16's own
  punch list. Traced `func_8001BAFC` in full -- it's the REAL byte-exact
  version of `physics_find_section_local_walk` (backward-then-forward
  walk via `func_800178A0`+`func_80017838`, operating on a Q8 fixed-point
  section index; confirmed `D_801E90E0` = loaded course's section count).
  Traced `func_80017B58` in full -- a second independent per-section
  along/lateral projection routine, confirming the in-RAM TrackSection
  field layout used by this function family and cross-confirming
  `D_801733A0`'s X-only bias pattern in a SECOND function. Grepped the
  whole codebase for readers of the wall-probe hit globals and found the
  real response consumer, `func_80033584`: it combines the 4 probes'
  recorded values as (front_left-front_right)+(rear_left-rear_right) and
  feeds the CONFIRMED accel accumulator (car+0xAC) -- a lateral wall-
  proximity asymmetry drains speed, not just full off-track. Read the
  real byte values of both lookup tables: `D_80010128` (the 6-point car
  hull, ~98 units long / ~44-50 wide) and `D_8007306E` (the 4 wall-probe
  offsets: front-left/right, rear-left/right). PORTED the result: new
  `physics_wall_probe_lateral_gradient()` + a `wall_lateral_gradient`
  parameter on `physics_car_integrate` apply a continuous "grazing the
  wall" decel using the confirmed probe geometry and combination shape,
  layered on top of (not replacing) round 15's binary off-track penalty
  -- the response SCALE constant is still a tuning knob (car+0xA4's real
  value wasn't extracted), but the geometry and combination are real. 3
  new unit tests (21 total physics_sanity checks). `D_801733A0` is
  further narrowed but still not conclusively explained (see physics.c).
  `func_8001B374` (car-vs-car) still needs a multi-car architecture --
  that's a design decision, not something more tracing resolves.

- Round 18 ("enchaîne"): one RESOLVED (with a genuinely surprising
  answer), one confirmed DEAD END. Traced the single call site of
  `func_80017DF4` inside `FUN_8001c490` (of 7 total call sites codebase-
  wide, the other 6 are unrelated functions) to finally answer round
  13's open question: what does `FUN_8001c490` do with the steering-
  reference output? It does NOT add it to the car's heading at all --
  the value is read straight back off the stack and fed DIRECTLY as the
  probe-radius argument to the very next call in the same block,
  `func_8001BD9C` (the CONFIRMED wall probe). So `physics_steering_
  reference_raw` isn't a turn-rate input in the original -- it's an
  anticipated-curvature term that scales how far/wide the wall probe
  reaches, which then feeds `func_80033584`'s wall response (round 17).
  Separately, attempted to identify the boost-mechanic's trigger button:
  traced `func_80019CF4`'s button-check logic fully (it ANDs the current
  pad state against one of two mask pairs, `D_801D777C/777E` or
  `D_801D778C/778E`, chosen by a per-car controller-slot byte), but
  grepping the WHOLE codebase for those 4 globals found only reads (8
  total, all in this one function) -- no write site anywhere in the
  matched code. They're uninitialized BSS values, populated (if at all)
  by something outside this executable's own code -- not resolvable via
  further static disassembly. Documentation-only round (see physics.h/
  physics.c for the full writeup); tests unchanged and still green.

- Round 19 ("extrais les constantes de func_80033584"): re-read
  `func_80033584` instruction-by-instruction to pin down every constant
  precisely, and CORRECTED two mistakes in round 17's summary in the
  process. Precise extraction: `car[0x28] -= gradient_lat*4` every call
  (unconditional); the accel-accumulator nudge (`car[0xAC] += (gradient_
  lat * car[0xA4]) >> 6`) only fires when `gradient_lat >= 5` -- a
  SIGNED threshold, so a strongly negative lateral gradient never
  triggers it, a real one-sided asymmetry round 17 missed entirely; all
  4 probes get summed (not differenced) and scaled by exactly 0.3, added
  to BOTH `car[0x14]` and `car[0x40]`; the longitudinal gradient drives
  `D_8012CF70` (a per-frame, non-accumulated delta -- overwritten each
  call, not added to) AND a running total in `car[0x20]`; and --
  correcting round 17's biggest error -- `car[0x14]` ALSO gets
  `|cos_lookup(car[0x28]) >> 6|` added, where the cos() argument is
  `car[0x28]` ITSELF (the lateral-gradient-driven accumulator from the
  first line), not the car's heading as round 17 claimed. That makes
  `car[0x28]` an OSCILLATOR PHASE -- the real wall-scrape response
  includes a genuine judder/vibration term whose frequency scales with
  scrape intensity, not just a linear decel. Four distinct accumulator
  fields identified (0x14, 0x20, 0x28, 0x40), not the "one field plus a
  rotation" round 17 implied. Still unresolved: what any of those 4
  fields drive downstream (their READ sites weren't searched this
  round, only their writes here), and `car[0xA4]`'s real meaning. Not
  ported -- retrofitting just the sign/threshold onto
  `physics_wall_probe_lateral_gradient` would be cherry-picking one
  detail from a formula this port's simplified excess-based quantity
  doesn't actually share the rest of the shape with. Documentation-only,
  no functional code changed; tests unchanged and still green.

- Round 20 ("envoie du lourd bogoss"): re-verified round 19's
  `func_80033584` breakdown byte-by-byte against the raw asm (it holds
  up, with one refinement: the "*0.3" term is an actual MIPS `div` by
  100, not a multiply) and confirmed `func_80044D0C` really is a Q12
  trig lookup (mod-4096 angle wrap, octant split into signed 16-bit
  tables). Bigger find: `func_80033584` is never called alone -- its one
  call site always calls `func_80033438` on the SAME car pointer
  immediately before it, and that sibling function ALSO writes
  `car+0x20`, via a shared (non-per-car) global ramp `D_8012CF80` gated
  by an arm-delay counter, a 30-tick warmup, and the CONFIRMED button-
  flag fields `car+0xC8`/`car+0xCA` plus the CONFIRMED `car+0xA0` ramp-
  timer: ramps up to a cap of 16 during warmup, up to a cap of 8 after
  warmup while one input is held, down to a floor of -16 while another
  is held, and decays 25%/call otherwise -- a classic press-and-hold
  ease-in/ease-out ramp. Since `car+0x20` receives both this ramp AND
  `func_80033584`'s own wall-scrape longitudinal total, the leading
  hypothesis is a body-lean/weight-transfer accumulator (visual tilt or
  a secondary grip modifier), fed by two unrelated causes. Documentation-
  only, no functional code changed; tests unchanged and still green.

- Round 21 ("trouve où car+0x20 est lu"): found it. First pinned down
  the exact player-car global (`D_8007C258`, a single fixed address --
  traced back through `func_8001C490`'s callers to `func_80014C2C`, the
  top-level per-frame drive dispatcher) so the search could target real
  call sites instead of a noisy blind grep for "offset 0x20" (offset
  0x20 turns out to be common to dozens of unrelated structs). Found
  `func_8002AE14`, called from a ~30-second-cadence gated state machine
  (`func_8002B024`), which snapshots car+0x20 (as a 16-bit read) into a
  40-byte record in a persisted-looking buffer (`D_8007C4F8`, sitting
  right after what looks like a saved best-time header) -- ALONGSIDE
  car+0x8 (position-like), car+0x10/car+0x14/car+0x18 (position/
  velocity-like), car+0x38 (near-certainly heading), and its own sibling
  accumulators car+0x24/car+0x28/car+0x40, plus the same 8 fields from a
  second, not-yet-identified struct (`D_801E9250`). This is the
  signature of a ghost/record-replay snapshot system. Being captured
  alongside position/velocity/heading rather than staying purely
  internal to its two writer functions is real supporting evidence for
  the body-lean hypothesis (round 20) -- a replay missing it would
  visibly lack body roll. Also CORRECTED a real error standing since
  round 19: physics.c's TODO claimed these offsets were relative to
  `car+0x58`; they're relative to the RAW car pointer (car+0x0) --
  confirmed both by `func_8001C490`'s own prologue and by `D_8007C258`
  itself being addressed directly with a `+0x20` offset. Documentation-
  only, no functional code changed; tests unchanged and still green.

- Round 22 ("enchaîne"): found the playback side. `func_8002AF1C` is the
  exact byte-for-byte inverse of round 21's `func_8002AE14` -- given an
  index and two destination pointers, it unpacks a `D_8007C4F8` record
  back into them at the same offsets (+0x8/+0x10/+0x14/+0x18/+0x20/+0x24/
  +0x28/+0x38/+0x40). All 3 call sites found pass `a1 = D_8007C258` --
  it writes DIRECTLY into the LIVE player car struct, not a copy. One
  call site uses a hardcoded slot 0 and, in the same function, resets
  `D_801D9060` (the CONFIRMED round-20 arm-delay gate) to 3 -- the
  signature of a race-(re)start handler: restoring car state from a
  saved baseline slot, including car+0x20/0x24/0x28/0x40, so a fresh
  start doesn't carry over leftover wall-scrape lean. The other 2 call
  sites use a variable index (`D_80173310`, a counter also seen gating
  a long chain of countdown/sequence comparisons), consistent with (not
  yet certain to be) a frame-by-frame ghost/intro playback. None of the
  3 caller functions has a direct `jal` call site anywhere -- almost
  certainly 3 of the 40 state-handler functions reached through the
  round-2 dispatch table (`D_80070EA4`). Bottom line: car+0x20/0x28/0x40
  are now CONFIRMED first-class car state -- explicitly saved and
  restored on equal footing with position/velocity/heading by the
  game's own restart/playback logic, not internal scratch -- even
  though the precise visual/mechanical meaning (still: body lean) isn't
  proven. Documentation-only, no functional code changed; tests
  unchanged and still green.

- Round 23 ("envoie du lourd"): identified `car+0xA4`, the one field in
  the whole cluster still completely unknown. `func_80026CA8` runs a
  self-updating (magnitude, phase) OSCILLATOR using car+0xA0 (the
  CONFIRMED ramp/frame-timer) and car+0xA4 together with two independent
  Q12 trig lookups of car+0xA8 and car+0x24 (confirmed `func_80044E2C`
  is a genuine second trig table alongside the round-20-confirmed
  `func_80044D0C` -- a real sin/cos pair, though which is which isn't
  settled, meaning the earlier "cos_lookup" label may be backwards),
  writing new values back into car+0xA0/car+0xA8 every call. The other
  phase, car+0x24, is advanced separately (`func_80026E7C`) proportional
  to the CONFIRMED accel accumulator (car+0xAC) -- spins faster under
  acceleration. `func_80026E7C` is called from inside `func_80019CF4`
  (the SAME function round 18 traced for the boost-button dead end),
  from one arm of a 4-way dispatch on car+0x5C (a previously
  uncharacterized per-car mode/sub-state field). Best-supported reading:
  this whole car+0x24/0xA0/0xA4/0xA8 cluster is a WHEEL-SPIN or
  SUSPENSION-BOUNCE visual system, with car+0xA4 acting as a per-car
  RESPONSIVENESS/DAMPING constant -- which also explains why
  `func_80033584` reuses the exact same field as its own wall-scrape
  accel-nudge scale (round 19/20): both consumers share one "how
  strongly this car reacts" tuning constant, applied to different
  triggers. car+0xA4's own write/init site (fixed per-car-model constant
  vs. dynamic) wasn't found. Documentation-only, no functional code
  changed; tests unchanged and still green.

- Round 24 ("next bogoss"): identified `D_801E9250` -- the "second
  struct" round 21/22 found saved/restored alongside the player car but
  couldn't place. It's the base of `func_8001B374`'s (car-vs-car
  collision, round 16) own response array: confirmed directly in the asm
  (two loop pointers each advancing by `0x114` per iteration, gated by
  `slti v0,v0,0xC` -- a 12-slot array, stride 0x114, exactly matching
  round 16's original description). The specific fields func_8001B374
  writes for slot 0 (`D_801E9294`/`+0x44` through `D_801E9310`/`+0xC0`,
  all relative to `D_801E9250`) sit past the `+0x8..+0x40` range round
  21/22's ghost snapshot uses for this same struct -- so slot 0 has both
  a "car-shaped" prefix (matching the player's own layout, used by the
  ghost system) and a separate collision-response region beyond it,
  consistent with `D_801E9250` being an array of full per-opponent car
  structs (0x114 bytes fits, given confirmed fields already reach
  +0xCA/+0xAC/+0xB4). STILL NOT found: the actual per-opponent consumer
  that reads this response region back out -- grepping for a
  `car+0x44`-equivalent read relative to the player's own `D_8007C258`
  found nothing, so the player doesn't consume it the same way; a real
  per-opponent physics update analogous to func_8001C490/func_80033584
  isn't confirmed to exist in this decompiled code, and building a
  multi-car simulation to use this remains an architecture decision, not
  something more tracing resolves. Documentation-only, no functional
  code changed; tests unchanged and still green.

- Round 25 ("let's go! finish this!"): searched harder for car+0xA4's
  write/init site -- inconclusive, but ruled out 2 false leads worth
  recording so a future round doesn't repeat them. A grep for `0xA4(`
  stores found 3 candidates, none of which turned out to be the real
  car+0xA4: `func_80020B88` copies a per-car-model config block into
  `car+0x58+0xA0..+0xB8` (confirmed via re-deriving the base pointer --
  it's `car+0x58`-relative, not raw car), a real find on its own (a
  genuine per-car-model tuning-data loader) but not the target field;
  `func_800229F4`/`func_80022A58` read/write a completely different
  `+0xA4` field, 16-bit and belonging to `D_801E9250` itself (confirmed
  via `func_80025268`'s prologue, `$s2 = D_801E9250` directly) -- a
  sawtooth counter (-150 to +120) gated by nearby fields and a global,
  best guess a camera-shake oscillator, NOT the player's own car+0xA4.
  Grepping for the literal computed global address of the player's
  car+0xA4 found nothing (expected -- it's always reached via a passed
  register, never a fixed global), confirming the real search has to be
  register-relative tracing, a bigger job than a flat grep. One new
  READ site found in the process (`func_80022F88`, using car+0xA4 as a
  fallback in what looks like an AI speed/difficulty decision).
  Documentation-only, no functional code changed; tests unchanged and
  still green.

- Round 26 ("go on"): two real advances, found re-reading
  `func_80014C2C` (the top-level per-frame drive dispatcher) fresh.
  (1) FOUND the per-opponent update loop round 24 couldn't confirm
  existed: right before the ghost-recorder call, it loops all 12 slots
  of `D_801E9250`, and for each active slot (a flag at +0x0) whose +0x58
  field equals 1, calls `func_8002128C(slot_ptr, 0)` -- a large
  (367-instruction) per-slot update that repeatedly calls
  `func_8003486C`, one of the state-handler-cluster functions from
  rounds 20-23's car+0x5C investigation. Not traced byte-by-byte (a
  multi-round job on its own) and not confirmed to reach
  `func_8001B374`'s response fields specifically, but it firmly
  establishes a per-opponent update DOES exist -- the natural next place
  to look for that consumer. (2) CORRECTED round 21: `func_8002AE14`'s
  recorder isn't a periodic ~30s snapshot, it's a CONTINUOUS per-frame
  recording session while armed, triggered by passing track checkpoints
  (a small table at `D_801D7760` indexed by `D_80173470`), running for
  up to 1800 frames (~30s @ 60fps) with an ever-incrementing record
  index (`D_801D77F0`) or until a checkpoint-specific duration expires.
  `D_8007C4F8` is a genuine multi-frame clip buffer, not a handful of
  snapshots -- strong reinforcement of the ghost/replay reading. Also
  confirmed `D_80173310` (open since round 22) IS a real general-purpose
  incrementing frame counter (`+= 1` every frame in `func_80014C2C`,
  reset to 0 in a few places and to -1 in the round-22 race-restart
  handler) -- used by many unrelated countdown checks too, not a
  dedicated ghost-playback index, but its semantics support round 22's
  frame-by-frame playback reading for the 2 variable-index
  `func_8002AF1C` call sites. Documentation-only, no functional code
  changed; tests unchanged and still green.

- Round 27 ("tu peux continuer"): fully traced `func_8002128C` to answer
  round 26's open question -- it does NOT reach `func_8001B374`'s
  collision-response fields (+0x44..+0xC0); every field it touches on
  its slot pointer falls inside the +0x8..+0x40 car-shaped-prefix range.
  It's actually an opponent-car 3D POSITIONAL AUDIO update: distance-LOD
  gated (via `D_801D9068`/`D_801D9070`, likely the listener/player
  position, thresholds 0xD00/0x2500), driving `func_800129AC` (pan/
  attenuation) and `func_8003486C` calls, with an SPU voice acquire
  (`func_80012EF0`/`func_80012FE4`) / commit (`func_8001315C`) pair --
  not an AI/collision update as round 26 guessed. This closes out
  `func_8002128C` but reopens the question of who actually consumes
  `func_8001B374`'s response fields, now with one major false lead
  eliminated. Documentation-only, no functional code changed; tests
  unchanged and still green.

- Round 28 ("enchaîne"): FOUND a real consumer, via a targeted xref
  search on all 9 confirmed response-field symbols (round 27's own
  recommendation) instead of guessing near an update loop. `D_801E92D8`
  (+0x88) has xrefs outside `func_8001B374`, in `func_8002252C`, which
  compares one opponent's own +0x88 against a SPECIFIC OTHER opponent's
  +0x88 (via a computed index into the 12-slot array) to drive what
  looks like an overtake/block/avoid decision. `func_8002252C`'s caller,
  `func_80025268`, turns out to be the REAL master per-opponent AI/
  behavior function -- called once per frame from `func_80014C2C`
  (immediately before, not as part of, round 26/27's audio loop), with
  its own internal 12-slot loop dispatching to a cluster of ~11 decision
  subroutines. This resolves the multi-round search for at least one
  response-field consumer; the other 8 fields (+0x44/+0x48/+0x4C/+0x50/
  +0x54/+0x64/+0x6C/+0xC0) had no xrefs outside func_8001B374 in this
  pass, so their consumers are still unfound but now have a well-founded
  place to keep looking (the same `func_80025268` dispatch cluster).
  Documentation-only, no functional code changed; tests unchanged and
  still green.

- Round 29 ("tu continues ?"): checked `func_80025268`'s own body and
  all ~11 per-slot subroutines for direct-offset accesses to the other 8
  unconfirmed collision-response fields -- none found. 2 apparent hits
  (`func_80024F54`'s `0x44($a1)`, `func_80023B2C`'s `0x6C($s1)`) turned
  out to be false leads: both registers are `a0+0x58` (the confirmed
  sub-struct-pointer offset), so they resolve to slot+0x9C and slot+0xC4
  respectively, not the target slot+0x44/+0x6C. Real finding: the a0+0x58
  sub-struct is a bigger AI-decision-state block than previously mapped
  (now known fields at slot+0x9A/0x9C/0xA2/0xA6/0xAE/0xB0/0xC4). The
  other 8 response fields' real consumer(s) still unfound after 2
  dedicated rounds -- next step is a full byte-by-byte trace of each
  subroutine (this round only checked for raw immediate-offset patterns,
  not computed/indirect accesses like round 28's own D_801E92D8-as-
  array-base trick). Documentation-only, no functional code changed;
  tests unchanged and still green.

- Round 30 ("OK tu peux continuer"): fully traced 4 of func_80025268's
  smaller dispatch subroutines (`func_80020524`, `func_80022984`,
  `func_80021BE0`, `func_80021CB4`) for computed/indirect accesses to
  the remaining 8 collision-response fields -- none found, but a real
  nuance surfaced: `func_80021BE0`/`func_80021CB4` are AUDIO priority-
  scoring helpers (computing a distance-based score into `D_80173348`,
  likely feeding an SPU voice-allocation decision, same family as round
  27's `func_8002128C`), not AI/collision logic. So `func_80025268`'s
  per-slot dispatch cluster is a MIXED per-opponent update (AI + audio),
  not purely AI as rounds 26/28 assumed. Only 4 larger subroutines
  (`func_800217F4`, `func_80024C64`, `func_8002362C`, `func_80025050`)
  remain untraced. Documentation-only, no functional code changed; tests
  unchanged and still green.

- Round 31 ("enchaîne"): finished the sweep -- traced the last 4
  subroutines (`func_800217F4`, `func_80024C64`, `func_8002362C`,
  `func_80025050`) for direct, indirect/computed, and even bare-constant
  occurrences of the 8 remaining offsets: zero hits across all 4. Re-ran
  a whole-file xref search on all 8 field symbols one more time to be
  sure: every occurrence is still inside `func_8001B374` itself, zero
  references anywhere else in the codebase. CONCLUSION: `func_8001B374`
  writes these 8 fields but nothing in the decompiled codebase reads
  them back -- most likely dead/unused code, or consumed through a path
  invisible to static tracing, or by something entirely outside the
  AI/audio cluster (e.g. rendering) that 4 rounds of AI-focused search
  never looked at. Deprioritizing this specific thread after 4 dedicated
  rounds (28-31) -- `D_801E92D8`'s own consumer (round 28) is enough to
  understand the shape of the collision-response mechanism for porting
  purposes. Documentation-only, no functional code changed; tests
  unchanged and still green.

- Round 32 ("enchaîne"): closed the `D_801E9250`+0xA4 sawtooth thread
  (round 25's "camera-shake?" lead). Fully traced its 2 only touchpoints:
  `func_800229F4` (the -150..120 asymmetric-sawtooth counter itself --
  its apparent "init" RNG call, `func_8003A958(8)`, turns out to be a
  discarded-return side-effect draw, not a real initializer) and
  `func_80022A58` (its only caller, the gate/controller: advances the
  counter unconditionally while negative, and once non-negative only
  while AI-state fields +0x96==1 AND +0xA2==1 hold AND the confirmed
  collision-response field +0x88/`D_801E92D8` compares favorably against
  a global-derived threshold; also a hard reset to -30 under one more
  condition). New this round: `func_80022A58`'s own only caller is INLINE
  code inside `func_80025268` (missed by rounds 28-31's subroutine-list
  sweep because it's not one of the ~11 named dispatch subroutines),
  gated by `D_801D9060<4` (the arm-delay flag already known from round
  25). CONCLUSION: fully closed loop, no other reader/writer anywhere in
  the codebase -- REVISES round 25's camera-shake guess to an AI-state
  pacing/cooldown counter for the overtake/block behavior (same
  `D_801E92D8` field round 28 tied to that decision), since it's gated by
  the same AI-state and collision fields rather than anything visual.
  Adequately understood; not worth further rounds. Documentation-only, no
  functional code changed; tests unchanged and still green.

- Round 33 ("on enchaine"): widened the search for car+0xA4's own
  write/init site (the OTHER +0xA4, distinct from round 32's now-closed
  `D_801E9250` sawtooth). 3 angles, all negative: (1) grepped
  `func_8001C490` (the confirmed player-only, once-per-frame drive
  dispatcher, called 6x always with `D_8007C258`) and its child
  `func_80019CF4` directly for `0xA4(` -- zero. (2) Checked all 18
  places in the whole file that reference the `D_8007C258` global
  directly, including 8 not previously read (`func_8001B0CC`,
  `func_8001F23C`, and the round-22/26 ghost/restart cluster
  `func_8002B024`/`8002B370`/`8002B4A8`/`8002B6F4`/`8002BBA0`/`8002C500`/
  `8002CE0C`/`80039874`) -- zero hits, so the restart code that DOES
  touch car+0x20/0x24/0x28/0x40 does not also touch car+0xA4.
  (3) Searched file-wide for the indirect `addiu $reg, $baseReg, 0xA4`
  pattern (computing a car+0xA4 address into a register) -- found 5
  hits, but all 5 are a false lead in an unrelated menu/garage-list
  rendering cluster (`func_80034050`/`8003446C`/`80035638`/`80035EAC`/
  `80036D30`), not the live car struct. CONCLUSION: the cheap search
  avenues are now exhausted (mirrors the 8-collision-field thread's
  shape) -- only a full call-graph trace from `D_8007C258` down would
  settle it. Documentation-only, no functional code changed; tests
  unchanged and still green.

- Round 34 ("hop hop hop"): identified round 23's 3 oscillator helpers
  and traced car+0x5C's states 2/3. `func_80019CA8` = signed BAM12
  shortest angle-difference (full trace). `func_80040B54` = integer
  square root (normalize/lookup/denormalize shape via `func_80044078` +
  `func_800409D4`). `func_800187A0` = atan2-style arctangent (LUT at
  `D_8005E93C`, full quadrant argument-reduction). Together: car+0xA0/
  +0xA8 is confirmed as a genuine Cartesian-to-polar (magnitude/phase)
  recompute each call. States 2/3 (`func_80027F60`, `func_80028294`)
  both run the SAME oscillator tick as state 0, but additionally feed
  `func_80044D0C/E2C(car+0xA8[+D_801D7E30 for state 2]) * car+0xA0` into
  car+0x60 -- THE CONFIRMED VELOCITY FIELD -- a new physics link
  supporting a wheelspin/traction-loss reading for states 2/3.
  Documentation-only, no functional code changed; tests unchanged and
  still green.

- Round 35 ("on enchaine"): found car+0x5C's RESET site. Grepped `0x5C(`
  stores on car-pointer registers; 3 apparent hits (`func_80026E7C`,
  `func_80027734`, `func_80027F60`) all resolved to the a0+0x58
  sub-struct (car+0xB4, a different field) once the base register was
  checked -- false leads, documented precisely. The real hit:
  `func_800205E4(a0, a1)` zeroes car+0x5C directly on the raw pointer,
  as the last of a reset cascade (+0x38..+0x58). Called from
  `func_800206CC` -- a genuine per-car spawn/init function (+0x4=active
  flag, +0x2=model id from a table) -- itself called from 3 sites inside
  `func_80021048`'s per-slot loop, shaped like race/grid setup.
  CONCLUSION: car+0x5C resets to 0 (state 0) at spawn time; this is NOT
  the same as finding what advances it to 1/2/3 mid-race, which is still
  unfound. Documentation-only, no functional code changed; tests
  unchanged and still green.

- Round 36 ("on enchaine"): two threads. (1) Closed the direct-offset
  search for car+0x5C's mid-race trigger: confirmed only 6 total
  `sw ..., 0x5C(` instructions exist file-wide; checked the 2 not
  already resolved (`func_8001AA60` -- another car+0xB4 sub-struct false
  lead; `func_80038018` -- an unrelated tagged-field stream decoder, not
  the car struct). None set car+0x5C nonzero anywhere. Deprioritized
  alongside the 8-field/car+0xA4 threads. (2) Resolved `D_801733A0`
  (stale since ~round 13): only 6 refs file-wide. Write site
  (`func_80015CD4`) sets it to a fixed `0xF000`, called once from
  `func_80032A54` -- a race/level-load setup function (also inits
  `D_80173318`, round 27's confirmed SPU-voice-count global). The 5
  reads use it purely as a max-range bound in Q14 fixed-point
  expressions over track-geometry-shaped data. CONCLUSION: a one-time
  audio/rendering-range constant, NOT a physics value -- ruled out of
  scope rather than left open. Documentation-only, no functional code
  changed; tests unchanged and still green.

- Round 37 ("enchaîne"): closed the "aux_a_raw checkpoint path" thread,
  open since round 13. Identified `func_800382A0` -- called once/frame,
  unconditionally, from `func_80014C2C` (the confirmed top-level
  dispatcher), right after the opponent AI/audio loop. Full trace
  (~480 instructions): computes sqrt-distance from the player position
  to a singleton "next checkpoint" record (`D_801D80A8`, fields at
  +0x14/+0x18/+0x1C), fires the approach sound (`func_80032F50(5,...)`
  -- the same call round 13 flagged) once per approach via a latch flag,
  updates an on-screen checkpoint arrow/blip, and LERPs the marker's own
  position+heading toward the next checkpoint over a duration field.
  Also corrects round 36's `func_80038018` classification: it's not
  "unrelated" so much as part of THIS checkpoint record's own per-frame
  update (still confirmed unrelated to car physics). Along the way,
  resolved the long-open "record offset 0x12" question from round 21's
  ghost-record layout: it's a structural alias of `D_801E9250+0x14`
  (same field as `car+0x14`), not a distinct unknown field. Also
  confirms round 36's `D_801733A0` resolution covers the specific
  `func_800178A0` reference this file already tracked separately (see
  below) -- same constant, same conclusion. Documentation-only, no
  functional code changed; tests unchanged and still green.

- Round 38 ("on fini ça ? je veux voir l'écran titre"): TITLE SCREEN
  reconstructed from real assets. Dumped and inspected every page of
  all 5 TEX banks (contact sheets): TEX0 = in-race HUD/liveries/the
  Namco race queen, TEX1/TEX2 = per-track scenery (TEX1 p112 = the
  small 96x64 in-game billboard logo), TEX3 = the MENU bank (car
  select, course select, AT/MT, records...), TEX4 = the boot sequence
  ("NOW LOADING!", the Galaxian mini-game sprites, namco(r)). The real
  title assets are in TEX3: p36 = the 240x96 "RIDGE RACER (tm)" logo
  (red speed-lined letters over green scribble + checkered flag),
  p90 = "START BUTTON", p40 = "namcot(r)". New tool
  `tools/titlescreen/title_main.c` (target `rr_title_tool`) composites
  them into a 320x240 PPM from the user's own local TEX3.TMS (assets
  never committed). Layout is a labeled RECONSTRUCTION (centered logo /
  prompt / namcot); tracing the original title-state's exact draw
  coordinates in the decomp is a future RE task. Output verified
  visually and synced to the user's machine. Also confirmed along the
  way: the whole data track is only ~3.1MB (1559 sectors) -- everything
  loads to RAM at boot, no hidden streamed data beyond the root files,
  and PSX.EXE contains no standard TIM images.

- Round 39 ("on continue ! lache pas le morceau"): TRACED the original
  title-logo renderer, and it's better than a static blit -- the logo
  CLUT id 0x7E8C (unique in the whole codebase) leads to
  `func_800266B8`, which builds a 28x20 grid (560 quads, 0x34-byte
  prims, double-buffered at D_8012E4C0/D_801510B4) all textured from
  the logo's VRAM page (704,256) via `func_8004788C(0,0,0x2C0,0x100)`
  (GetTPage-equivalent). `func_80026794` = init (zeroes frame counter
  D_801D77A8, builds both buffers). `func_800267E4` = the per-frame
  animator: rebuilds a 112-entry (x,y) wave table in the PS1
  scratchpad from cos/sin (func_80044D0C/func_80044E2C, the confirmed
  BAM12 trig pair) of a frame-advancing traveling phase (amplitude
  ~0x90, breathing via cos(frame*4), y bias +0x80), then deforms the
  quad mesh through it. CONFIRMED: the title logo is a WAVING-FLAG
  MESH ANIMATION. `rr_title_tool` gained a `--wave N` mode reproducing
  it (per-column traveling sine, breathing amplitude, blinking START
  BUTTON) -- structure traced, coefficients approximated (byte-matching
  the scratchpad math is the remaining step). Animated GIF rendered
  and synced to the user's machine (`title_screen_animated.gif`).
  Build clean, 3/3 tests green.

- Round 40 ("Maximum effort dessus"): THE AUTHENTIC PHYSICS CORE IS
  PORTED. `func_8001C490` (the per-frame player function) traced
  instruction-by-instruction END TO END, plus `func_8001C0E4` (=
  steering, not engine), `func_80026E7C` (= the grip/drift handler)
  and `func_80019CF4`'s engine block. Three long-standing
  misreadings corrected: car+0x24 = VELOCITY DIRECTION chasing the
  heading (car+0xAC) with a /5 filter -- the lag IS the drift model;
  car+0x38 = wheel rotation (not heading), blur flag at speed>=0x321;
  car+0xA0/+0xA8 = the velocity vector in polar form (speed/angle),
  not a suspension oscillator. BONUS: car+0xA4's write site FOUND
  (closes the rounds-25/33 deprioritized thread) -- `0x4C($s1)` with
  $s1=car+0x58, the substruct trap in reverse; it's recomputed every
  frame from engine force. New files `src/physics_psx.{c,h}`: integer
  BAM12 core implementing the full confirmed cycle (gearbox
  thresholds, steering ramps +-0x500 with /2 decay and speed/0x320
  low-speed scale, engine shape (0x2710-rpm)*base/0xC350 with slope
  feed +0x2C>>8, coast *996/1000, brake *94/100, velocity projection
  grip with slip extraction, wheel rotation, pos += vel>>8 predict-
  probe-commit with wall veto + *0.7/*0.8 speed cuts, gravity
  vy+=0xC / y+=vy>>3, spin-out trigger) + the traced attract-mode
  autopilot steering law. Only approximated numbers: per-gear engine
  curve + rpm rates (per-model tables not extracted) and the
  autopilot lat-term units. New test `physics_psx_authentic` (4th
  ctest target): autopilot laps an oval, gearbox 1->6, bounded speed,
  curve-slip > straight-slip (drift proven), wall blocks + cuts
  speed. 4/4 tests green, 0 warnings.

- Round 41 ("envoie moi tout ça à 100%"): THE REAL ENGINE DATA IS IN.
  Found the per-gear engine-table BUILDER in `func_8001AA60` (the
  spawn/init function): D_801D7EC8 (20 bytes/gear) is built from three
  STATIC tables inside PSX.EXE -- D_80073050/D_80073034/D_80073018 =
  torque at rpm 0 / 0x1388 / 0x2710 per gear -- scaled by the car
  model's stat1/100, with entry[0] = drivetrain ratio (rpm*ratio -
  wheel_speed(+0xB0) = transmission slip, accumulated in D_8012CF78).
  Extracted the real values from the user's own PSX.EXE: the 3 gear
  tables AND the 13-model stats table D_800593B0 (model 12 = the
  secret #13 car: 1180/1200/1500/1500/274). Also confirmed from the
  same init: spin threshold car+0x94 = stat2*14, car+0x98 = stat3, and
  the REAL starting-grid pose (0x6935, 0x60, 0xA253, track_pos
  0x4400). Engine force = piecewise-linear torque blend over rpm
  segments [0,0x1388]/[0x1388,0x2710] normalized /0xC350 (exact asm
  sequence). physics_psx.c now runs the REAL curve -- round 40's
  invented per-gear caps are deleted (the real torque-vs-drag
  equilibrium self-limits speed). `psx_car_set_model(0..12)` selects
  any of the 13 real cars. Remaining approximations: rpm dynamics
  (engine-to-wheel coupling not line-matched) and the autopilot
  lat-term units (test uses a harness-side PD driver instead; the
  traced law stays in the lib, documented). 4/4 tests green, 0
  warnings, trajectory re-rendered with the real tables.

- Round 42 ("je veux un port natif pc de ridge racer"): THE AUTHENTIC
  PHYSICS LAPS THE REAL COURSE. New `src/psx_drive_demo.c` (target
  `rr_psx_drive`, 5th ctest -- SKIPs without RR_EXE_FILE): bridges
  PsxTrackIface onto the REAL course-A section table (256 sections
  from the user's PSX.EXE via tools/trackdata), with road direction
  and lateral offset computed from section geometry (heading_raw's
  orientation convention on real data disagrees with the
  synthetic-test reading per-section -- geometry wins, byte-matching
  the convention is a future task), real asymmetric track widths as
  walls, a curvature-lookahead braking driver, and a boundary
  resolver (harness stand-in for the still-untraced func_800181C8)
  that projects the car back to the track edge instead of letting
  integer rounding wedge it inside a wall. Two real core fixes came
  out of this integration: (1) wall hits now cut the VELOCITY VECTOR
  along with the speed scalar (this port's projection reads the
  vector; without the cut, the polar rebuild resurrected the old
  speed), (2) an axis-separated slide retry on wall hits (documented
  approximation of 181C8's grinding). RESULT: 4 complete laps of the
  real Ridge Racer circuit in 16000 frames (~2:13/lap at 30fps), 0
  off-track frames, gearbox 1->6, top speed 0x743. 5/5 tests green.
  The trajectory render over the real course outline is
  `rr_real_course_laps.png`.

- Round 43 ("faut tout brancher"): EVERYTHING IS WIRED -- THE PORT IS
  PLAYABLE. (1) New `src/psx_track_bridge.{c,h}`: the round-42 bridge
  (geometry-based road dir/lateral, deeper-only wall test, boundary
  resolver, curvature brake helper) extracted into a shared unit --
  the headless lap test and the interactive mode now drive through
  the SAME code. (2) `main.c`: the C-key driving mode now uses the
  AUTHENTIC fixed-point core (physics_psx.c) whenever --physicsdata
  loaded the real course -- digital pad input like the original
  (up/down/left/right, shift for manual gears), fixed 30Hz steps
  decoupled from render rate, camera follow with BAM<->radians
  conversion, live HUD (gear/speed hex/rpm/section/slip/wheel-blur/
  spin flags); the float model stays as fallback without course
  data. (3) New `--selfdrive N <prefix>` headless capture mode: the
  authentic core + shared driver runs while draw_track_drive_scene
  renders, dumping PPM frames -- produced `rr_drive_view.gif` (the
  3D drive view is still flat-shaded/single-texture-page, the known
  Phase-5 simplification; making it pretty is rendering work, not
  physics). Also fixed an arg-scan bug (--physicsdata's break
  skipped later flags). 5/5 tests green. TO DRIVE: build with SDL2,
  run `rr_pc_port --track MAP.RRM IDX.HED [TEX] --physicsdata
  PSX.EXE`, press V (drive view) then C (authentic mode), arrows to
  drive.

- Round 44 ("le vrai rendu"): first real-rendering pass on the drive
  view. (1) REAL HEIGHTS: MAP.RRM type-B records carry per-corner
  heights (v[1], PS1 y-down, -13064..+2594 across the shipped course
  -- the tunnel and hills), previously ignored by the flat-plane
  camera; the projection now uses them, with a camera ground sampler
  that scores planar distance + a height-continuity penalty (so the
  camera stays glued to ITS deck where the circuit crosses over
  itself) and a smoothing filter. (2) SKY: 12-band vertical gradient
  above the horizon + neutral ground fill. (3) MATERIALS: the record
  group_id (30 distinct values in the course -- the stepped
  "material id" candidate) hashes to a stable asphalt-family tint per
  material, so different surfaces read as different. Also brightened
  fog (floor 110, range 9000). Statistical recon of the undecoded
  record bytes (24-39): group_id as signed has a small value set;
  flags = two small bytes (1-4, 1-3) -- candidates for texture-page /
  tessellation info. HONEST STATE: structurally correct relief +
  sky + material variation, still no true per-quad textures -- that
  needs the PS1 track-render-loop trace (the consumer of the
  D_801D35F0 runtime table; func_800355A4 is just its scratchpad
  setup, the actual renderer is the next RE target). 5/5 tests
  green; capture: rr_drive_relief.gif.
- IDEA NOTED (user, round 44): the ARCADE Ridge Racer (Namco System
  22, 1993) as a REFERENCE -- same course, higher-quality source
  textures, useful for identifying what each zone should look like,
  cross-checking landmarks, and later as an upscale guide. Different
  hardware/codebase, so reference only -- no arcade assets in the
  repo, ever.

- Round 45 ("la suite"): THE REAL TEXTURES ARE ON SCREEN -- MAP.RRM's
  16 mystery bytes DECODED. Traced the PS1 track renderer: the
  per-frame track function is `func_800163E4` (0x13D4 bytes, 64
  scratchpad refs, fed by `func_800355A4`'s setup), and its quad
  emitter `func_8003486C` copies record bytes 24-39 VERBATIM into
  POLY_FT4 packets: u0v0 (24-25), CLUT id (26-27 -- the field Phase 5
  called "heading"), u1v1 (28-29), TPAGE id (30-31), u2v2 (32-33),
  OT depth bias (34-35 -- the field called "group_id"), u3v3 (36-37);
  38-39 still open. Validated on the real file: all tpages = valid
  4bpp VRAM pages where the TEX banks load, all CLUTs at y=480..509,
  UVs form clean rects. IMPLEMENTED: `tools/texparse/psx_vram.{h,c}`
  recreates the PS1's 1024x512 VRAM by blitting all TEX*.TMS pages +
  CLUTs at their declared destinations (629 pages), with a PS1-exact
  4bpp sampler and a per-(tpage,clut) baked-page cache; new `--texdir
  <dir>` flag; draw_track_drive_scene uses each record's OWN texture
  reference (corner order v0,v1,v3,v2 <-> uv0,uv1,uv3,uv2). RESULT:
  the red bridge girders, chevron barriers, cliff faces, canopy --
  the real game art, mapped by the game's own data. Still rough:
  camera/deck relationship needs tuning, no OBJ.RRO scenery, no
  sort-bias use yet. map_rrm.h updated with the confirmed decode.
  5/5 tests green; capture: rr_drive_textured.gif.

- Round 46 ("continue bogosss"): camera glued to the road deck +
  game-data depth layering. (1) Ground sampler v2: 2D point-in-quad
  (cross-product signs, both windings) over the road records finds the
  quad CONTAINING the camera and takes its mean corner height; where
  decks stack (the overpass) the candidate closest to last frame's
  ground wins; nearest-corner fallback off-road; 0.35 smoothing. The
  drive view now sits ON the road -- driving under the red bridge
  reads exactly like the game. (2) The painter sort now folds in each
  record's own ordering-table bias (bytes 34-35, decoded round 45),
  the game's own deck-layering mechanism. (3) Tried affine depth-
  clamping for quads crossing the near plane -- smears them across
  the screen; reverted to the cull, REAL polygon clipping documented
  as the correct future fix (the near-foreground hole remains until
  then). 5/5 tests green; capture: rr_drive_r46.gif.

- Round 47 ("t'arettes pas !"): REAL near-plane polygon clipping.
  Sutherland-Hodgman against depth==near_plane in camera space, with
  right/height/depth AND UV interpolation at the crossings; output
  (up to 5 vertices) drawn as a triangle fan through the existing
  quad rasterizer (duplicated last vertex = degenerate second
  triangle). Replaces round 44-46's any-vertex-behind cull -- the
  gray hole in the immediate foreground is gone; near road decks and
  walls now render right up to the camera. Remaining artifact:
  nearly-edge-on clipped slivers (e.g. bridge girders sweeping past
  the camera) can project as large columns -- side/top screen-edge
  clipping and/or frustum culling of edge-on quads is the follow-up.
  5/5 tests green; capture: rr_drive_r47.gif.

- Round 48 ("enchaine"): textured sky + chase-cam experiment.
  (1) SKY: the game's own 256x256 cloud panorama (TEX0 page 95, VRAM
  (320,256), tpage 21 / clut 0x7983 -- identified in the round-38
  bank survey) drawn as a screen-wide band above the horizon,
  horizontally scrolled by camera yaw (cylindrical panorama, one turn
  = one wrap); gradient fallback without --texdir. (2) Tried a chase
  camera 220 units behind the car (the game's drive view): it backs
  INTO trackside geometry in corners -- a proper chase cam needs its
  own collision probe (pull-in on obstruction); reverted to
  first-person, documented as future work alongside the visible-car
  OBJ.RRO milestone it pairs with. 5/5 tests green; capture:
  rr_drive_r48.gif.

- Round 49 ("enchaine"): OBJ.RRO recon -- a precise negative + the
  next target named. Tested the natural hypothesis that OBJ.RRO's
  40-byte sub-blocks share MAP.RRM's record layout (4 corners +
  UV/CLUT/TPAGE): REJECTED empirically (only 25/91 blocks decode to
  plausible tpage/clut values -- the object prim formats are their
  own thing). NEW STRUCTURAL FINDING: func_80034EFC (7 calls from the
  track renderer) has the EXACT same prologue as round 45's quad
  emitter func_8003486C -- both are emitters of a generic "mesh-set"
  system reading 16-byte directory entries with per-type prim counts;
  OBJ.RRO's directory (16 bytes: ptr + SIX counts x sizes
  40/48/32/64/72/56) is the same system's on-disk shape. NEXT SESSION
  TARGET (named): trace the builder that turns OBJ.RRO entries into
  these emitter tables + the 6 prim-type layouts (each ends in
  UV/clut/tpage words per the 40-byte type's partial signal), then
  the car models render through the SAME pipeline as the track.
  Also: the 100920 unaccounted trailing bytes = likely the shared
  vertex pool those prims index into (strengthened hypothesis).

- Round 50 ("let's go go go"): THE CARS ARE DECODED. Three linked
  finds: (1) func_80012670 re-read closely -- obj_rro.h's directory
  layout was off by 4 bytes: the REAL entry is 6 counts at +0x0..+0xA
  (for prim sizes 40/48/32/64/72/56) + the data pointer WRITTEN at
  +0xC. With that fix the file closes EXACTLY (445348/445348, zero
  slack -- the "100920 missing bytes" never existed). The entry
  layout is exactly what the round-45 emitters consume: D_801D35E8 =
  this directory, D_80173318 = object count. (2) func_8002128C is
  the OPPONENT-CAR RENDERER as well as its audio (corrects round
  27): it calls the emitters 6x with the slot's model ids
  (bounds-checked against D_80173318) and per-piece transforms via
  func_800129AC. (3) PRIM LAYOUTS: 40-byte = same as MAP.RRM type-B
  (4 corners + texture tail), 961/961 valid; 64-byte = POLY_GT4
  family: 4 model-space verts + 4 UNIT NORMALS Q12 (|n|~=4096,
  per-vertex lighting) + the same 16-byte texture tail. Objects
  0..~23 (~84-104 GT4 quads each) ARE THE CAR MODELS -- rendered 8 of
  them offline with real VRAM liveries: recognizable Ridge Racer
  bodies (rr_cars.png). obj_rro.h updated with the correction.
  NEXT: load OBJ.RRO in the port, render the player car through the
  existing per-quad texture path (+ chase cam), then scenery objects.

- Round 51 ("enchaine"): THE CAR IS IN THE PORT. (1) New `--objfile
  <OBJ.RRO>` loader in main.c using round 50's corrected directory
  (6 counts + computed data offsets; verifies the byte-exact close at
  load). (2) The visible car: object 0's type-64 prims (4 verts +
  4 Q12 normals + texture tail) rotated by the car heading, scaled
  (APPROXIMATED factor 0.30 -- the original's GTE matrix scale in
  func_800129AC not extracted yet), placed on the sampled deck, and
  pushed through the SAME projection/texture path as the track --
  the red body with its real "namco" bumper livery renders in-engine.
  (3) Chase cam v2 WITH a collision probe: pulls in (x0.55 steps)
  whenever the would-be position leaves 55% of the track width,
  preserving the bridge's deck-continuity walk state (a full re-seed
  can snap to the wrong deck at the overpass -- found and avoided).
  KNOWN WIP: model scale approximated, wheels are separate objects
  (slot piece ids) not yet drawn, no gouraud lighting yet (normals
  available), camera framing rough during wall-grind phases (driver
  tuning). False alarm resolved: a 1200-frame sample ended mid-grind
  (gear 1); full 1400 frames end healthy (gear 6, 0x2C3) -- no
  physics regression. 5/5 tests green; capture: rr_drive_r51.gif.

- Round 52 (bigger rounds per user request): FOUR features in one.
  (1) OBJECT SURVEY completes the car-family map: objects 36-38 =
  complete two-wheel AXLES (28 GT4 quads, car-width, round y/z),
  24-35 = body LODs (13-14 quads), 39-55 = per-livery SPOILERS
  (1 quad each); body bbox x±130 z-450..+120 (2.2:1, +z = front).
  (2) WHEELS: the car draws as PIECES (body + 2 axle instances,
  front +40 / rear -350 model units, approximated from the bbox),
  axles SPIN around X with the AUTHENTIC wheel angle car+0x38.
  (3) OPPONENTS (visual drones): N-car draw list (player + 5 drones
  riding the centerline at fixed speeds/lateral offsets with real
  car models 5/6/11/14/16) -- pure visuals until the func_80025268
  AI port; the track reads as a RACE now. (4) LIGHTING + FOG on
  textures: gpu_draw_quad_textured_mod (per-quad 0xRRGGBB texel
  modulation) -- track quads get distance haze, car quads get
  per-quad shading from their Q12 normals (sun high front-left).
  5/5 tests green; capture: rr_drive_r52.gif.

### Not yet done (future round)

- (Deprioritized) The other 8 collision-response fields' consumer is
  still unfound after an exhaustive sweep of `func_80025268`'s entire
  dispatch cluster (rounds 28-31) -- if picked up again, the next step
  would be a much broader whole-codebase search (not AI-cluster-scoped)
  or accepting them as effectively dead code for porting purposes.
- (Deprioritized) car+0xA4's own write/init site is still unfound after
  rounds 25 and 33's searches (direct offset, all direct-global-ref
  functions, and the computed-register pattern, all exhausted) -- if
  picked up again, the next step would be a full call-graph trace from
  `D_8007C258` down through every function that receives the car
  pointer as a parameter, several levels deep. (`D_801E9250`'s own
  +0xA4 sawtooth, round 25's other lead, is closed as of round 32 --
  not camera-shake. Round 23's oscillator helpers and car+0x5C's
  states 2/3 are closed as of round 34 -- see above.)
- (Deprioritized) What advances car+0x5C from 0 to 1/2/3 DURING a race
  is still unfound after round 36 exhausted every direct-offset store
  in the file -- would need a broader/indirect search like the other 2
  deprioritized threads. (Spawn-time reset to 0 is known -- round 35,
  `func_800205E4`/`func_800206CC`.)
- Continue mapping the per-car live state struct -- round 17/19/20/21/22
  surfaced car+0x14, car+0x20, car+0x28, car+0x40 (all read/written by
  the wall-collision response, func_80033584; car+0x20 additionally by
  the press-and-hold ramp func_80033438; 0x20/0x24/0x28/0x40 all
  confirmed round 22 as saved/restored first-class state) but still
  haven't pinned down their precise visual/mechanical meaning.
- The boost/kick mechanic's trigger button is a confirmed DEAD END for
  static tracing (round 18) -- would need a real runtime capture (RAM
  dump during gameplay, or locating whatever resource file populates
  D_801D777C/777E/778C/778E) to make further progress, which is out of
  scope for asm-only tracing.
- Make `physics_wall_probe_lateral_gradient`'s probe reach dynamic
  (curvature-scaled via `physics_steering_reference_raw`'s now-
  understood role, round 18) instead of the fixed `D_8007306E` offsets
  it uses today -- a real upgrade, not a quick follow-on, since it
  changes that function's whole shape.
- Find and trace `func_8001B374`'s own response consumer (the other-slot
  velocity/priority globals it writes) the way round 17 found
  `func_80033584` for the wall probe. Then design and build a multi-car
  simulation architecture -- needed before `func_8001B374` (car-vs-car)
  can be ported at all; this is a scope/architecture decision as much as
  an RE one.
- (RESOLVED round 37) The "aux_a_raw checkpoint path" thread (round 13)
  is closed -- see round 37's entry above. `func_800382A0` fully traced.
- `D_801733A0`'s ROLE is now resolved (round 36/37: a one-time
  race-setup max-range constant, not a physics value) -- what's still an
  open PORTING task is making `physics_find_section_local_walk` adopt
  `func_8001BAFC`'s fully-traced byte-exact gate-line edge-crossing test
  (which consumes that constant) instead of its current center-to-center
  midpoint approximation.
- Extract the original's response SCALE constants (car+0xA4's real
  value, the >>6 rounding, func_80017B58's own tail classification codes)
  to make `physics_wall_probe_lateral_gradient`'s decel less of a tuning
  knob and more of a real port.

## Later / unscheduled

Save states, higher internal resolution / widescreen, proper input
remapping UI, packaging/distribution. None of this matters until phases
2-4 produce something that actually plays.
- **Round 53 (XL) -- la vraie IA adverse + le décor + les corrections de rendu.**
  (1) IA: trace instruction par instruction de `func_80025268` (la boucle
  maître: 12 slots de 0x114 octets à `D_801E9250`) et de ses callees -->
  nouveau module `src/psx_ai.{c,h}` avec registre confirmé/approximé:
  cap de virage blend circulaire BAM12 exact (`func_800177B8`), contrôleur
  de roulis ±2/frame bornes +0x1E/-0xE deadband ±0x50 (`func_80023FF8`),
  vitesse = limite de section x8/10 + accel/frame, structure élastique
  (fenêtre de progression autour du joueur), roue += vitesse avec flou à
  0x321 (variante IA), progression (section<<8|frac) et `D_801E90E0` =
  NOMBRE DE SECTIONS (résout le motif de division ((k<<16)+p)/(k<<8) vu
  partout). 11 adversaires remplacent les drones du round 52, dans les
  deux boucles (interactive + selfdrive).
  (2) Kit de pièces par modèle `D_80059228` (13 x 8 int16, extrait du
  PSX.EXE local au chargement): carrosserie/LOD/essieu par modèle,
  offset d'essieu arrière (-83 monde = -33x modèle), LOD à distance
  Manhattan 0xD00, cull à 0x2500 (tous tracés dans `func_8002128C`).
  (3) Échelle modèle CONFIRMÉE 0.25: `func_800129AC` décale la
  translation GTE de 2 bits (pos<<2) pendant que les verts passent 1:1
  -- recoupé par le double stockage -83/-335 du kit.
  (4) Miroir z du modèle (le nez pointait vers la caméra AVEC texte
  miroir -- signature d'une réflexion d'axe; "namco" se lit maintenant
  sur le pare-chocs arrière).
  (5) CAMÉRA RÉPARÉE: la sonde du round 51 exigeait 55% de largeur et
  coupait 45%/essai -- en virage elle effondrait la distance 300->9 et
  se garait DANS le modèle (la soupe de polygones rouges visible en
  milieu de tour dans le GIF du round 52). Maintenant: largeur réelle,
  retrait doux x0.78, plancher 140. Sol échantillonné à la VOITURE
  (pas la caméra) + échantillonnage par voiture IA (continuité de
  tablier propre à chacune, helper `ground_sample_at`).
  (6) LE DÉCOR: les types A et C de MAP.RRM sont le MÊME format de
  quad texturé 40 octets que le type B (queue texture round 45 valide)
  -- falaises, bâtiments, murs de TUNNEL (l'orange!). Dessinés
  maintenant (budget quads 12288 + cull distance au-delà du brouillard).
  Ouvert: zone sombre vers la section ~40 (records absents du filtre
  route), plafond de tunnel, y IA sur pentes lointaines hors continuité,
  vraie table de grille de départ, `func_80023C58` (élastique exact).
- **Round 54 (XL) -- l'élastique exact, le convertisseur piste->monde, 61 sections retrouvées, le HUD.**
  (1) `func_80023C58` tracée: fenêtre de progression [D_8012CD80, D_8007C510]
  autour du joueur; la franchir met l'IA en état 4 (rattrapage) avec cible =
  9/10 de la borne (+0xB0) et un NIVEAU (+0x60) ajusté ±1/frame vers ±3 par
  bandes de ±5 sections (0 sur cible); le rival (+0xAC==1) force ±5 au-delà
  de sections fixes. Porté dans psx_ai.c (niveau->vitesse ~5%/niveau, seule
  constante encore approximée).
  (2) `func_800181C8` tracée: PAS un résolveur de mur -- c'est le
  convertisseur canonique (progression, latéral) -> monde:
  x = D_801733A0 − lerp(sec.x>>14), **y = −lerp(aux_a)/2**, z = lerp(z>>14),
  cap = 0x800 − lerp circulaire(heading_raw), **carrossage =
  lerp(aux_heading_raw × 8)**, puis x += cos·lat>>11, z −= sin·lat>>11.
  RÉSOUT deux champs ouverts depuis le début de trackdata: aux_a_raw =
  HAUTEUR de section, aux_heading_raw = CARROSSAGE. (Le repère y physique
  vs le maillage MAP.RRM reste à réconcilier -- noté.)
  (3) Le filtre "section réelle si >2 records" excluait 61 sections sur 258
  (une ligne droite simple = 1 seul quad!) -- c'était la zone sombre vers
  les sections 40-42 et d'autres trous. Supprimé de la vue conduite et de
  l'échantillonneur de sol. Le fond du canyon (sections à records type-A
  seuls) reste partiellement sombre -- ouvert.
  (4) HUD course: vitesse km/h (= vitesse brute ×33/100, ~232 km/h au
  max comme l'original), rapport, compteur de tours, style arcade jaune
  ombré. Typographie microfont PLACEHOLDER (les vrais sprites TEX0 =
  round futur). Branché dans les deux boucles (interactive + selfdrive).
  Ouvert R55+: readout de rapport parfois incohérent à vitesse stable (à
  vérifier contre la boîte), fond du canyon, offset y des essieux (roues
  un peu basses), réconciliation des repères y, vrais sprites HUD.
- **Round 55 (XL) -- la VRAIE course, la boîte réparée, les vrais chiffres HUD.**
  (1) Init de course tracée (`func_80021048` -> `func_800206CC`/`func_80020E4C`/
  `func_80020B88`) et EXTRAITE du EXE au chargement (`psx_ai_race_from_exe`):
  roster de grille D_80073130 {2,3,1,11,10,6,7,4,8,5,9,-1} (le mode 4 =
  seulement modèle 2 + la voiture SECRÈTE 12 en rival), grille D_800731C0
  (16 o/slot: x,z,ajust-y, PROGRESSION de départ section<<8 -- slots 0-4
  avec le joueur à la section 68, slots 5-10 égrenés aux sections 144-255 :
  le peloton roulant qu'on remonte, design authentique RR1 !), table
  d'allure D_80073560 (24 o/slot, +0x4 ×8 = allure de croisière par
  voiture, variation ±4-5%/slot dans func_80020B88). Unités vitesse IA =
  1/8 du joueur (intégrateur >>11 vs >>8) -> limite joueur = champ ×64.
  FERME l'approximation "limite par section" : chaque adversaire a UNE
  allure ; les modes 2/3 (intermédiaire/expert) ont leurs tables plus
  rapides (D_80073680/D_800737A0, + variantes miroir).
  (2) Boîte auto réparée : la dynamique rpm approximée s'effondrait hors
  gaz -> cascade jusqu'au rapport 1 à 190 km/h (1793/3000 frames du CSV!),
  puis remontait 1->6 à l'arrêt. Fix (marqués APPROX comme le reste de la
  dynamique rpm) : plancher rpm couplé à la vitesse (l'original couple le
  rpm à la vitesse roue via entry[0]) + chute de rpm au passage de rapport.
  Effet secondaire corrigé : le moteur soutenait alors ~5 de poussée ->
  407 km/h ; ajout de la résistance de transmission approximée (la table
  D_8012CF78 non tracée est la charge qui plafonne la vitesse réelle).
  Distribution finale : p95 = 0x2C8 (~232 km/h), rapports 3-6 en course,
  zéro rapport 1 à vitesse.
  (3) HUD : les VRAIS chiffres -- TEX0 page 0 est la planche de sprites
  HUD du jeu ("winner", "1st/2nd/3rd", "TIME IS UP", compte-tours, et
  plusieurs polices de chiffres). Rangée moyenne mesurée (y=152-175,
  x-runs par chiffre) et échantillonnée en indices 4bpp directement dans
  la VRAM recréée ; formes authentiques, teinte à nous tant que le CLUT
  HUD n'est pas tracé.
  (4) Zone sombre du canyon : AUCUNE cellule MAP.RRM n'a d'origine près
  de la position joueur là-bas -- géométrie absente ou mapping IDX.HED
  décalé sur cette bande. D_801733A0 (l'origine x du convertisseur
  func_800181C8) est en BSS -> tracer son écrivain au R56 pour trancher
  avec des coordonnées d'autorité.
- **Round 56 (XL) -- le repère résolu, les chiffres chromés, la vraie grille.**
  (1) La transformation physique<->maillage TRANCHÉE : D_801733A0 = 0xF000
  (constante écrite par func_80015CD4, retrouvée dans les notes du round 36)
  -> x_maillage = 61440 − x_physique. VALIDÉE par la grille : slot 0 à
  x=27126 vs section 68 à 34604 -> 61440−34604 = 26836, l'écart étant la
  voie de grille. Le canyon diagnostiqué à fond : la route y EXISTE dans le
  maillage mais déplacée de ~2-3k unités dans notre placement de cellules ;
  AUCUN transform global (miroir cellules, 61440−x continu, variantes
  63488/65536) n'aligne tout -> le placement réel du jeu passe par son code
  de streaming MAP/IDX (à tracer, round dédié). Métriques : placement
  actuel avg 548 / pire 2934@sec192 ; les alternatives sont pires.
  (2) HUD : le CLUT des chiffres TROUVÉ = 0x7EC8 -- scoring de chacune des
  143 rangées CLUT de TEX0 contre les texels de la rangée de chiffres ->
  le dégradé CHROME ARGENTÉ authentique du compteur original (0x7F82 =
  gris plat, 0x7802 = variante jaune/rouge, sans doute l'état highlight).
  Le compteur, le rapport et le tour s'affichent maintenant en vraies
  couleurs échantillonnées (page 0 = tpage 5), fini le masque teinté.
  (3) La VRAIE grille de départ : les x/z de D_800731C0 sont en repère
  maillage -> convertis (61440−x) et projetés sur l'axe de la piste, la
  voie latérale réelle de chaque slot remplace le motif de voies inventé
  (les slots lointains gardent l'éparpillement, leurs x/z étant sur
  d'autres tronçons).
  (4) Essieux remontés de 14 unités modèle (y-down) -- les jantes posent
  sur le sol au lieu de couler sous la ligne de caisse.
  Ouvert R57+ : streaming MAP/IDX du jeu (le canyon), compte-tours
  (l'arc est dans la planche page 0), CLUT highlight 0x7802 pour les
  survitesses, positions de grille des slots lointains, sons.
- **Round 57 (XL) -- le streamer de cellules tracé, le compte-tours, le premier son.**
  (1) `func_80012C14` = LE STREAMER MAP/IDX du jeu, tracé : cellule caméra
  = (pos+0x400)>>11 (cellules de 2048, grille 32x32) ; liste de cellules
  visibles par SECTEUR DE YAW (16 secteurs x 64 offsets (dx,dz) signés,
  table statique D_8005944C EXTRAITE -- un vrai frustum précalculé) ;
  gate de visibilité par cellule via func_80015BC4 (PVS) ; lookup :
  **IDX[row*32 + (30 - col)]** (miroir sur 30, pas 31 !) -> id de
  mesh-set (table D_801D35F0 construite par func_800125B4 depuis
  MAP.RRM), -1 = vide ; origine monde = (col<<11, row<<11) direct.
  MAIS : la dérivation physique (x = col_fichier*2048 - x_local) score
  211/256 sections-dans-un-quad contre 212/256 pour notre mapping
  actuel, avec des zones d'échec DIFFÉRENTES -- il manque encore une
  pièce (probablement un réarrangement de la table au chargement :
  l'écrivain de D_801D82D0 est introuvable statiquement). Le canyon
  attend cette pièce ; les deux mappings sont documentés.
  (2) Compte-tours : le cadran réel de la planche TEX0 page 0
  ((64,73)-(152,151), CLUT déclaré 0x7984 -- ticks blancs, zone rouge,
  "X1000 R/MIN") + aiguille programmée : angle = 270° - rpm/0x2710 x
  270° (0 en bas, 10 à droite, comme l'original). En bas à gauche,
  pilotée par le rpm authentique.
  (3) PREMIER SON : tonalité moteur pilotée par le rpm authentique
  (SDL audio, 2 dents-de-scie désaccordées + sous-octave carrée,
  55-280 Hz sur 0-0x2710, souffle de bruit proportionnel au slip de
  drift). Placeholder assumé -- le vrai son viendra de RR.VH/RR.VB
  (VAB). Binaire headless inchangé et silencieux.
- **Round 58 (XL) -- la séquence de chargement, les 6 prims, le VAB ouvert.**
  (1) Canyon : la chaîne complète tracée -- table de fichiers du EXE à
  0x800747BC (MAP.RRM/OBJ.RRO/IDX.HED/TEX*/RR.VH/VB), `func_80032A54` =
  le chargeur maître (CdSearchFile par nom, MAP.RRM -> D_8007C520 puis
  `func_800125B4` construit la table de mesh-sets ; OBJ.RRO juste après,
  `func_80012670` + compte -> D_80173318 [qui est donc le NOMBRE
  D'OBJETS, pas des voix SPU -- round 27 corrigé] ; IDX.HED -> buffer
  dédié D_8007B200 puis `func_80015CD4` qui stocke le pointeur BRUT
  dans D_801D82D0 : PAS de réarrangement). Le mystère restant est donc
  géométrique pur : "sans miroir du tout" score 224/256 sections-dans-
  un-quad (le meilleur), notre rendu actuel 212, la dérivation
  streamer 211 -- avec des bandes d'échec différentes. Prochaine étape
  décidée : vérification DYNAMIQUE (émulateur, dump caméra/cellules).
  (2) Les 6 layouts de prims OBJ.RRO tous décodés (voir obj_rro.h) :
  type 0 = FT4 nu (= MAP type-B), 1 = FT4 teinté, 2 = F4 plat,
  3 = GT4 normales partagées (round 50), 4 = GT4 GOURAUD par sommet +
  teinte (7 gros objets décor), 5 = quad lissé à queue courte.
  (3) LE SON DU JEU OUVERT : RR.VH/RR.VB = VAB standard, parsé à
  l'octet près (58 programmes / 79 tons / 50 VAG dont la somme des
  tailles = exactement les 491056 octets de RR.VB). Programmes 0-16 =
  la famille MOTEUR (un VAG par voiture). Nouvel outil
  `vabparse_tool` (tools/vabparse) : liste les programmes et décode
  n'importe quel VAG en WAV (ADPCM SPU complet). Premier sample moteur
  décodé et livré en WAV.
- **Round 59 (XL) -- LE MONDE S'ASSEMBLE : le placement des cellules PROUVÉ,
  les vrais moteurs VAG dans le port.**
  (1) La percée : les 12 positions de grille (repère maillage + progression
  connue) utilisées comme POINTS D'AUTORITÉ. Résultat : le transform
  x_maillage = 61440 − x_physique confirmé en 4 points propres du circuit
  (slots 5/6/7/10, erreur ≤ 380 = leurs voies), et le placement du jeu
  PROUVÉ : contenu de cellule à (col_monde×2048 + vert), col_monde =
  30 − col_fichier (le miroir n'est qu'une bizarrerie d'INDEXATION du
  lookup IDX[row*32+(30−col)]). LES 12 ANCRES TOMBENT TOUTES dans un quad
  route sous cette règle, et 232/256 centres de section aussi -- les 24
  manquants sont le canyon de terre (sections 34-65) dont la route n'est
  pas faite de quads B (probablement les records A = terrain). Implémenté :
  idx_hed.c retourne l'origine maillage confirmée, macro MAP_PHYS_X(61440−…)
  appliquée aux 6 sites de main.c. RÉSULTAT VISUEL MAJEUR : le tunnel aux
  néons orange, les barrières jaune/noir, l'herbe, les surplombs -- le
  monde entier s'est mis en place (l'ancien mapping décalait TOUT
  subtilement, pas seulement le canyon).
  (2) Les VRAIS MOTEURS : --vabfiles <RR.VH> <RR.VB> décode le VAG moteur
  du modèle joueur (programme = id modèle, famille 0-16) au chargement et
  le callback audio BOUCLE le sample avec un pas de resampling piloté par
  le rpm authentique (0.55x ralenti -> ~1.9x zone rouge). La synthèse
  round-57 reste le fallback sans VAB. Tokens splicés d'argv (piège connu).
  (3) Gouraud type-4 : reporté honnêtement -- les 7 objets concernés sont
  du décor dont le PLACEMENT reste le dernier grand inconnu.
- **Round 60 (FINAL) -- tout ce qui restait est fermé ou tranché.**
  (1) CANYON : RÉSOLU. Avec le placement prouvé du round 59, la portion
  terre (sections 34-65) rend correctement -- sa "route" est faite des
  records A (terrain), dessinés depuis le round 53 ; seul le mauvais
  mapping la déplaçait. Aucune géométrie manquante sur tout le tour.
  (2) OBJETS 56-318 : TRANCHÉ. Les seuls consommateurs côté course sont
  le renderer voitures (kit D_80059228 : ids 0-56 + la secrète
  252/253/256) ; le décor de course est INTÉGRALEMENT des quads MAP.RRM
  A/B/C. Les objets 57-251 et 257-318 sont des assets hors-course
  (menus, podium, intro). Le "placement du décor" n'était pas un
  inconnu : il n'existe pas. Gouraud type-4 : sans consommateur course,
  support reporté sine die (les 7 objets concernés sont hors-course).
  (3) SON DÉRAPAGE : le VAG 20 (composant pneu partagé par les
  programmes 17-27) boucle à volume proportionnel au slip de drift
  authentique, mixé sous le moteur.
  (4) COMPTEUR : variante CLUT 0x7802 (jaune/rouge) sur les chiffres
  km/h quand le rpm est en zone rouge (>= 9000) -- l'highlight du
  scoring round 56 trouvait enfin son rôle. Loi de pitch moteur :
  func_80021BE0 documentée comme le calcul de pitch (base point-fixe
  0x10000/0x17000 selon le mode, termes linéaires en rpm via
  D_8007C32C/E) -- notre approximation 0.55x-1.9x reste dans l'esprit.
  GIF final : 2 800 frames (2 tours au-delà du peloton, dépassements
  des lentes égrenées, tunnel, canyon, autoroute).

## BILAN (rounds 38-60, la phase "port jouable")
- Physique : LE cœur authentique BAM12 (drift, boîte, murs, pente,
  13 modèles, vraies tables moteur) + pont vers la vraie géométrie.
- Course : 11 adversaires IA authentiques (roster/grille/allures du
  EXE, élastique par niveaux, peloton égrené), la secrète en rival.
- Rendu : VRAM PS1 recréée, textures/UV/CLUT par quad, relief,
  clipping, ciel, décor A/B/C complet, voitures en pièces (corps/
  essieux tournants/LOD), éclairage normales Q12, brume, placement
  cellules PROUVÉ (12/12 ancres).
- HUD : chiffres et cadran RÉELS (planche TEX0), chrome + highlight.
- Son : moteurs VAG réels par modèle au pitch rpm + couche dérapage.
- Outils repo : mapparse, worldmap, objparse, texparse, trackdata,
  titlescreen, vabparse -- tous les formats disque fermés à l'octet.
- Encore ouvert (au-delà du périmètre "course jouable") : menus/écrans
  hors-course, loi de pitch exacte, PVS func_80015BC4 au rendu,
  gouraud hors-course, push GitHub (sur demande).
