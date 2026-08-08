# rr-pc-port

A from-scratch PC port scaffold derived from
[rr-decomp](https://github.com/kazuyette/rr-decomp), a Ridge Racer (1994,
Namco, PS1) decompilation. This repo does **not** contain the decomp
itself (see that repo for the actual PS1 reverse engineering work) --
it's the beginning of a separate, from-scratch effort to turn the
portable pieces of that decomp into a real PC build.

**Status: phase 1 (infrastructure + trivial vertical slice) only.** This
is not a playable game, not a renderer, not close to one yet. See
[ROADMAP.md](ROADMAP.md) for the honest, phased plan and the key finding
that shapes it: only about 2.5% of the decomp's matched code is genuine
portable C, the rest is byte-exact PS1 assembly transcription.

## What's here

- `src/globals.c`, `src/stubs.c` -- the 24 genuine, hand-written C
  functions ported verbatim from rr-decomp (state-flag getters/setters
  and small pure-logic helpers; no hardware dependency).
- `src/psx_compat/` -- compile-time compatibility shims (type aliases,
  BIOS-call stubs) that future ported code can build against.
- `src/main.c` -- minimal vertical slice: calls a few ported functions
  and prints their results, plus an optional SDL2 window/event loop that
  gracefully degrades to headless when SDL2 isn't available or there's
  no display.
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
