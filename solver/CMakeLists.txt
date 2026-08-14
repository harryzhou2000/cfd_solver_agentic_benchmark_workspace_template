cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ---------------------------------------------------------------------------
# External dependencies (DNDSR convention)
# ---------------------------------------------------------------------------
if(NOT DEFINED CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install"
      CACHE PATH "Root of compiled external libraries")
endif()

if(NOT DEFINED EXTERNAL_HEADERS_ROOT)
  set(EXTERNAL_HEADERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external"
      CACHE PATH "Root of header-only external packages")
endif()

find_package(MPI REQUIRED COMPONENTS CXX)

add_library(cfd_externals INTERFACE)
target_include_directories(cfd_externals INTERFACE
  "${CFD_EXTERNALS_ROOT}/include"
  "${EXTERNAL_HEADERS_ROOT}/nlohmann"
)
# CGNS was built with 64-bit sizes (CGNS_64BIT=1 in cgnsBuild.defs).
target_compile_definitions(cfd_externals INTERFACE CG_BUILD_64BIT)

find_library(CGNS_LIB cgns PATHS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(HDF5_LIB hdf5 PATHS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(HDF5_HL_LIB hdf5_hl PATHS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(METIS_LIB metis PATHS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(ZLIB_LIB z PATHS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)

target_link_libraries(cfd_externals INTERFACE
  ${CGNS_LIB} ${HDF5_LIB} ${HDF5_HL_LIB} ${METIS_LIB} ${ZLIB_LIB} dl m)

# ---------------------------------------------------------------------------
# Solver library
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOLVER_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(cfd_solver_lib STATIC ${SOLVER_SOURCES})
target_include_directories(cfd_solver_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cfd_solver_lib PUBLIC cfd_externals MPI::MPI_CXX)
target_compile_options(cfd_solver_lib PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(cfd_solver src/main.cpp)
target_link_libraries(cfd_solver PRIVATE cfd_solver_lib)

# Mesh inspection tool (development / diagnostics).
add_executable(mesh_info tools/mesh_info.cpp)
target_link_libraries(mesh_info PRIVATE cfd_solver_lib)

install(TARGETS cfd_solver mesh_info RUNTIME DESTINATION bin)
