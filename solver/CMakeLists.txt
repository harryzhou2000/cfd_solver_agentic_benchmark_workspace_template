cmake_minimum_required(VERSION 3.16)
project(cfd_fv2d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

find_package(MPI REQUIRED)

# External dependency root: -DCFD_EXTERNALS_ROOT=... or the env variable.
if(NOT DEFINED CFD_EXTERNALS_ROOT)
    if(DEFINED ENV{CFD_EXTERNALS_ROOT})
        set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
    else()
        set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
    endif()
endif()

find_path(CGNS_INCLUDE_DIR cgnslib.h
          HINTS "${CFD_EXTERNALS_ROOT}/include")
find_library(CGNS_LIBRARY NAMES cgns
             HINTS "${CFD_EXTERNALS_ROOT}/lib")
find_path(HDF5_INCLUDE_DIR hdf5.h
          HINTS "${CFD_EXTERNALS_ROOT}/include")
find_library(HDF5_LIBRARY NAMES hdf5
             HINTS "${CFD_EXTERNALS_ROOT}/lib")
find_library(ZLIB_LIBRARY NAMES z
             HINTS "${CFD_EXTERNALS_ROOT}/lib")
find_path(METIS_INCLUDE_DIR metis.h
          HINTS "${CFD_EXTERNALS_ROOT}/include")
find_library(METIS_LIBRARY NAMES metis
             HINTS "${CFD_EXTERNALS_ROOT}/lib")
find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp
          HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann")

if(NOT CGNS_INCLUDE_DIR OR NOT CGNS_LIBRARY OR NOT HDF5_INCLUDE_DIR OR
   NOT HDF5_LIBRARY OR NOT METIS_INCLUDE_DIR OR NOT METIS_LIBRARY OR
   NOT NLOHMANN_INCLUDE_DIR)
    message(FATAL_ERROR
        "Missing external dependencies under ${CFD_EXTERNALS_ROOT} "
        "(pass -DCFD_EXTERNALS_ROOT=<path>).")
endif()

add_executable(cfd_fv2d
    src/main.cpp
    src/case_input.cpp
    src/global_mesh.cpp
    src/partition.cpp
    src/distributed_mesh.cpp
    src/physics.cpp
    src/solver.cpp)

target_include_directories(cfd_fv2d PRIVATE
    "${CGNS_INCLUDE_DIR}"
    "${HDF5_INCLUDE_DIR}"
    "${METIS_INCLUDE_DIR}"
    "${NLOHMANN_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(cfd_fv2d PRIVATE
    MPI::MPI_CXX
    "${CGNS_LIBRARY}"
    "${HDF5_LIBRARY}"
    "${ZLIB_LIBRARY}"
    "${METIS_LIBRARY}")

target_link_directories(cfd_fv2d PRIVATE "${CFD_EXTERNALS_ROOT}/lib")
set_target_properties(cfd_fv2d PROPERTIES
    BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
    INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib")
