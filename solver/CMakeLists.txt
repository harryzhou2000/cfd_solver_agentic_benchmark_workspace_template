cmake_minimum_required(VERSION 3.18)
project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(MPI REQUIRED)

# ---------------------------------------------------------------------------
# External dependencies
# ---------------------------------------------------------------------------
# Root of the prebuilt external libraries (CGNS, HDF5, METIS, zlib).
# Declared as STRING so that relative -D values are stored verbatim
# (PATH-type cache variables would be pre-resolved against the build
# directory, losing the intended source-relative meaning).
set(CFD_EXTERNALS_ROOT "../external/cfd_externals/install"
    CACHE STRING "Root of the prebuilt external libraries")
# Resolve relative paths against the project source directory so that the
# default works regardless of where CMake is invoked from. As a fallback,
# also try the build directory-relative interpretation.
set(CFD_EXTERNALS_ROOT_SRC "${CFD_EXTERNALS_ROOT}")
set(CFD_EXTERNALS_ROOT_BIN "${CFD_EXTERNALS_ROOT}")
if(NOT IS_ABSOLUTE "${CFD_EXTERNALS_ROOT}")
  set(CFD_EXTERNALS_ROOT_SRC
      "${CMAKE_CURRENT_SOURCE_DIR}/${CFD_EXTERNALS_ROOT}")
  set(CFD_EXTERNALS_ROOT_BIN
      "${CMAKE_CURRENT_BINARY_DIR}/${CFD_EXTERNALS_ROOT}")
endif()
get_filename_component(CFD_EXTERNALS_ROOT_SRC
                       "${CFD_EXTERNALS_ROOT_SRC}" ABSOLUTE)
get_filename_component(CFD_EXTERNALS_ROOT_BIN
                       "${CFD_EXTERNALS_ROOT_BIN}" ABSOLUTE)
if(EXISTS "${CFD_EXTERNALS_ROOT_BIN}")
  set(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT_BIN}")
else()
  set(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT_SRC}")
endif()
unset(CFD_EXTERNALS_ROOT_SRC)
unset(CFD_EXTERNALS_ROOT_BIN)

set(CFD_EXTERNALS_LIB_DIR "${CFD_EXTERNALS_ROOT}/lib")
set(CFD_EXTERNALS_INC_DIR "${CFD_EXTERNALS_ROOT}/include")

find_library(CGNS_LIBRARY NAMES cgns PATHS "${CFD_EXTERNALS_LIB_DIR}"
             NO_DEFAULT_PATH REQUIRED)
find_library(HDF5_LIBRARY NAMES hdf5 PATHS "${CFD_EXTERNALS_LIB_DIR}"
             NO_DEFAULT_PATH REQUIRED)
find_library(METIS_LIBRARY NAMES metis PATHS "${CFD_EXTERNALS_LIB_DIR}"
             NO_DEFAULT_PATH REQUIRED)
find_library(ZLIB_LIBRARY NAMES z PATHS "${CFD_EXTERNALS_LIB_DIR}"
             NO_DEFAULT_PATH REQUIRED)

find_path(CGNS_INCLUDE_DIR NAMES cgnslib.h PATHS "${CFD_EXTERNALS_INC_DIR}"
          NO_DEFAULT_PATH REQUIRED)
find_path(HDF5_INCLUDE_DIR NAMES hdf5.h PATHS "${CFD_EXTERNALS_INC_DIR}"
          NO_DEFAULT_PATH REQUIRED)
find_path(METIS_INCLUDE_DIR NAMES metis.h PATHS "${CFD_EXTERNALS_INC_DIR}"
          NO_DEFAULT_PATH REQUIRED)
find_path(ZLIB_INCLUDE_DIR NAMES zlib.h PATHS "${CFD_EXTERNALS_INC_DIR}"
          NO_DEFAULT_PATH REQUIRED)

# Header-only libraries (Eigen, fmt, nlohmann_json).
set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external")
set(EIGEN_INCLUDE_DIR "${EXTERNAL_DIR}/eigen")
set(FMT_INCLUDE_DIR "${EXTERNAL_DIR}/fmt/include")
set(NLOHMANN_INCLUDE_DIR "${EXTERNAL_DIR}/nlohmann")

# ---------------------------------------------------------------------------
# Solver executable
# ---------------------------------------------------------------------------
add_executable(cfd_solver
  src/main.cpp
  src/mesh_reader.cpp
  src/geometry.cpp
  src/case_parser.cpp
  src/adjacency.cpp
  src/partition.cpp
  src/halo_exchange.cpp
  src/flux.cpp
  src/boundary.cpp
  src/reconstruction.cpp
  src/limiter.cpp
  src/residual.cpp
  src/time_integrator.cpp
  src/output_writer.cpp
)

target_include_directories(cfd_solver PRIVATE
  include
  ${CGNS_INCLUDE_DIR}
  ${HDF5_INCLUDE_DIR}
  ${METIS_INCLUDE_DIR}
  ${ZLIB_INCLUDE_DIR}
  ${EIGEN_INCLUDE_DIR}
  ${FMT_INCLUDE_DIR}
  ${NLOHMANN_INCLUDE_DIR}
)

target_compile_options(cfd_solver PRIVATE -Wall -Wextra)

# Use fmt in header-only mode (we consume the prebuilt headers without
# building libfmt).
target_compile_definitions(cfd_solver PRIVATE FMT_HEADER_ONLY)

target_link_libraries(cfd_solver PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIBRARY}
  ${HDF5_LIBRARY}
  ${METIS_LIBRARY}
  ${ZLIB_LIBRARY}
  m
)

# Make the prebuilt shared libraries (libcgns, libhdf5, ...) findable at
# runtime regardless of the LD_LIBRARY_PATH.
set_target_properties(cfd_solver PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_LIB_DIR}"
  INSTALL_RPATH "${CFD_EXTERNALS_LIB_DIR}"
)
