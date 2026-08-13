cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Standard and compiler flags
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# ---------------------------------------------------------------------------
# External compiled libraries (CGNS, HDF5, METIS, zlib) live under
#   external/cfd_externals/install  (relative to the repo root).
# Override with:  cmake .. -DCFD_EXTERNALS_ROOT=/path/to/install
# A relative value is resolved against the solver source dir first (so the
# documented `-DCFD_EXTERNALS_ROOT=../external/cfd_externals/install`
# invocation from solver/build works), falling back to the build dir.
# ---------------------------------------------------------------------------
if(NOT DEFINED CFD_EXTERNALS_ROOT OR "${CFD_EXTERNALS_ROOT}" STREQUAL "")
  set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
endif()
if(NOT IS_ABSOLUTE "${CFD_EXTERNALS_ROOT}")
  if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${CFD_EXTERNALS_ROOT}")
    set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_BINARY_DIR}/${CFD_EXTERNALS_ROOT}")
  else()
    set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/${CFD_EXTERNALS_ROOT}")
  endif()
endif()
set(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT}" CACHE PATH
    "Root of the compiled external libraries (CGNS, HDF5, METIS, zlib)" FORCE)

message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")
if(NOT EXISTS "${CFD_EXTERNALS_ROOT}/lib")
  message(FATAL_ERROR "CFD_EXTERNALS_ROOT does not contain a lib/ directory: ${CFD_EXTERNALS_ROOT}")
endif()

# ---------------------------------------------------------------------------
# MPI (use the MPI compiler wrapper found on PATH)
# ---------------------------------------------------------------------------
find_package(MPI REQUIRED COMPONENTS CXX)

# ---------------------------------------------------------------------------
# Header-only libraries
# ---------------------------------------------------------------------------
set(HEADER_ONLY_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}/../external/eigen"            # Eigen/
  "${CMAKE_CURRENT_SOURCE_DIR}/../external/fmt/include"      # fmt/
  "${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann"         # nlohmann/
  "${CMAKE_CURRENT_SOURCE_DIR}/../external/argparse/include" # argparse/
)

# ---------------------------------------------------------------------------
# Compiled external libraries from CFD_EXTERNALS_ROOT
# ---------------------------------------------------------------------------
find_library(CGNS_LIBRARY    cgns     HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(HDF5_LIBRARY    hdf5     HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(HDF5_HL_LIBRARY hdf5_hl  HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(METIS_LIBRARY   metis    HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY    z        HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(DL_LIBRARY      dl)

foreach(_lib CGNS HDF5 HDF5_HL METIS ZLIB)
  if(NOT ${_lib}_LIBRARY)
    message(FATAL_ERROR "External library ${_lib} not found under ${CFD_EXTERNALS_ROOT}/lib")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# Solver executable
# ---------------------------------------------------------------------------
set(SOLVER_SOURCES
  src/boundary.cpp
  src/case_reader.cpp
  src/flux.cpp
  src/gradient.cpp
  src/halo.cpp
  src/limiter.cpp
  src/main.cpp
  src/mesh.cpp
  src/output.cpp
  src/partition.cpp
  src/physics.cpp
  src/solver.cpp
  src/viscous.cpp
)

add_executable(cfd_solver ${SOLVER_SOURCES})

target_include_directories(cfd_solver PRIVATE
  "${CFD_EXTERNALS_ROOT}/include"
  ${HEADER_ONLY_INCLUDE_DIRS}
)

target_link_libraries(cfd_solver PRIVATE
  MPI::MPI_CXX
  "${CGNS_LIBRARY}"
  "${HDF5_HL_LIBRARY}"
  "${HDF5_LIBRARY}"
  "${METIS_LIBRARY}"
  "${ZLIB_LIBRARY}"
  "${DL_LIBRARY}"
)

# ---------------------------------------------------------------------------
# Tests: run the solver on known cases and check the exit status
# ---------------------------------------------------------------------------
enable_testing()

set(CFD_BENCHMARK_CASES_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/cases")

# The tests run the full benchmark pipeline (partitioning, halo exchange,
# second-order residual, implicit marching) on 4 ranks. The implicit
# solver converges the inviscid NACA case in ~500 steps; the cylinder cases
# are bounded with --max-steps so the smoke suite stays fast: the Re=20 case
# is a steady run (credible plateau by step 3000) and the Re=200 case is a
# transient BDF2 run bounded to 20 physical steps (dt = 0.01, t = 0.2).
add_test(NAME solve_naca0012_m015_inviscid
  COMMAND mpirun -np 4 cfd_solver solve
          --case "${CFD_BENCHMARK_CASES_DIR}/naca0012_m015_inviscid.json"
          --output "${CMAKE_CURRENT_BINARY_DIR}/ctest_out_naca0012_m015_inviscid")

add_test(NAME solve_cylinder_m010_laminar_re20
  COMMAND mpirun -np 4 cfd_solver solve
          --case "${CFD_BENCHMARK_CASES_DIR}/cylinder_m010_laminar_re20.json"
          --output "${CMAKE_CURRENT_BINARY_DIR}/ctest_out_cylinder_m010_laminar_re20"
          --max-steps 3000)

add_test(NAME solve_cylinder_m010_laminar_re200
  COMMAND mpirun -np 4 cfd_solver solve
          --case "${CFD_BENCHMARK_CASES_DIR}/cylinder_m010_laminar_re200.json"
          --output "${CMAKE_CURRENT_BINARY_DIR}/ctest_out_cylinder_m010_laminar_re200"
          --max-steps 20)
