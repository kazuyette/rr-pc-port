cmake_minimum_required(VERSION 3.13)
project(rr_pc_port C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_executable(rr_pc_port
  src/main.c
  src/globals.c
  src/stubs.c
  src/ported_logic.c
  src/physics.c
  src/physics_psx.c
  src/psx_track_bridge.c
  src/psx_ai.c
  src/psx_compat/psx_bios.c
  src/gpu/gpu_soft.c
  tools/texparse/tim.c
  tools/texparse/psx_vram.c
  tools/vabparse/vab.c
  tools/mapparse/map_rrm.c
  tools/mapparse/idx_hed.c
  tools/trackdata/trackdata.c
)

target_include_directories(rr_pc_port PRIVATE src tools/texparse tools/mapparse tools/trackdata tools/vabparse)
target_compile_options(rr_pc_port PRIVATE -Wall -Wextra)

# Phase 3: headless sanity check for the software rasterizer, no SDL
# needed -- exercises gpu_draw_triangle_flat() directly against the raw
# framebuffer and asserts pixel colors. Runs fine in CI/sandboxes with
# no display. See src/gpu/gpu_soft_test.c.
add_executable(rr_pc_port_gpu_test
  src/gpu/gpu_soft_test.c
  src/gpu/gpu_soft.c
)
target_include_directories(rr_pc_port_gpu_test PRIVATE src/gpu)
target_compile_options(rr_pc_port_gpu_test PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME gpu_soft_sanity COMMAND rr_pc_port_gpu_test)

# Track-data reverse-engineering tool: standalone CLI, no game/PSX
# dependencies. Parses a MAP.RRM file (from the user's own local disc
# extraction, never committed to this repo) and prints/exports its
# structure. See tools/mapparse/map_rrm.h for the format writeup.
add_executable(mapparse_tool
  tools/mapparse/mapparse_main.c
  tools/mapparse/map_rrm.c
)
target_include_directories(mapparse_tool PRIVATE tools/mapparse)
target_compile_options(mapparse_tool PRIVATE -Wall -Wextra)

# Phase 5 round 2: IDX.HED spatial-grid parser + combined MAP.RRM/IDX.HED
# world-space plotter, and a directory-level OBJ.RRO parser. See
# tools/mapparse/idx_hed.h and tools/mapparse/obj_rro.h for the format
# writeups (confirmed vs. hypothesis, clearly separated).
add_executable(worldmap_tool
  tools/mapparse/worldmap_main.c
  tools/mapparse/map_rrm.c
  tools/mapparse/idx_hed.c
)
target_include_directories(worldmap_tool PRIVATE tools/mapparse)
target_compile_options(worldmap_tool PRIVATE -Wall -Wextra)

add_executable(objparse_tool
  tools/mapparse/objparse_main.c
  tools/mapparse/obj_rro.c
)
target_include_directories(objparse_tool PRIVATE tools/mapparse)
target_compile_options(objparse_tool PRIVATE -Wall -Wextra)

# Physics-RE round 8 (rr-decomp session): extracts Ridge Racer 1's two
# built-in section-chain course-geometry tables directly out of the
# user's own PSX.EXE (never committed to this repo) -- see
# tools/trackdata/trackdata.h for the format writeup (confirmed via an
# exact instruction-level trace of PS1 func_80017B58, not guessed) and
# src/physics.h for how this data feeds the port's physics code.
add_executable(trackdata_tool
  tools/trackdata/trackdata_main.c
  tools/trackdata/trackdata.c
)
target_include_directories(trackdata_tool PRIVATE tools/trackdata)
target_compile_options(trackdata_tool PRIVATE -Wall -Wextra)

# Headless sanity check for src/physics.c + tools/trackdata's
# project-point math: builds a synthetic track section by hand (no real
# PSX.EXE needed) and asserts the CONFIRMED formulas (gear-shift
# thresholds, asymmetric off-track width test, BAM12 heading wraparound)
# behave as documented. See src/physics_test.c.
add_executable(rr_pc_port_physics_test
  src/physics_test.c
  src/physics.c
  tools/trackdata/trackdata.c
)
target_include_directories(rr_pc_port_physics_test PRIVATE src tools/trackdata)
target_compile_options(rr_pc_port_physics_test PRIVATE -Wall -Wextra)
add_test(NAME physics_sanity COMMAND rr_pc_port_physics_test)

# Round 40: the AUTHENTIC fixed-point player physics (BAM12/integer,
# ported from func_8001C490 + callees -- see src/physics_psx.c's
# provenance ledger) with its own headless validation: autopilot lap
# on a synthetic oval, drift-lag assertion, wall x0.7 assertion.
add_executable(rr_pc_port_physics_psx_test
  src/physics_psx_test.c
  src/physics_psx.c
)
target_include_directories(rr_pc_port_physics_psx_test PRIVATE src)
target_compile_options(rr_pc_port_physics_psx_test PRIVATE -Wall -Wextra)
add_test(NAME physics_psx_authentic COMMAND rr_pc_port_physics_psx_test)

# Round 42: the authentic core driving the REAL course (trackdata from
# the user's local PSX.EXE -- SKIPs cleanly when RR_EXE_FILE is unset,
# same pattern as texdemo_headless_decode; no game data in the repo).
add_executable(rr_psx_drive
  src/psx_drive_demo.c
  src/physics_psx.c
  src/psx_track_bridge.c
  tools/trackdata/trackdata.c
)
target_include_directories(rr_psx_drive PRIVATE src tools/trackdata)
target_compile_options(rr_psx_drive PRIVATE -Wall -Wextra)
add_test(NAME psx_drive_real_course COMMAND rr_psx_drive)

# Texture reverse-engineering tool: standalone CLI, no game/PSX
# dependencies. Parses a TEX*.TMS file (from the user's own local disc
# extraction, never committed to this repo) -- a container of PS1 TIM
# texture pages -- and prints/dumps its structure. See
# tools/texparse/tim.h for the format writeup (confirmed vs. hypothesis,
# clearly separated, plus a documented caveat about the pixmap length
# field found via independent hex verification this round).
add_executable(texparse_tool
  tools/texparse/texparse_main.c
  tools/texparse/tim.c
)
target_include_directories(texparse_tool PRIVATE tools/texparse)
target_compile_options(texparse_tool PRIVATE -Wall -Wextra)

# Headless proof-of-concept: decodes a real TEX*.TMS file via tim.c and
# renders one page through gpu_draw_quad_textured() into the raw
# framebuffer, then asserts the result actually varies (not all one
# color) -- i.e. real game texture art made it through the whole
# decode -> rasterize pipeline. Needs a real local texture file, which
# is never committed to this repo, so this test SKIPS (exit 0) rather
# than failing when the RR_TEX_FILE env var isn't set or doesn't point
# at a readable file -- keeps `ctest` green in CI/sandboxes that don't
# have the asset, while still giving a real, automatable check for
# anyone who does (see README/task notes for how to point it at a real
# TEX0.TMS locally).
add_executable(rr_pc_port_texdemo_test
  tools/texparse/texdemo_test.c
  tools/texparse/tim.c
  src/gpu/gpu_soft.c
)
target_include_directories(rr_pc_port_texdemo_test PRIVATE tools/texparse src/gpu)
target_compile_options(rr_pc_port_texdemo_test PRIVATE -Wall -Wextra)
add_test(NAME texdemo_headless_decode COMMAND rr_pc_port_texdemo_test)

# Round 58: VAB sound-bank parser (RR.VH/RR.VB) -- standalone CLI, no
# game data in the repo. Format writeup in tools/vabparse/vab.h
# (confirmed byte-exact against the local files: 50 VAG sizes sum to
# RR.VB's length). Can decode any VAG to a WAV for verification.
add_executable(vabparse_tool
  tools/vabparse/vabparse_main.c
  tools/vabparse/vab.c
)
target_include_directories(vabparse_tool PRIVATE tools/vabparse)
target_compile_options(vabparse_tool PRIVATE -Wall -Wextra)

# Title-screen reconstruction: composites the real "RIDGE RACER" title
# logo + START BUTTON prompt + namcot logo (decoded from the user's own
# local TEX3.TMS, never committed to this repo) into a 320x240 PPM.
# See tools/titlescreen/title_main.c for the asset-provenance writeup
# (which pages of which bank, and what is reconstruction vs. traced).
add_executable(rr_title_tool
  tools/titlescreen/title_main.c
  tools/texparse/tim.c
)
target_include_directories(rr_title_tool PRIVATE tools/texparse)
target_compile_options(rr_title_tool PRIVATE -Wall -Wextra)

# SDL2 is optional: this is a HOST port scaffold, not the PSX cross build.
# If SDL2 dev headers/libs aren't present, we build a headless binary that
# still exercises the ported logic functions -- see ROADMAP.md phase 3/4
# for when real windowing/rendering actually matters.
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
  endif()
endif()

if(SDL2_FOUND)
  message(STATUS "rr-pc-port: SDL2 found, building with windowing support")
  target_compile_definitions(rr_pc_port PRIVATE HAVE_SDL2)
  target_include_directories(rr_pc_port PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(rr_pc_port PRIVATE ${SDL2_LIBRARIES})
  target_link_directories(rr_pc_port PRIVATE ${SDL2_LIBRARY_DIRS})
else()
  message(STATUS "rr-pc-port: SDL2 NOT found, building headless (logic-only) binary")
endif()

# Phase 3 animated demo scene (src/main.c) uses <math.h> (sin/cos) for
# the rotating/pulsing shapes -- link libm where it's a separate library
# (Linux/WSL); on platforms where it's already part of libc (e.g. MSVC)
# find_library just won't find a separate one and this is a no-op.
find_library(RR_PC_PORT_MATH_LIBRARY m)
if(RR_PC_PORT_MATH_LIBRARY)
  target_link_libraries(rr_pc_port PRIVATE ${RR_PC_PORT_MATH_LIBRARY})
  target_link_libraries(trackdata_tool PRIVATE ${RR_PC_PORT_MATH_LIBRARY})
  target_link_libraries(rr_pc_port_physics_test PRIVATE ${RR_PC_PORT_MATH_LIBRARY})
  target_link_libraries(rr_pc_port_physics_psx_test PRIVATE ${RR_PC_PORT_MATH_LIBRARY})
  target_link_libraries(rr_psx_drive PRIVATE ${RR_PC_PORT_MATH_LIBRARY})
endif()
