cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# External dependency root: default follows the DNDSR convention.
if(NOT DEFINED CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
  else()
    set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
  endif()
endif()

find_package(MPI REQUIRED COMPONENTS CXX)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CFD_EXTERNALS_ROOT}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann
)

add_executable(cfd_solver
  src/main.cpp
  src/common.cpp
  src/case.cpp
  src/mesh.cpp
  src/partition.cpp
  src/physics.cpp
  src/reconstruction.cpp
  src/solver.cpp
  src/output.cpp
)

add_executable(mesh_diag
  tools/mesh_diag.cpp
  src/common.cpp
  src/case.cpp
  src/mesh.cpp
)

target_link_libraries(cfd_solver PRIVATE MPI::MPI_CXX cgns hdf5 metis z)
target_link_libraries(mesh_diag PRIVATE MPI::MPI_CXX cgns hdf5 metis z)
target_include_directories(cfd_solver PRIVATE ${CFD_EXTERNALS_ROOT}/include)
target_include_directories(mesh_diag PRIVATE ${CFD_EXTERNALS_ROOT}/include)
target_link_directories(cfd_solver PRIVATE ${CFD_EXTERNALS_ROOT}/lib)
target_link_directories(mesh_diag PRIVATE ${CFD_EXTERNALS_ROOT}/lib)

execute_process(
  COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR}/.. rev-parse HEAD
  OUTPUT_VARIABLE GIT_REV OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT GIT_REV)
  set(GIT_REV "unknown")
endif()
target_compile_definitions(cfd_solver PRIVATE
  GIT_REVISION="${GIT_REV}"
  SOLVER_VERSION="1.0.0")
