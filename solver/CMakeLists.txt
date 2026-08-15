cmake_minimum_required(VERSION 3.20)
project(CFDSolver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
# Suppress unavoidable warnings from external headers
add_compile_options(-Wno-unused-parameter -Wno-sign-compare)

# --- Dependencies ----------------------------------------------------------
set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install"
    CACHE PATH "Root of compiled external CFD libraries")
set(EXTERNAL_ROOT "${CMAKE_SOURCE_DIR}/../external"
    CACHE PATH "Root of header-only external libraries")

# MPI (Open MPI 5.0.9)
find_package(MPI REQUIRED)

# CGNS 4.5 (manual paths to avoid broken cmake transitive HDF5 deps)
set(CGNS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(CGNS_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libcgns.a")
message(STATUS "CGNS: using manual paths (${CGNS_LIBRARY})")

# HDF5 1.14.3 (has cmake config at cmake/; may have stale paths)
set(HDF5_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(HDF5_LIBRARIES "${CFD_EXTERNALS_ROOT}/lib/libhdf5.a" "${CFD_EXTERNALS_ROOT}/lib/libz.a")
message(STATUS "HDF5: using manual paths")

# METIS / ParMETIS (no cmake config, manual paths)
set(METIS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(METIS_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libmetis.so")
set(PARMETIS_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libparmetis.so")

find_library(DL_LIBRARY dl REQUIRED)

# Header-only libraries (exact paths from explorer)
set(EIGEN_INCLUDE_DIR "${EXTERNAL_ROOT}/eigen")                 # 5.0.1
set(FMT_INCLUDE_DIR "${EXTERNAL_ROOT}/fmt/include")             # 11.1.4
set(NLOHMANN_INCLUDE_DIR "${EXTERNAL_ROOT}/nlohmann")   # 3.12.0 (include <nlohmann/json.hpp>)
# argparse available at external/argparse/ (not currently used; CLI is hand-rolled)

# --- Solver Library --------------------------------------------------------
file(GLOB_RECURSE SOLVER_SOURCES
    "src/mesh/*.cpp"
    "src/partition/*.cpp"
    "src/solver/*.cpp"
    "src/output/*.cpp"
    "src/boundary/*.cpp"
    "src/io/*.cpp"
    "src/utils/*.cpp"
)

add_library(cfdsolver_lib STATIC ${SOLVER_SOURCES})
target_include_directories(cfdsolver_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CGNS_INCLUDE_DIR}
    ${HDF5_INCLUDE_DIR}
    ${METIS_INCLUDE_DIR}
    ${EIGEN_INCLUDE_DIR}
    ${FMT_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
)
target_link_libraries(cfdsolver_lib PUBLIC
    MPI::MPI_CXX
    ${CGNS_LIBRARY}
    ${HDF5_LIBRARIES}
    ${DL_LIBRARY}
    ${METIS_LIBRARY}
)

# --- Main Executable -------------------------------------------------------
add_executable(cfd_solver src/main.cpp)
target_link_libraries(cfd_solver PRIVATE cfdsolver_lib ${PARMETIS_LIBRARY})

# --- Tests -----------------------------------------------------------------
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
    add_executable(cfd_tests ${TEST_SOURCES})
    target_include_directories(cfd_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(cfd_tests PRIVATE cfdsolver_lib)
endif()
