cmake_minimum_required(VERSION 3.16)
project(cns2d LANGUAGES CXX)

# ---------------------------------------------------------------------------
# cns2d: 2-D unstructured compressible Navier-Stokes finite-volume solver.
#
# Dependency locations are configurable.  The DNDSR-style external tree is
# located with CFD_EXTERNALS_ROOT (CMake cache variable or environment
# variable); header-only packages are looked up under CFD_EXTERNALS_HEADER_ROOT
# (defaults to the parent of CFD_EXTERNALS_ROOT/..).
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CNS2D_EXTRA_RELEASE_FLAGS "-O3" CACHE STRING "Optimization flags appended for Release builds")

# --- version / provenance --------------------------------------------------
set(CNS2D_VERSION "1.0.0")
# Record the git revision so metadata.json can identify the build that produced a
# result.  This is re-evaluated on EVERY build rather than only at configure
# time: a revision captured once at configure time goes stale as soon as another
# commit lands, which silently mislabels every subsequent result with the
# revision the build directory happened to be created at.
add_custom_target(cns2d_version ALL
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h"
  COMMAND "${CMAKE_COMMAND}"
          -D "CNS2D_VERSION=${CNS2D_VERSION}"
          -D "CNS2D_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
          -D "CNS2D_VERSION_IN=${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
          -D "CNS2D_VERSION_OUT=${CMAKE_CURRENT_BINARY_DIR}/generated/version.h"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StampVersion.cmake"
  COMMENT "Stamping git revision into version.h"
  VERBATIM)

# Generate an initial copy at configure time so the first build has a header to
# compile against even before the custom target runs.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/StampVersion.cmake")

# --- external roots --------------------------------------------------------
if(NOT CFD_EXTERNALS_ROOT AND DEFINED ENV{CFD_EXTERNALS_ROOT})
  set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
endif()
if(NOT CFD_EXTERNALS_ROOT)
  foreach(candidate
          "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install"
          "/opt/external/cfd_externals/install")
    if(EXISTS "${candidate}/include/cgnslib.h")
      set(CFD_EXTERNALS_ROOT "${candidate}")
      break()
    endif()
  endforeach()
endif()
if(NOT CFD_EXTERNALS_ROOT)
  message(FATAL_ERROR
    "CFD_EXTERNALS_ROOT not found.  Pass -DCFD_EXTERNALS_ROOT=<path to cfd_externals/install>.")
