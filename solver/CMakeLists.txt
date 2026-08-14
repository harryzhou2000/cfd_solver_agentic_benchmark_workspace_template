cmake_minimum_required(VERSION 3.16)
project(benchmark_cfd_solver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

find_package(MPI REQUIRED)

set(CFD_EXTERNALS_ROOT "" CACHE PATH "Root of compiled external libraries (install prefix)")
if(NOT CFD_EXTERNALS_ROOT AND DEFINED ENV{CFD_EXTERNALS_ROOT})
  set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
endif()
if(NOT CFD_EXTERNALS_ROOT)
  get_filename_component(_ws "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  set(CFD_EXTERNALS_ROOT "${_ws}/external/cfd_externals/install")
endif()
message(STATUS "CFD_EXTERNALS_ROOT=${CFD_EXTERNALS_ROOT}")

set(CFD_HEADERONLY_ROOT "" CACHE PATH "Root containing header-only packages (nlohmann, eigen, ...)")
if(NOT CFD_HEADERONLY_ROOT AND DEFINED ENV{CFD_HEADERONLY_ROOT})
  set(CFD_HEADERONLY_ROOT "$ENV{CFD_HEADERONLY_ROOT}")
endif()
if(NOT CFD_HEADERONLY_ROOT)
  get_filename_component(_ws "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  set(CFD_HEADERONLY_ROOT "${_ws}/external")
endif()
message(STATUS "CFD_HEADERONLY_ROOT=${CFD_HEADERONLY_ROOT}")

find_library(CGNS_LIB NAMES cgns PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(HDF5_LIB NAMES hdf5 PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(METIS_LIB NAMES metis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(Z_LIB NAMES z PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)

add_executable(cfd_solver
  src/main.cpp
  src/case_file.cpp
  src/mesh.cpp
  src/partition.cpp
  src/solver.cpp
  src/output.cpp
)

target_include_directories(cfd_solver PRIVATE
  src
  "${CFD_EXTERNALS_ROOT}/include"
  "${CFD_HEADERONLY_ROOT}/nlohmann"
  ${MPI_CXX_INCLUDE_DIRS}
)

target_link_libraries(cfd_solver PRIVATE
  ${CGNS_LIB}
  ${HDF5_LIB}
  ${METIS_LIB}
  MPI::MPI_CXX
)
if(Z_LIB)
  target_link_libraries(cfd_solver PRIVATE ${Z_LIB})
endif()

target_compile_options(cfd_solver PRIVATE -Wall -Wextra)

execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE CFD_GIT_REV
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(CFD_GIT_REV)
  target_compile_definitions(cfd_solver PRIVATE CFD_GIT_REVISION="${CFD_GIT_REV}")
endif()
