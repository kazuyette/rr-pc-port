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
)

target_include_directories(rr_pc_port PRIVATE src)
target_compile_options(rr_pc_port PRIVATE -Wall -Wextra)

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
