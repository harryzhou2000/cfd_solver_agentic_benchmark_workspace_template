cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependency root
if(NOT DEFINED CFD_EXTERNALS_ROOT)
add_compile_definitions(EIGEN_DONT_PARALLELIZE)
  set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install" CACHE PATH "CFD externals install root")
add_compile_definitions(EIGEN_DONT_PARALLELIZE)
endif()
set(EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/../external" CACHE PATH "Header-only libraries root")

# Find MPI
find_package(MPI REQUIRED)

# CGNS
set(CGNS_ROOT "${CFD_EXTERNALS_ROOT}")
add_compile_definitions(EIGEN_DONT_PARALLELIZE)
find_path(CGNS_INCLUDE_DIR cgnslib.h HINTS "${CGNS_ROOT}/include" REQUIRED)
find_library(CGNS_LIBRARY cgns HINTS "${CGNS_ROOT}/lib" REQUIRED)

# HDF5
set(HDF5_ROOT "${CFD_EXTERNALS_ROOT}")
add_compile_definitions(EIGEN_DONT_PARALLELIZE)
find_path(HDF5_INCLUDE_DIR hdf5.h HINTS "${HDF5_ROOT}/include" REQUIRED)
find_library(HDF5_LIBRARY hdf5 HINTS "${HDF5_ROOT}/lib" REQUIRED)

# METIS
set(METIS_ROOT "${CFD_EXTERNALS_ROOT}")
add_compile_definitions(EIGEN_DONT_PARALLELIZE)
find_path(METIS_INCLUDE_DIR metis.h HINTS "${METIS_ROOT}/include" REQUIRED)
find_library(METIS_LIBRARY metis HINTS "${METIS_ROOT}/lib" REQUIRED)

# Eigen (header-only)
set(EIGEN_DIR "${EXTERNAL_DIR}/eigen")

# nlohmann/json (header-only)
set(NLOHMANN_DIR "${EXTERNAL_DIR}/nlohmann")

# fmt (header-only)
set(FMT_DIR "${EXTERNAL_DIR}/fmt")

# argparse (header-only)
set(ARGPARSE_DIR "${EXTERNAL_DIR}/argparse")

# Compiler flags
add_compile_options(-O3 -march=native  -Wall -Wextra)

# Solver executable
add_executable(cfd_solver
  src/main.cpp
  src/mesh.cpp
  src/partition.cpp
  src/solver.cpp
  src/implicit.cpp
  src/transient.cpp
  src/output.cpp
)

target_include_directories(cfd_solver PRIVATE
  src
  ${CGNS_INCLUDE_DIR}
  ${HDF5_INCLUDE_DIR}
  ${METIS_INCLUDE_DIR}
  ${EIGEN_DIR}
  ${NLOHMANN_DIR}
  ${FMT_DIR}/include
  ${ARGPARSE_DIR}/include
)

target_link_libraries(cfd_solver PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIBRARY}
  ${HDF5_LIBRARY}
  ${METIS_LIBRARY}
)

target_compile_definitions(cfd_solver PRIVATE CGNS_ENABLE_HDF5)
