cmake_minimum_required(VERSION 3.21)
project(Doodlebold VERSION 1.0.0 DESCRIPTION "Doodlebold painting application" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Bound the build's parallelism (Ninja only). An unbounded build starts one compiler per core, each up to ~0.7 GB on the
# Qt test files, and two such builds at once ran a 16 GB machine out of memory.
set(DOODLEBOLD_COMPILE_JOBS 6 CACHE STRING "Parallel compile jobs (Ninja)")
set(DOODLEBOLD_LINK_JOBS 2 CACHE STRING "Parallel link jobs (Ninja)")
set_property(GLOBAL PROPERTY JOB_POOLS doodlebold_compile=${DOODLEBOLD_COMPILE_JOBS} doodlebold_link=${DOODLEBOLD_LINK_JOBS})
set(CMAKE_JOB_POOL_COMPILE doodlebold_compile)
set(CMAKE_JOB_POOL_LINK doodlebold_link)

# With ccache installed, unchanged files are not compiled again in a new build folder or after a branch switch. CI passes
# its own launcher (sccache).
find_program(DOODLEBOLD_CCACHE ccache)
if(DOODLEBOLD_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  set(CMAKE_C_COMPILER_LAUNCHER "${DOODLEBOLD_CCACHE}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${DOODLEBOLD_CCACHE}")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Svg)

include(cmake/Warnings.cmake)
include(cmake/Lint.cmake)
include(cmake/FloatingPoint.cmake)

# <windows.h> defines min and max as macros, which breaks std::min / std::max (platform/SystemUsage.cpp includes it).
if(WIN32)
  add_compile_definitions(NOMINMAX)
endif()
# The main thread's stack: 1 MB by default on Windows, 8 MB on Linux, where the program is mostly developed.
if(MSVC)
  add_link_options(/STACK:8388608)
elseif(MINGW)
  add_link_options(-Wl,--stack,8388608)
endif()

# A sanitizer build: "address,undefined" or "thread" with GCC/Clang, "address" with MSVC. The sanitizer presets in CMakePresets.json
# build one and run the tests in it; .github/workflows/ci.yml runs the address one.
set(DOODLEBOLD_SANITIZE "" CACHE STRING "Sanitizers to build with, e.g. address,undefined")
if(DOODLEBOLD_SANITIZE)
  if(MSVC)
    add_compile_options(/fsanitize=${DOODLEBOLD_SANITIZE})
    add_link_options(/INCREMENTAL:NO)
  else()
    add_compile_options(-fsanitize=${DOODLEBOLD_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${DOODLEBOLD_SANITIZE})
  endif()
endif()

option(DOODLEBOLD_BUILD_TESTS "Build unit and golden-image tests" ON)

add_subdirectory(src)
add_subdirectory(plugins)

if(DOODLEBOLD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
doodlebold_add_lint_targets()
