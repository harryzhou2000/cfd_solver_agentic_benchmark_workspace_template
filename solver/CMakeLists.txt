cmake_minimum_required(VERSION 3.16)
project(cfd2d_solver CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native -DNDEBUG")
if(NOT DEFINED CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install")
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")
set(EXT_INC "${CFD_EXTERNALS_ROOT}/include")
set(EXT_LIB "${CFD_EXTERNALS_ROOT}/lib")
set(EIGEN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external/eigen")
set(FMT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external/fmt/include")
set(NLOHMANN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external/nlohmann")
find_package(MPI REQUIRED)
find_library(CGNS_LIB NAMES cgns PATHS ${EXT_LIB} REQUIRED)
find_library(HDF5_LIB NAMES hdf5 PATHS ${EXT_LIB} REQUIRED)
find_library(METIS_LIB NAMES metis PATHS ${EXT_LIB} REQUIRED)
find_library(Z_LIB NAMES z PATHS ${EXT_LIB} REQUIRED)
add_executable(cfd2d
  src/main.cpp
  src/mesh.cpp
  src/partition.cpp
  src/solver.cpp
  src/output.cpp
)
target_include_directories(cfd2d PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${EXT_INC}
  ${EIGEN_INCLUDE_DIR}
  ${FMT_INCLUDE_DIR}
  ${NLOHMANN_INCLUDE_DIR}
)
target_link_libraries(cfd2d PRIVATE
  ${CGNS_LIB}
  ${HDF5_LIB}
  ${METIS_LIB}
  ${Z_LIB}
  MPI::MPI_CXX
  dl
)
target_compile_definitions(cfd2d PRIVATE CGNS_BUILD_64BIT OMPI_SKIP_MPICXX)
