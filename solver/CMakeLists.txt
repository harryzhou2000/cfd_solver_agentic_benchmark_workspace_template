cmake_minimum_required(VERSION 3.16)
project(cfd2d CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# External deps root
set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install"
    CACHE PATH "Root of compiled external libs")
set(EIGEN_ROOT "${CMAKE_SOURCE_DIR}/../external/eigen"
    CACHE PATH "Eigen header root")
set(FMT_ROOT "${CMAKE_SOURCE_DIR}/../external/fmt"
    CACHE PATH "fmt header root")
set(JSON_ROOT "${CMAKE_SOURCE_DIR}/../external/nlohmann"
    CACHE PATH "nlohmann json header root")
set(ARGPARSE_ROOT "${CMAKE_SOURCE_DIR}/../external/argparse"
    CACHE PATH "argparse header root")

find_package(MPI REQUIRED)

# Include paths
include_directories(
    ${CFD_EXTERNALS_ROOT}/include
    ${EIGEN_ROOT}
    ${FMT_ROOT}/include
    ${JSON_ROOT}
    ${ARGPARSE_ROOT}/include
    ${MPI_CXX_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)

link_directories(${CFD_EXTERNALS_ROOT}/lib)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(cfd2d ${SOURCES})

target_link_libraries(cfd2d
    ${MPI_CXX_LIBRARIES}
    cgns
    hdf5
    metis
    parmetis
    z
    dl
    m
)

target_compile_definitions(cfd2d PRIVATE FMT_HEADER_ONLY=1)
target_compile_options(cfd2d PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-cast-function-type)
