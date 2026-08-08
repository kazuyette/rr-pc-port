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

## Phase 6 -- Audio

SPU emulation or a from-scratch replacement audio engine, once there's
enough of the game loop running to actually need sound cues. Lowest
priority; a fully silent, correctly-playing game loop is a fine
milestone before this.

## Later / unscheduled

Save states, higher internal resolution / widescreen, proper input
remapping UI, packaging/distribution. None of this matters until phases
2-4 produce something that actually plays.
