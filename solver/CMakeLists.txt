cmake_minimum_required(VERSION 3.16)
project(cfd2d LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependency discovery. The benchmark provides an externals root with the
# DNDSR-style layout (include/, lib/, cmake/, ...) plus header-only packages
# as siblings under external/. CFD_EXTERNALS_ROOT is configurable.
# ---------------------------------------------------------------------------
set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install"
    CACHE PATH "Root of compiled CFD externals (include/,lib/,cmake/)")
set(EXT_ROOT "${CMAKE_SOURCE_DIR}/../external" CACHE PATH "Header-only externals root")

# ---- MPI ----
find_package(MPI REQUIRED)

# ---- CGNS + HDF5 + zlib (compiled externals) ----
find_library(CGNS_LIB cgns PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
find_library(HDF5_LIB hdf5 PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
find_library(HDF5_HL_LIB hdf5_hl PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
find_library(ZLIB_LIB z PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH REQUIRED)

# ---- METIS / ParMETIS (graph partitioning) ----
find_library(METIS_LIB metis PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
find_library(PARMETIS_LIB parmetis PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH)

# ---- Header-only: Eigen, fmt, nlohmann_json ----
set(EIGEN_INCLUDE "${EXT_ROOT}/eigen" CACHE PATH "Eigen include dir")
set(FMT_INCLUDE "${EXT_ROOT}/fmt/include" CACHE PATH "fmt include dir")
set(JSON_INCLUDE "${EXT_ROOT}/nlohmann" CACHE PATH "nlohmann_json include dir")

include_directories(
  ${CFD_EXTERNALS_ROOT}/include
  ${EIGEN_INCLUDE}
  ${FMT_INCLUDE}
  ${JSON_INCLUDE}
  ${CMAKE_SOURCE_DIR}/src
)

set(CFD_LIBS ${CGNS_LIB} ${HDF5_HL_LIB} ${HDF5_LIB} ${ZLIB_LIB} ${METIS_LIB})
if(PARMETIS_LIB)
  list(APPEND CFD_LIBS ${PARMETIS_LIB})
endif()

add_compile_options(
  -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
  -Wno-sign-compare -Wno-unused-but-set-variable -Wno-pedantic
  -fopenmp
)

# ---------------------------------------------------------------------------
# Solver executable. CLI: cfd2d solve --case <json> --output <dir>
# ---------------------------------------------------------------------------
add_executable(cfd2d
  src/types.cpp
  src/case.cpp
  src/mesh.cpp
  src/partition.cpp
  src/halo.cpp
  src/physics.cpp
  src/reconstruction.cpp
  src/residual.cpp
  src/implicit.cpp
  src/time_integrate.cpp
  src/io.cpp
  src/solver.cpp
  src/main.cpp
)

target_compile_definitions(cfd2d PRIVATE FMT_HEADER_ONLY _USE_MATH_DEFINES)
target_link_libraries(cfd2d PRIVATE ${CFD_LIBS} MPI::MPI_CXX)

# Optional mesh-inspection helper
add_executable(inspect_mesh src/tools/inspect_mesh.cpp)
target_link_libraries(inspect_mesh PRIVATE ${CFD_LIBS} MPI::MPI_CXX)

# Unit tests
enable_testing()
add_executable(unit_tests src/tools/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE ${CFD_LIBS} MPI::MPI_CXX)
add_test(NAME unit_tests COMMAND unit_tests)
