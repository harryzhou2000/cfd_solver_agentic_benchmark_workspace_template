cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ---------------------------------------------------------------------------
# External dependencies.  The benchmark convention provides a DNDSR-style
# install tree; the location can be supplied via -DCFD_EXTERNALS_ROOT=<path>
# or the CFD_EXTERNALS_ROOT environment variable.  Header-only packages
# (nlohmann_json) are picked up from the same tree when present.
# ---------------------------------------------------------------------------
if(NOT DEFINED CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
  else()
    set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
  endif()
endif()
get_filename_component(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT}" ABSOLUTE)
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

find_package(MPI REQUIRED COMPONENTS CXX)

# Header-only packages -----------------------------------------------------------------
set(EXTERNAL_HEADER_DIRS "")
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann")
  list(APPEND EXTERNAL_HEADER_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann")
endif()
if(IS_DIRECTORY "${CFD_EXTERNALS_ROOT}/include")
  list(APPEND EXTERNAL_HEADER_DIRS "${CFD_EXTERNALS_ROOT}/include")
endif()

find_library(CGNS_LIB cgns PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(HDF5_LIB hdf5 PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(ZLIB_LIB z PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(METIS_LIB metis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(PARMETIS_LIB parmetis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)

if(NOT CGNS_LIB OR NOT HDF5_LIB OR NOT METIS_LIB)
  message(FATAL_ERROR "CGNS/HDF5/METIS libraries not found under ${CFD_EXTERNALS_ROOT}/lib")
endif()

add_executable(cfd_solver
  src/main.cpp
  src/config.cpp
  src/mesh.cpp
  src/partition.cpp
  src/physics.cpp
  src/solver.cpp
  src/output.cpp
  src/krylov.cpp
)

target_include_directories(cfd_solver PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${EXTERNAL_HEADER_DIRS}
)
target_link_libraries(cfd_solver PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIB} ${HDF5_LIB} ${ZLIB_LIB}
  ${METIS_LIB}
)
if(PARMETIS_LIB)
  target_link_libraries(cfd_solver PRIVATE ${PARMETIS_LIB})
  target_compile_definitions(cfd_solver PRIVATE HAVE_PARMETIS=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cfd_solver PRIVATE -Wall -Wextra)
endif()

install(TARGETS cfd_solver RUNTIME DESTINATION bin)
