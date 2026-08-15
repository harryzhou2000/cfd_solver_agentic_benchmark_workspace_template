cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE / tooling integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# External libraries (DNDSR-style layout). The runner may point this at a
# different installation via -DCFD_EXTERNALS_ROOT=<path>.
# ---------------------------------------------------------------------------
set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install" CACHE PATH
    "Root directory of pre-built external libraries (CGNS/HDF5/METIS/zlib)")

find_package(MPI REQUIRED)

# CGNS + HDF5 + METIS + zlib. The CGNS cmake config in this installation
# references build-machine-specific targets (hdf5-shared, hard-coded zlib/MPI
# paths), so plain find_library is used instead; the shared libcgns.so already
# pulls in libhdf5 at runtime via its DT_NEEDED entries.
find_library(CGNS_LIBRARY NAMES cgns PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(HDF5_LIBRARY NAMES hdf5 PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(METIS_LIBRARY NAMES metis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY NAMES z PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)

# find_library sets the variable to "<name>-NOTFOUND" (a non-empty string) on
# failure, so an explicit -NOTFOUND check is required.
foreach(_lib CGNS_LIBRARY HDF5_LIBRARY METIS_LIBRARY ZLIB_LIBRARY)
  if(NOT ${_lib} OR ${_lib} MATCHES "-NOTFOUND$")
    message(FATAL_ERROR "Could not find ${_lib} under ${CFD_EXTERNALS_ROOT}/lib "
            "(set CFD_EXTERNALS_ROOT to the external libraries installation)")
  endif()
endforeach()

# Header-only dependencies
set(EIGEN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external/eigen")
set(FMT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external/fmt/include")
set(JSON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external/nlohmann/single_include")
if(NOT EXISTS "${JSON_INCLUDE_DIR}/nlohmann/json.hpp")
  # flat layout fallback (no single_include/ level)
  set(JSON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external/nlohmann")
endif()
set(ARGPARSE_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external/argparse/include")

# ---------------------------------------------------------------------------
# Solver core library (all sources except main.cpp) — shared by the solver
# executable and the test suite.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOLVER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER SOLVER_SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")

add_library(solver_core STATIC ${SOLVER_SOURCES})

target_include_directories(solver_core PUBLIC
  "${CMAKE_SOURCE_DIR}/src"
  "${CFD_EXTERNALS_ROOT}/include"
  "${EIGEN_INCLUDE_DIR}"
  "${FMT_INCLUDE_DIR}"
  "${JSON_INCLUDE_DIR}"
  "${ARGPARSE_INCLUDE_DIR}"
)

target_link_libraries(solver_core PUBLIC
  MPI::MPI_CXX
  "${CGNS_LIBRARY}"
  "${HDF5_LIBRARY}"
  "${METIS_LIBRARY}"
  "${ZLIB_LIBRARY}"
)

# fmt is header-only in this tree; FMT_HEADER_ONLY makes fmt::print/vprint
# work without linking a compiled fmt library.
target_compile_definitions(solver_core PUBLIC FMT_HEADER_ONLY)

# Current git revision for metadata.json (empty string when not a git repo).
execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE CFD_GIT_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT CFD_GIT_REVISION)
  set(CFD_GIT_REVISION "")
endif()
target_compile_definitions(solver_core PUBLIC
  CFD_GIT_REVISION_STR="${CFD_GIT_REVISION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(solver_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Solver executable
# ---------------------------------------------------------------------------
add_executable(cfd_solver "${CMAKE_SOURCE_DIR}/src/main.cpp")
target_link_libraries(cfd_solver PRIVATE solver_core)

# Let the binary run directly from the build tree without LD_LIBRARY_PATH
set_target_properties(cfd_solver PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cfd_solver PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
