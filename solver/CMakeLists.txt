cmake_minimum_required(VERSION 3.16)
project(cfd_benchmark_solver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

find_package(MPI REQUIRED COMPONENTS CXX)

set(CFD_EXTERNALS_ROOT "" CACHE PATH "Root of compiled externals (include/, lib/)")
if(NOT CFD_EXTERNALS_ROOT AND DEFINED ENV{CFD_EXTERNALS_ROOT})
  set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
endif()
if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install)
endif()
get_filename_component(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT}" ABSOLUTE)
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

set(CFD_HEADERONLY_ROOT "" CACHE PATH "Root of header-only packages (eigen, nlohmann, ...)")
if(NOT CFD_HEADERONLY_ROOT AND DEFINED ENV{CFD_HEADERONLY_ROOT})
  set(CFD_HEADERONLY_ROOT $ENV{CFD_HEADERONLY_ROOT})
endif()
if(NOT CFD_HEADERONLY_ROOT)
  set(CFD_HEADERONLY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../external)
endif()
get_filename_component(CFD_HEADERONLY_ROOT "${CFD_HEADERONLY_ROOT}" ABSOLUTE)
message(STATUS "CFD_HEADERONLY_ROOT = ${CFD_HEADERONLY_ROOT}")

find_library(CGNS_LIB NAMES cgns PATHS ${CFD_EXTERNALS_ROOT}/lib REQUIRED)
find_library(HDF5_LIB NAMES hdf5 PATHS ${CFD_EXTERNALS_ROOT}/lib REQUIRED)
find_library(HDF5_HL_LIB NAMES hdf5_hl PATHS ${CFD_EXTERNALS_ROOT}/lib)
find_library(METIS_LIB NAMES metis PATHS ${CFD_EXTERNALS_ROOT}/lib REQUIRED)
find_library(Z_LIB NAMES z PATHS ${CFD_EXTERNALS_ROOT}/lib)

add_executable(cfdsolve
  src/main.cpp
  src/case_config.cpp
  src/cgns_mesh.cpp
  src/distribute.cpp
  src/solver.cpp
  src/output.cpp
)

target_include_directories(cfdsolve PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CFD_EXTERNALS_ROOT}/include
  ${CFD_HEADERONLY_ROOT}
  ${CFD_HEADERONLY_ROOT}/nlohmann
  ${CFD_HEADERONLY_ROOT}/eigen
)

target_link_libraries(cfdsolve PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIB}
  ${HDF5_HL_LIB}
  ${HDF5_LIB}
  ${METIS_LIB}
  ${Z_LIB}
)

add_executable(meshinfo tools/meshinfo.cpp)
target_include_directories(meshinfo PRIVATE ${CFD_EXTERNALS_ROOT}/include)
target_link_libraries(meshinfo PRIVATE ${CGNS_LIB} ${HDF5_HL_LIB} ${HDF5_LIB} ${Z_LIB})
