cmake_minimum_required(VERSION 3.16)
project(cfd_agentic_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
endif()

find_package(MPI REQUIRED COMPONENTS CXX)

find_library(CGNS_LIBRARY cgns PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_path(CGNS_INCLUDE_DIR cgnslib.h PATHS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
if(NOT CGNS_LIBRARY OR NOT CGNS_INCLUDE_DIR)
  message(FATAL_ERROR "CGNS not found under ${CFD_EXTERNALS_ROOT}")
endif()

find_library(METIS_LIBRARY metis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_path(METIS_INCLUDE_DIR metis.h PATHS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
if(NOT METIS_LIBRARY OR NOT METIS_INCLUDE_DIR)
  message(FATAL_ERROR "METIS not found under ${CFD_EXTERNALS_ROOT}")
endif()

find_library(HDF5_LIBRARY hdf5 PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_path(HDF5_INCLUDE_DIR hdf5.h PATHS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
if(NOT HDF5_LIBRARY OR NOT HDF5_INCLUDE_DIR)
  message(FATAL_ERROR "HDF5 not found under ${CFD_EXTERNALS_ROOT}")
endif()

add_executable(cfd_solver
  src/main.cpp
  src/io.cpp
  src/mesh.cpp
  src/partition.cpp
  src/fluxes.cpp
  src/solver.cpp
  src/driver.cpp
  src/output.cpp
)

target_include_directories(cfd_solver PRIVATE
  src
  ${CGNS_INCLUDE_DIR}
  ${METIS_INCLUDE_DIR}
  ${HDF5_INCLUDE_DIR}
  ${CFD_EXTERNALS_ROOT}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../external/eigen
  ${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann
  ${CMAKE_CURRENT_SOURCE_DIR}/../external/argparse/include
)

target_compile_options(cfd_solver PRIVATE -O2 -Wall -Wextra)

target_link_libraries(cfd_solver PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIBRARY}
  ${METIS_LIBRARY}
  ${HDF5_LIBRARY}
  z
  m
)

add_executable(mesh_probe src/mesh_probe.cpp src/mesh.cpp)
target_include_directories(mesh_probe PRIVATE
  src
  ${CGNS_INCLUDE_DIR}
  ${METIS_INCLUDE_DIR}
  ${HDF5_INCLUDE_DIR}
  ${CFD_EXTERNALS_ROOT}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../external/eigen
)
target_link_libraries(mesh_probe PRIVATE
  ${CGNS_LIBRARY}
  ${HDF5_LIBRARY}
  z
  m
)
