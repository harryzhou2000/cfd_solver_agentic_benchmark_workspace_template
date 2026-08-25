cmake_minimum_required(VERSION 3.16)
project(cfd2d_benchmark_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- External dependency root (DNDSR convention) --------------------------
# Priority: -DCFD_EXTERNALS_ROOT=... > $ENV{CFD_EXTERNALS_ROOT} > repo default
if(NOT DEFINED CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
  else()
    get_filename_component(CFD_EXTERNALS_ROOT
      "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install" ABSOLUTE)
  endif()
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

if(NOT EXISTS "${CFD_EXTERNALS_ROOT}/include/cgnslib.h")
  message(FATAL_ERROR "cgnslib.h not found under ${CFD_EXTERNALS_ROOT}/include. "
                      "Set CFD_EXTERNALS_ROOT to the cfd_externals install prefix.")
endif()

# Header-only packages live one level above the install prefix (external/).
get_filename_component(CFD_EXTERNALS_PKG "${CFD_EXTERNALS_ROOT}" DIRECTORY)
get_filename_component(CFD_EXTERNALS_PKG "${CFD_EXTERNALS_PKG}" DIRECTORY)
message(STATUS "Header-only package root = ${CFD_EXTERNALS_PKG}")

find_package(MPI REQUIRED COMPONENTS CXX)

# Capture the parent repository revision for metadata (best effort).
execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
  OUTPUT_VARIABLE GIT_REV
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)

add_executable(cfd2d
  src/main.cpp
  src/case_file.cpp
  src/mesh.cpp
  src/partition.cpp
  src/spatial.cpp
  src/timestep.cpp
  src/output.cpp
)

target_include_directories(cfd2d PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CFD_EXTERNALS_ROOT}/include
  ${CFD_EXTERNALS_PKG}/nlohmann
)

target_link_directories(cfd2d PRIVATE ${CFD_EXTERNALS_ROOT}/lib)

target_link_libraries(cfd2d PRIVATE
  MPI::MPI_CXX
  cgns
  hdf5
  metis
  z
  dl
)

target_compile_options(cfd2d PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)

if(GIT_REV)
  target_compile_definitions(cfd2d PRIVATE GIT_REVISION_STR="${GIT_REV}")
endif()

# RPATH so the binary runs without LD_LIBRARY_PATH tweaks.
set_target_properties(cfd2d PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib")
