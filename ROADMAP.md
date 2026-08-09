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

## Later / unscheduled

Save states, higher internal resolution / widescreen, proper input
remapping UI, packaging/distribution. None of this matters until phases
2-4 produce something that actually plays.
