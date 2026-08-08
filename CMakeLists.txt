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
  src/psx_compat/psx_bios.c
  src/gpu/gpu_soft.c
)

target_include_directories(rr_pc_port PRIVATE src)
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
