cmake_minimum_required(VERSION 3.16)
project(fv2d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# DNDSR-style externals root: compiled libraries (CGNS, HDF5, METIS, ...)
if(DEFINED ENV{CFD_EXTERNALS_ROOT} AND NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
endif()
if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT ${CMAKE_SOURCE_DIR}/../external/cfd_externals/install)
endif()

# Header-only packages root (eigen, nlohmann, fmt, ...)
if(DEFINED ENV{CFD_HEADER_ROOT} AND NOT CFD_HEADER_ROOT)
  set(CFD_HEADER_ROOT $ENV{CFD_HEADER_ROOT})
endif()
if(NOT CFD_HEADER_ROOT)
  set(CFD_HEADER_ROOT ${CMAKE_SOURCE_DIR}/../external)
endif()

message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")
message(STATUS "CFD_HEADER_ROOT    = ${CFD_HEADER_ROOT}")

find_package(MPI REQUIRED COMPONENTS CXX)

add_executable(fv2d
  src/main.cpp
  src/case_config.cpp
  src/global_mesh.cpp
  src/partition.cpp
  src/local_mesh.cpp
  src/grad.cpp
  src/flux.cpp
  src/jacobian.cpp
  src/solver.cpp
  src/solver_run.cpp
  src/gmres.cpp
  src/output.cpp
)

# embed git revision when available
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND \${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY \${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_REV OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(GIT_REV)
    target_compile_definitions(fv2d PRIVATE GIT_REVISION="\${GIT_REV}")
  endif()
endif()

target_include_directories(fv2d PRIVATE
  src
  ${CFD_EXTERNALS_ROOT}/include
  ${CFD_HEADER_ROOT}/nlohmann
)

target_link_directories(fv2d PRIVATE ${CFD_EXTERNALS_ROOT}/lib)
target_link_libraries(fv2d PRIVATE MPI::MPI_CXX cgns metis hdf5 z)
target_compile_options(fv2d PRIVATE -O3 -Wall -Wno-unused-parameter)
set_target_properties(fv2d PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib")
