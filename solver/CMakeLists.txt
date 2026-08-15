cmake_minimum_required(VERSION 3.18)
project(cfdns2d LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native -funroll-loops")

option(CFD_ENABLE_MPI "Enable MPI parallelism" ON)

# ---- External dependency root (DNDSR convention) ----
set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}" CACHE PATH "Root of cfd_externals install")
if(NOT CFD_EXTERNALS_ROOT)
  get_filename_component(_WS_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
  if(EXISTS "${_WS_ROOT}/external/cfd_externals/install")
    set(CFD_EXTERNALS_ROOT "${_WS_ROOT}/external/cfd_externals/install")
  endif()
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

# ---- MPI ----
find_package(MPI REQUIRED)

# ---- CGNS + HDF5 + METIS + zlib ----
set(CGNS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
find_library(CGNS_LIBRARY NAMES cgns PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(HDF5_LIBRARY NAMES hdf5 hdf5_serial PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(HDF5_HL_LIBRARY NAMES hdf5_hl PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY NAMES libz.a z zlib PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(METIS_LIBRARY NAMES metis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
set(METIS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")

# ---- Header-only libs ----
get_filename_component(_WS_ROOT2 "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(EIGEN_INCLUDE "${_WS_ROOT2}/external/eigen")
set(FMT_INCLUDE_DIR "${_WS_ROOT2}/external/fmt/include")
if(NOT EXISTS "${FMT_INCLUDE_DIR}")
  set(FMT_INCLUDE_DIR "${_WS_ROOT2}/external/fmt")
endif()
set(JSON_INCLUDE "${_WS_ROOT2}/external/nlohmann")

include_directories(
  ${CGNS_INCLUDE_DIR}
  ${METIS_INCLUDE_DIR}
  ${EIGEN_INCLUDE}
  ${FMT_INCLUDE_DIR}
  ${JSON_INCLUDE}
  ${CMAKE_SOURCE_DIR}/src
)

add_executable(cfdns2d
  src/main.cpp
  src/mesh.cpp
  src/solver.cpp
  src/io.cpp
)
target_compile_definitions(cfdns2d PRIVATE
  EIGEN_MPL2_ONLY
  FMT_HEADER_ONLY
)
target_link_libraries(cfdns2d PRIVATE
  ${CGNS_LIBRARY}
  ${HDF5_HL_LIBRARY}
  ${HDF5_LIBRARY}
  ${ZLIB_LIBRARY}
  ${METIS_LIBRARY}
  MPI::MPI_CXX
  m
)

install(TARGETS cfdns2d RUNTIME DESTINATION bin)
