cmake_minimum_required(VERSION 3.16)
project(fv2d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(MPI REQUIRED)

# External dependency root (DNDSR convention). Can be passed as
# -DCFD_EXTERNALS_ROOT=<path> or via the CFD_EXTERNALS_ROOT env var.
if(NOT CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
  else()
    set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
  endif()
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

if(NOT CFD_HEADER_ROOT)
  if(DEFINED ENV{CFD_HEADER_ROOT})
    set(CFD_HEADER_ROOT $ENV{CFD_HEADER_ROOT})
  else()
    set(CFD_HEADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external")
  endif()
endif()
message(STATUS "CFD_HEADER_ROOT = ${CFD_HEADER_ROOT}")

find_library(CGNS_LIB NAMES cgns PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH)
find_library(HDF5_LIB NAMES hdf5 PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH)
find_library(METIS_LIB NAMES metis PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH)
find_library(Z_LIB NAMES z PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH)

foreach(v CGNS_LIB HDF5_LIB METIS_LIB Z_LIB)
  if(NOT ${v})
    message(FATAL_ERROR "missing dependency: ${v} (root=${CFD_EXTERNALS_ROOT})")
  endif()
endforeach()

add_executable(fv2d
  src/main.cpp
  src/case_file.cpp
  src/mesh.cpp
  src/solver_core.cpp
  src/output.cpp
)

target_include_directories(fv2d PRIVATE
  ${CFD_EXTERNALS_ROOT}/include
  ${CFD_HEADER_ROOT}/nlohmann
  ${CFD_HEADER_ROOT}/eigen
)

target_link_libraries(fv2d PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIB}
  ${HDF5_LIB}
  ${METIS_LIB}
  ${Z_LIB}
  dl
)

set_target_properties(fv2d PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib"
)

target_compile_options(fv2d PRIVATE -O2 -Wall -Wextra)
