# rr-pc-port

A from-scratch PC port scaffold derived from
[rr-decomp](https://github.com/kazuyette/rr-decomp), a Ridge Racer (1994,
Namco, PS1) decompilation. This repo does **not** contain the decomp
itself (see that repo for the actual PS1 reverse engineering work) --
it's the beginning of a separate, from-scratch effort to turn the
portable pieces of that decomp into a real PC build.

**Status: playable race slice (phases 1-7, rounds 1-60).** The port now
runs a full race on the real course: the authentic BAM12 fixed-point
player physics (drift model, gearbox, walls, slope -- ported at
instruction level from the decomp), 11 AI opponents driven by the game's
own roster/grid/pace tables (extracted at runtime from the user's local
PSX.EXE, never committed), the whole world rendered with real textures
(recreated PS1 VRAM, per-quad UV/CLUT/TPAGE, cell placement PROVEN
against the game's own streamer), the real HUD sprites (chrome digits +
tachometer dial) and the actual engine/skid samples decoded from the VAB
sound bank, pitch-driven by the authentic rpm. See
[ROADMAP.md](ROADMAP.md) for the complete round-by-round log with its
strict CONFIRMED-vs-APPROXIMATED provenance ledger.

## What's here

- `src/globals.c`, `src/stubs.c` -- the 24 genuine, hand-written C
  functions ported verbatim from rr-decomp (state-flag getters/setters
  and small pure-logic helpers; no hardware dependency).
- `src/psx_compat/` -- compile-time compatibility shims (type aliases,
  BIOS-call stubs) that future ported code can build against.
- `src/main.c` -- minimal vertical slice: calls a few ported functions
  and prints their results, plus an optional SDL2 window/event loop that
  gracefully degrades to headless when SDL2 isn't available or there's
  no display. Also hosts the `--track` course demo (top-down + drivable
  3D views, autopilot, optional texturing) and, since Phase 7, a
  real-physics driving mode (**C** key).
- `src/physics.c`/`physics.h` -- portable-C car physics core (gearbox,
  track-relative off-track test, heading interpolation) reimplementing
  the CONFIRMED structure of the original PS1 game's live race code --
  see ROADMAP.md Phase 7 and the file header comments for exactly what's
  a faithful port vs. a documented approximation.
- `tools/trackdata/` -- extracts Ridge Racer 1's two built-in course
  section-geometry tables directly out of the user's own PSX.EXE, same
  never-commit-the-asset pattern as the sibling `tools/mapparse` (MAP.RRM
  / IDX.HED / OBJ.RRO) and `tools/texparse` (TEX*.TMS) tools -- see
  ROADMAP.md for the full asset-format writeups.
- `CMakeLists.txt` -- host gcc/clang build (not the PS1 cross toolchain).

## Building

```sh
mkdir build && cd build
cmake ..
make
SDL_VIDEODRIVER=dummy ./rr_pc_port   # dummy driver avoids hangs with no display
```

SDL2 is optional. If `find_package(SDL2)` / `pkg-config sdl2` can't find
it, the build produces a headless binary that still exercises the ported
logic functions.

## What's NOT here (and never will be)

No copyrighted game assets: no disc image, no PSX.EXE, no textures,
audio, or track data. Only original scaffolding/tooling/ported-logic
source.

## License

Same spirit as rr-decomp: original code here (scaffolding, compat
shims, ported logic re-expressed as portable C) is provided as-is for
personal/research use. See rr-decomp's LICENSE for context on the
underlying reverse-engineering work this builds on.
