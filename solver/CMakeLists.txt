cmake_minimum_required(VERSION 3.16)
project(cfd2d LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CFD_EXTERNALS_ROOT "" CACHE PATH "Path to cfd_externals install prefix")
set(EIGEN_ROOT "" CACHE PATH "Path to Eigen header-only library")
set(NLOHMANN_ROOT "" CACHE PATH "Path to nlohmann_json header-only library")
set(FMT_ROOT "" CACHE PATH "Path to fmt library")

find_package(MPI REQUIRED COMPONENTS CXX)

if(CFD_EXTERNALS_ROOT)
    list(APPEND CMAKE_PREFIX_PATH ${CFD_EXTERNALS_ROOT})
    include_directories(${CFD_EXTERNALS_ROOT}/include)
    link_directories(${CFD_EXTERNALS_ROOT}/lib)
endif()

if(EIGEN_ROOT)
    include_directories(${EIGEN_ROOT})
endif()

if(NLOHMANN_ROOT)
    include_directories(${NLOHMANN_ROOT})
endif()

if(FMT_ROOT)
    include_directories(${FMT_ROOT}/include)
endif()

add_executable(cfd2d
    src/main.cpp
    src/mesh.cpp
    src/partition.cpp
    src/solver.cpp
    src/output.cpp
    src/mpi_comm.cpp
)

target_include_directories(cfd2d PRIVATE src)
target_link_libraries(cfd2d PRIVATE MPI::MPI_CXX cgns hdf5 z metis parmetis)
target_compile_options(cfd2d PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
