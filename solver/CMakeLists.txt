cmake_minimum_required(VERSION 3.16)
project(cfd2d LANGUAGES CXX)

# 2-D unstructured finite-volume compressible Navier-Stokes solver (C++17/MPI).
# Build:
#   cmake -S . -B build -DCFD_EXTERNALS_ROOT=<workspace>/external/cfd_externals/install
#   cmake --build build -j

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(MPI REQUIRED CXX)

# Root of compiled external libraries (DNDSR convention):
#   ${CFD_EXTERNALS_ROOT}/include, ${CFD_EXTERNALS_ROOT}/lib
if(NOT DEFINED CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
  else()
    set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
  endif()
endif()
get_filename_component(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT}" ABSOLUTE
                       BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

# Root of header-only packages (nlohmann_json lives in <root>/nlohmann).
if(NOT DEFINED CFD_HEADERONLY_ROOT)
  if(DEFINED ENV{CFD_HEADERONLY_ROOT})
    set(CFD_HEADERONLY_ROOT "$ENV{CFD_HEADERONLY_ROOT}")
  else()
    set(CFD_HEADERONLY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external")
  endif()
endif()
get_filename_component(CFD_HEADERONLY_ROOT "${CFD_HEADERONLY_ROOT}" ABSOLUTE
                       BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "CFD_HEADERONLY_ROOT = ${CFD_HEADERONLY_ROOT}")

find_library(CGNS_LIBRARY NAMES cgns PATHS "${CFD_EXTERNALS_ROOT}/lib")
find_library(HDF5_LIBRARY NAMES hdf5 PATHS "${CFD_EXTERNALS_ROOT}/lib")
find_library(METIS_LIBRARY NAMES metis PATHS "${CFD_EXTERNALS_ROOT}/lib")
find_library(Z_LIBRARY NAMES z PATHS "${CFD_EXTERNALS_ROOT}/lib")
foreach(lib CGNS HDF5 METIS Z)
  if(NOT ${lib}_LIBRARY)
    message(FATAL_ERROR "required library ${lib} not found under ${CFD_EXTERNALS_ROOT}/lib")
  endif()
endforeach()

set(CFD2D_SOURCES
  src/main.cpp
  src/config.cpp
  src/mesh.cpp
  src/partition.cpp
  src/halo.cpp
  src/flux.cpp
  src/reconstruction.cpp
  src/residual.cpp
  src/output.cpp
  src/driver.cpp
)

add_executable(cfd2d ${CFD2D_SOURCES})

target_include_directories(cfd2d PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CFD_EXTERNALS_ROOT}/include
  ${CFD_HEADERONLY_ROOT}/nlohmann
)

target_link_libraries(cfd2d PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIBRARY}
  ${HDF5_LIBRARY}
  ${METIS_LIBRARY}
  ${Z_LIBRARY}
  ${CMAKE_DL_LIBS}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cfd2d PRIVATE -Wall -Wextra -Wno-unused-parameter)
  option(CFD2D_NATIVE "Build with -march=native" OFF)
  if(CFD2D_NATIVE)
    target_compile_options(cfd2d PRIVATE -march=native)
  endif()
endif()

# RPATH so the executable finds libcgns/libhdf5/libmetis without env tweaks.
set_target_properties(cfd2d PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib"
)

install(TARGETS cfd2d RUNTIME DESTINATION bin)