endif()
get_filename_component(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT}" ABSOLUTE)
message(STATUS "cns2d: CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

if(NOT CFD_EXTERNALS_HEADER_ROOT AND DEFINED ENV{CFD_EXTERNALS_HEADER_ROOT})
  set(CFD_EXTERNALS_HEADER_ROOT "$ENV{CFD_EXTERNALS_HEADER_ROOT}")
endif()
if(NOT CFD_EXTERNALS_HEADER_ROOT)
  get_filename_component(_cns2d_ext_parent "${CFD_EXTERNALS_ROOT}/../.." ABSOLUTE)
  set(CFD_EXTERNALS_HEADER_ROOT "${_cns2d_ext_parent}")
endif()
get_filename_component(CFD_EXTERNALS_HEADER_ROOT "${CFD_EXTERNALS_HEADER_ROOT}" ABSOLUTE)
message(STATUS "cns2d: CFD_EXTERNALS_HEADER_ROOT = ${CFD_EXTERNALS_HEADER_ROOT}")

# --- MPI (mandatory) -------------------------------------------------------
find_package(MPI REQUIRED COMPONENTS CXX)

# --- compiled libraries under the externals install tree -------------------
function(cns2d_find_library out_var lib_name header_name)
  find_library(${out_var}
    NAMES ${lib_name}
    HINTS "${CFD_EXTERNALS_ROOT}/lib" "${CFD_EXTERNALS_ROOT}/lib64"
    NO_DEFAULT_PATH)
  if(NOT ${out_var})
    find_library(${out_var} NAMES ${lib_name})
  endif()
  if(NOT ${out_var})
    message(FATAL_ERROR "cns2d: required library '${lib_name}' (header ${header_name}) not found under ${CFD_EXTERNALS_ROOT}")
  endif()
  set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

cns2d_find_library(CNS2D_CGNS_LIBRARY cgns cgnslib.h)
cns2d_find_library(CNS2D_HDF5_LIBRARY hdf5 hdf5.h)
cns2d_find_library(CNS2D_METIS_LIBRARY metis metis.h)
find_library(CNS2D_Z_LIBRARY NAMES z HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
if(NOT CNS2D_Z_LIBRARY)
  find_library(CNS2D_Z_LIBRARY NAMES z)
endif()

find_path(CNS2D_CGNS_INCLUDE_DIR cgnslib.h HINTS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
find_path(CNS2D_METIS_INCLUDE_DIR metis.h HINTS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
if(NOT CNS2D_CGNS_INCLUDE_DIR OR NOT CNS2D_METIS_INCLUDE_DIR)
  message(FATAL_ERROR "cns2d: cgnslib.h / metis.h not found under ${CFD_EXTERNALS_ROOT}/include")
endif()

message(STATUS "cns2d: CGNS  = ${CNS2D_CGNS_LIBRARY}")
message(STATUS "cns2d: HDF5  = ${CNS2D_HDF5_LIBRARY}")
message(STATUS "cns2d: METIS = ${CNS2D_METIS_LIBRARY}")

# --- header-only third-party packages -------------------------------------
find_path(CNS2D_JSON_INCLUDE_DIR nlohmann/json.hpp
  HINTS "${CFD_EXTERNALS_HEADER_ROOT}/nlohmann" "${CFD_EXTERNALS_ROOT}/include"
  NO_DEFAULT_PATH)
if(NOT CNS2D_JSON_INCLUDE_DIR)
  find_path(CNS2D_JSON_INCLUDE_DIR nlohmann/json.hpp)
endif()
if(NOT CNS2D_JSON_INCLUDE_DIR)
  message(FATAL_ERROR "cns2d: nlohmann/json.hpp not found; set -DCFD_EXTERNALS_HEADER_ROOT=<dir containing nlohmann/>")
endif()
message(STATUS "cns2d: nlohmann_json include = ${CNS2D_JSON_INCLUDE_DIR}")

find_path(CNS2D_EIGEN_INCLUDE_DIR Eigen/Dense
  HINTS "${CFD_EXTERNALS_HEADER_ROOT}/eigen" "${CFD_EXTERNALS_ROOT}/include/eigen3"
  NO_DEFAULT_PATH)
if(NOT CNS2D_EIGEN_INCLUDE_DIR)
  find_path(CNS2D_EIGEN_INCLUDE_DIR Eigen/Dense PATH_SUFFIXES eigen3)
endif()
if(CNS2D_EIGEN_INCLUDE_DIR)
  message(STATUS "cns2d: Eigen include = ${CNS2D_EIGEN_INCLUDE_DIR}")
endif()

# --- solver library --------------------------------------------------------
add_library(cns2d_core
  src/core/options.cpp
  src/core/path_utils.cpp
  src/core/case_input.cpp
  src/core/logging.cpp
  src/mesh/cgns_reader.cpp
  src/mesh/global_mesh.cpp
  src/mesh/geometry.cpp
  src/mesh/mesh_verification.cpp
  src/parallel/partitioner.cpp
  src/parallel/distributed_mesh.cpp
  src/parallel/halo_exchange.cpp
  src/physics/perfect_gas.cpp
  src/physics/boundary_conditions.cpp
  src/numerics/gradients.cpp
  src/numerics/limiter.cpp
  src/numerics/riemann_flux.cpp
  src/numerics/viscous_flux.cpp
  src/numerics/residual.cpp
  src/solve/local_time_step.cpp
  src/solve/lusgs.cpp
  src/solve/solver_context.cpp
  src/solve/steady_driver.cpp
  src/solve/transient_driver.cpp
  src/io/output_writer.cpp
  src/io/vtu_writer.cpp
  src/io/restart_io.cpp
  src/post/forces.cpp
  src/post/surface_output.cpp
)
target_include_directories(cns2d_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${CNS2D_CGNS_INCLUDE_DIR}"
  "${CNS2D_METIS_INCLUDE_DIR}"
  "${CNS2D_JSON_INCLUDE_DIR}"
)
if(CNS2D_EIGEN_INCLUDE_DIR)
  target_include_directories(cns2d_core PUBLIC "${CNS2D_EIGEN_INCLUDE_DIR}")
  target_compile_definitions(cns2d_core PUBLIC CNS2D_HAVE_EIGEN=1)
endif()
target_link_libraries(cns2d_core PUBLIC
  MPI::MPI_CXX
  "${CNS2D_CGNS_LIBRARY}"
  "${CNS2D_HDF5_LIBRARY}"
  "${CNS2D_METIS_LIBRARY}"
)
if(CNS2D_Z_LIBRARY)
  target_link_libraries(cns2d_core PUBLIC "${CNS2D_Z_LIBRARY}")
endif()
target_compile_options(cns2d_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(cns2d_core PRIVATE ${CNS2D_EXTRA_RELEASE_FLAGS})
endif()

# The revision stamp must complete BEFORE anything that embeds it is compiled.
# Without this the stamp target and the compilation run concurrently, so a
# changed revision only reaches the binary on the following build -- which is
# exactly how every result came to be labelled with a stale commit.
add_dependencies(cns2d_core cns2d_version)

# --- executables -----------------------------------------------------------
add_executable(cns2d src/main.cpp)
target_link_libraries(cns2d PRIVATE cns2d_core)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(cns2d PRIVATE ${CNS2D_EXTRA_RELEASE_FLAGS})
endif()
add_dependencies(cns2d cns2d_version)

add_executable(cns2d_tests tests/unit_tests.cpp)
target_link_libraries(cns2d_tests PRIVATE cns2d_core)

enable_testing()
add_test(NAME cns2d_unit COMMAND cns2d_tests)
