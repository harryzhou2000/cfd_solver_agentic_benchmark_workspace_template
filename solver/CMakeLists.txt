cmake_minimum_required(VERSION 3.16)
project(fv2d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- MPI ----
find_package(MPI REQUIRED COMPONENTS CXX)

# ---- External dependency root (DNDSR convention) ----
if(NOT CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
  else()
    set(CFD_EXTERNALS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install)
  endif()
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

set(EXT_INCLUDE ${CFD_EXTERNALS_ROOT}/include)
set(EXT_LIB ${CFD_EXTERNALS_ROOT}/lib)

# Header-only packages relative to workspace external/ dir
if(NOT CFD_HEADER_ONLY_ROOT)
  set(CFD_HEADER_ONLY_ROOT ${CFD_EXTERNALS_ROOT}/..)
endif()
message(STATUS "CFD_HEADER_ONLY_ROOT = ${CFD_HEADER_ONLY_ROOT}")

find_library(CGNS_LIB NAMES cgns PATHS ${EXT_LIB} NO_DEFAULT_PATH REQUIRED)
find_library(HDF5_LIB NAMES hdf5 PATHS ${EXT_LIB} NO_DEFAULT_PATH REQUIRED)
find_library(HDF5HL_LIB NAMES hdf5_hl PATHS ${EXT_LIB} NO_DEFAULT_PATH)
find_library(METIS_LIB NAMES metis PATHS ${EXT_LIB} NO_DEFAULT_PATH REQUIRED)
find_library(Z_LIB NAMES z PATHS ${EXT_LIB} NO_DEFAULT_PATH)

add_executable(fv2d
  src/main.cpp
  src/case_file.cpp
  src/mesh.cpp
  src/partition.cpp
  src/flux.cpp
  src/solver.cpp
  src/output.cpp
)

target_include_directories(fv2d PRIVATE
  ${EXT_INCLUDE}
  ${CFD_HEADER_ONLY_ROOT}
  ${CFD_HEADER_ONLY_ROOT}/nlohmann
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(fv2d PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIB}
  ${HDF5HL_LIB}
  ${HDF5_LIB}
  ${METIS_LIB}
  ${Z_LIB}
  dl
)

target_compile_options(fv2d PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
set_target_properties(fv2d PROPERTIES
  BUILD_RPATH ${EXT_LIB}
  INSTALL_RPATH ${EXT_LIB}
)
