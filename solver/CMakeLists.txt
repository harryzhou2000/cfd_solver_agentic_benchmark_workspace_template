cmake_minimum_required(VERSION 3.18)
project(cfd_solver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -funroll-loops")

# External dependency root (DNDSR convention).
set(CFD_EXTERNALS_ROOT "/workspace/external/cfd_externals/install" CACHE PATH "cfd externals install root")
set(EXT_INCLUDE ${CFD_EXTERNALS_ROOT}/include)
set(EXT_LIB ${CFD_EXTERNALS_ROOT}/lib)

# Header-only packages live under /workspace/external/<pkg>/...
set(HDR_ROOT "/workspace/external")
include_directories(
  ${EXT_INCLUDE}
  ${HDR_ROOT}/eigen
  ${HDR_ROOT}/fmt/include
  ${HDR_ROOT}/nlohmann
  ${HDR_ROOT}/argparse/include
  ${HDR_ROOT}/boost
)
link_directories(${EXT_LIB})

find_package(MPI REQUIRED)
find_package(HDF5 REQUIRED NO_MODULE_PATH)
# CGNS, METIS, ParMETIS are provided as static/shared libs in EXT_LIB.

add_executable(cfd_solver
  src/main.cpp
  src/mesh.cpp
  src/partition.cpp
  src/solver.cpp
  src/io.cpp
)
target_include_directories(cfd_solver PRIVATE src ${EXT_INCLUDE})
target_link_libraries(cfd_solver PRIVATE
  MPI::MPI_CXX
  ${EXT_LIB}/libcgns.so
  ${EXT_LIB}/libhdf5.so
  ${EXT_LIB}/libmetis.so
  ${EXT_LIB}/libparmetis.so
  ${EXT_LIB}/libz.so
)
target_compile_options(cfd_solver PRIVATE -Wall -Wno-unused-parameter)

# Mesh reader smoke test (no MPI needed).
add_executable(mesh_test src/mesh_test.cpp src/mesh.cpp)
target_include_directories(mesh_test PRIVATE src ${EXT_INCLUDE} ${HDR_ROOT}/nlohmann)
target_link_libraries(mesh_test PRIVATE ${EXT_LIB}/libcgns.so ${EXT_LIB}/libhdf5.so ${EXT_LIB}/libz.so)
