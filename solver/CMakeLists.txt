cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
add_compile_options(-Wall -Wextra)

# ---- External dependencies -------------------------------------------------
# The benchmark follows the DNDSR external-dependency convention.  The build
# accepts the install root through -DCFD_EXTERNALS_ROOT=<path> or the
# environment variable of the same name.
set(CFD_EXTERNALS_ROOT "" CACHE PATH "Root of compiled external libraries (cgns/hdf5/metis)")
if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
endif()
if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
endif()

find_package(MPI REQUIRED COMPONENTS CXX)

find_path(CGNS_INCLUDE_DIR cgnslib.h HINTS "${CFD_EXTERNALS_ROOT}/include" REQUIRED)
find_library(CGNS_LIBRARY cgns HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(HDF5_LIBRARY hdf5 HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(Z_LIBRARY z HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(METIS_LIBRARY metis HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)

find_path(EIGEN_INCLUDE_DIR Eigen/Core HINTS "${CFD_EXTERNALS_ROOT}/../eigen"
  "${CMAKE_CURRENT_SOURCE_DIR}/../external/eigen" REQUIRED)
find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp
  HINTS "${CFD_EXTERNALS_ROOT}/../nlohmann"
  "${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann" REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CGNS_INCLUDE_DIR} ${EIGEN_INCLUDE_DIR} ${NLOHMANN_INCLUDE_DIR}
  ${MPI_CXX_INCLUDE_DIRS})

add_executable(cfd_solver
  src/main.cpp
  src/case.cpp
  src/mesh.cpp
  src/partition.cpp
  src/gradient.cpp
  src/flux.cpp
  src/solver.cpp
  src/output.cpp
  src/restart.cpp
)
target_link_libraries(cfd_solver
  ${MPI_CXX_LIBRARIES}
  ${CGNS_LIBRARY} ${HDF5_LIBRARY} ${Z_LIBRARY}
  ${METIS_LIBRARY}
)
target_link_directories(cfd_solver PRIVATE "${CFD_EXTERNALS_ROOT}/lib")
set_target_properties(cfd_solver PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib")

# ---- Tests ------------------------------------------------------------------
enable_testing()
add_executable(mesh_probe tests/mesh_probe.cpp src/mesh.cpp)
target_link_libraries(mesh_probe ${MPI_CXX_LIBRARIES} ${CGNS_LIBRARY} ${HDF5_LIBRARY} ${Z_LIBRARY})
target_sources(mesh_probe PRIVATE src/case.cpp)
target_link_directories(mesh_probe PRIVATE "${CFD_EXTERNALS_ROOT}/lib")
set_target_properties(mesh_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(partition_probe tests/partition_probe.cpp src/mesh.cpp src/partition.cpp)
target_link_libraries(partition_probe ${MPI_CXX_LIBRARIES} ${CGNS_LIBRARY} ${HDF5_LIBRARY}
                      ${Z_LIBRARY} ${METIS_LIBRARY})
target_sources(partition_probe PRIVATE src/case.cpp)
target_link_directories(partition_probe PRIVATE "${CFD_EXTERNALS_ROOT}/lib")
set_target_properties(partition_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
