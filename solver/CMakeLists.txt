cmake_minimum_required(VERSION 3.16)
project(cfd_solver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")

# ---------------------------------------------------------------------------
# External dependency locations
#   CFD_EXTERNALS_ROOT : compiled libs (CGNS, HDF5, METIS, ...) install dir
#   EXTERNAL_HEADERS   : directory with header-only packages (nlohmann, eigen)
# ---------------------------------------------------------------------------
set(CFD_EXTERNALS_ROOT "" CACHE PATH "Path to external/cfd_externals/install")
set(EXTERNAL_HEADERS "" CACHE PATH "Path to the external/ header-only packages dir")

if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
endif()
if(NOT EXTERNAL_HEADERS)
  set(EXTERNAL_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/../external")
endif()

find_package(MPI REQUIRED COMPONENTS CXX)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CFD_EXTERNALS_ROOT}/include
  ${EXTERNAL_HEADERS}/nlohmann
)

link_directories(${CFD_EXTERNALS_ROOT}/lib)

add_executable(cfd_solver
  src/main.cpp
  src/case_io.cpp
  src/mesh_global.cpp
  src/partition.cpp
  src/mesh_local.cpp
  src/reconstruction.cpp
  src/solver.cpp
  src/output.cpp
  src/forces.cpp
)

target_link_libraries(cfd_solver
  ${MPI_CXX_LIBRARIES}
  cgns
  hdf5
  hdf5_hl
  z
  metis
  m
)

target_include_directories(cfd_solver PRIVATE ${MPI_CXX_INCLUDE_DIRS})
target_compile_options(cfd_solver PRIVATE ${MPI_CXX_COMPILE_OPTIONS})

execute_process(
  COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short HEAD
  OUTPUT_VARIABLE CFD_GIT_REV
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT CFD_GIT_REV)
  set(CFD_GIT_REV "unknown")
endif()
target_compile_definitions(cfd_solver PRIVATE
  SOLVER_VERSION="1.0.0"
  GIT_REVISION="${CFD_GIT_REV}")
