cmake_minimum_required(VERSION 3.20)
project(CFDSolver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Options ---
option(ENABLE_MPI "Build with MPI support" ON)
set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install"
    CACHE PATH "Root of compiled external libraries (CGNS, HDF5, METIS, etc.)")

# --- Paths to header-only libraries (relative to solver/) ---
set(EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/../external")
set(EIGEN3_DIR     "${EXTERNAL_DIR}/eigen")
set(NLOHMANN_DIR   "${EXTERNAL_DIR}/nlohmann")
set(FMT_DIR        "${EXTERNAL_DIR}/fmt/include")
set(ARGPARSE_DIR   "${EXTERNAL_DIR}/argparse/include")
set(NANOFLANN_DIR  "${EXTERNAL_DIR}/nanoflann/include")
set(EXPRTK_DIR     "${EXTERNAL_DIR}/exprtk")

# fmt is used header-only (no libfmt is linked).
add_definitions(-DFMT_HEADER_ONLY)

# --- MPI ---
if(ENABLE_MPI)
    find_package(MPI REQUIRED)
    add_definitions(-DUSE_MPI)
endif()

# --- CGNS (with HDF5) ---
# NO_DEFAULT_PATH: the runner-provided externals root must strictly win over
# any system-installed CGNS/HDF5/METIS (e.g. /usr/local/lib).
find_library(CGNS_LIB cgns PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(HDF5_LIB hdf5 PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path(CGNS_INCLUDE_DIR cgnslib.h PATHS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
add_definitions(-DUSE_CGNS)

# --- METIS ---
find_library(METIS_LIB metis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path(METIS_INCLUDE_DIR metis.h PATHS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
add_definitions(-DUSE_METIS)

# --- ParMETIS (optional) ---
find_library(PARMETIS_LIB parmetis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_path(PARMETIS_INCLUDE_DIR parmetis.h PATHS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
if(PARMETIS_LIB AND PARMETIS_INCLUDE_DIR)
    add_definitions(-DUSE_PARMETIS)
    message(STATUS "ParMETIS found")
else()
    message(STATUS "ParMETIS not found — using serial METIS for preprocessing")
endif()

# --- Zlib ---
find_library(ZLIB_LIB z PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)

# --- Source files ---
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# --- Executable ---
add_executable(cfd_solver ${SRC_FILES})

target_include_directories(cfd_solver PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${EIGEN3_DIR}"
    "${NLOHMANN_DIR}"
    "${FMT_DIR}"
    "${ARGPARSE_DIR}"
    "${NANOFLANN_DIR}"
    "${CGNS_INCLUDE_DIR}"
    "${CFD_EXTERNALS_ROOT}/include"  # HDF5 headers are co-located with CGNS
    "${METIS_INCLUDE_DIR}"
)

if(PARMETIS_INCLUDE_DIR)
    target_include_directories(cfd_solver PRIVATE "${PARMETIS_INCLUDE_DIR}")
endif()

target_link_libraries(cfd_solver PRIVATE
    ${CGNS_LIB}
    ${HDF5_LIB}
    ${METIS_LIB}
    ${ZLIB_LIB}
)

if(PARMETIS_LIB)
    target_link_libraries(cfd_solver PRIVATE ${PARMETIS_LIB})
endif()

if(ENABLE_MPI)
    target_link_libraries(cfd_solver PRIVATE MPI::MPI_CXX)
endif()

# --- Compiler flags ---
target_compile_options(cfd_solver PRIVATE -Wall -Wextra -Wpedantic -O3 -march=native)

# --- Install ---
install(TARGETS cfd_solver DESTINATION bin)
