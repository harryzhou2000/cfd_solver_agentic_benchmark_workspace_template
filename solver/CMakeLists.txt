cmake_minimum_required(VERSION 3.16)
project(cfdns2d CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native -ffast-math -funroll-loops")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer")

if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
endif()
if(CFD_EXTERNALS_ROOT STREQUAL "")
  set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install")
endif()

if(NOT EXTERNAL_HEADER_ROOT)
  set(EXTERNAL_HEADER_ROOT "$ENV{EXTERNAL_HEADER_ROOT}")
endif()
if(EXTERNAL_HEADER_ROOT STREQUAL "")
  set(EXTERNAL_HEADER_ROOT "${CMAKE_SOURCE_DIR}/../external")
endif()

message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")
message(STATUS "EXTERNAL_HEADER_ROOT = ${EXTERNAL_HEADER_ROOT}")

find_package(MPI REQUIRED)

set(CGNS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(CGNS_LIBRARY     "${CFD_EXTERNALS_ROOT}/lib/libcgns.so")
if(NOT EXISTS "${CGNS_LIBRARY}")
  set(CGNS_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libcgns.a")
endif()

set(HDF5_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(HDF5_LIBRARY     "${CFD_EXTERNALS_ROOT}/lib/libhdf5.so")
if(NOT EXISTS "${HDF5_LIBRARY}")
  set(HDF5_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libhdf5.a")
endif()
set(HDF5_HL_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libhdf5_hl.so")
if(NOT EXISTS "${HDF5_HL_LIBRARY}")
  set(HDF5_HL_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libhdf5_hl.a")
endif()

set(METIS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(METIS_LIBRARY     "${CFD_EXTERNALS_ROOT}/lib/libmetis.so")
if(NOT EXISTS "${METIS_LIBRARY}")
  set(METIS_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libmetis.a")
endif()

set(ZLIB_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libz.so")
if(NOT EXISTS "${ZLIB_LIBRARY}")
  set(ZLIB_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libz.a")
endif()

set(EIGEN_INCLUDE_DIR     "${EXTERNAL_HEADER_ROOT}/eigen")
set(FMT_INCLUDE_DIR       "${EXTERNAL_HEADER_ROOT}/fmt/include")
set(NLOHMANN_INCLUDE_DIR  "${EXTERNAL_HEADER_ROOT}/nlohmann")

include_directories(
  ${CMAKE_SOURCE_DIR}/src
  ${CGNS_INCLUDE_DIR}
  ${HDF5_INCLUDE_DIR}
  ${METIS_INCLUDE_DIR}
  ${EIGEN_INCLUDE_DIR}
  ${FMT_INCLUDE_DIR}
  ${NLOHMANN_INCLUDE_DIR}
)

link_directories(${CFD_EXTERNALS_ROOT}/lib)

add_executable(cfd2d
  src/main.cpp
  src/mesh.cpp
  src/partition.cpp
  src/solver.cpp
  src/flux.cpp
  src/reconstruction.cpp
  src/implicit.cpp
  src/output.cpp
  src/case_def.cpp
)

target_link_libraries(cfd2d
  ${METIS_LIBRARY}
  ${CGNS_LIBRARY}
  ${HDF5_HL_LIBRARY}
  ${HDF5_LIBRARY}
  ${ZLIB_LIBRARY}
  MPI::MPI_CXX
  stdc++fs
)

target_compile_definitions(cfd2d PRIVATE
  CGNS_ENABLE_64BIT
  EIGEN_NO_DEBUG
)
target_compile_options(cfd2d PRIVATE -Wall -Wextra)

set_target_properties(cfd2d PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib"
)
