cmake_minimum_required(VERSION 3.20)
project(cfd_solver LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- MPI ----
find_package(MPI REQUIRED COMPONENTS CXX)

# ---- CFD externals root ----
if(NOT DEFINED CFD_EXTERNALS_ROOT)
    set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install")
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

# ---- CGNS / HDF5 ----
set(CGNS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(CGNS_LIBRARY_DIR "${CFD_EXTERNALS_ROOT}/lib")
find_library(CGNS_LIB cgns PATHS ${CGNS_LIBRARY_DIR} REQUIRED)
find_library(HDF5_LIB hdf5  PATHS ${CGNS_LIBRARY_DIR} REQUIRED)
find_library(Z_LIB   z     PATHS ${CGNS_LIBRARY_DIR} REQUIRED)

# ---- METIS / ParMETIS ----
find_library(METIS_LIB   metis   PATHS ${CGNS_LIBRARY_DIR} REQUIRED)
find_library(PARMETIS_LIB parmetis PATHS ${CGNS_LIBRARY_DIR})

# ---- Header-only packages ----
set(EIGEN_DIR        "${CMAKE_SOURCE_DIR}/../external/eigen")
set(FMT_DIR          "${CMAKE_SOURCE_DIR}/../external/fmt/include")
set(NLOHMANN_DIR     "${CMAKE_SOURCE_DIR}/../external/nlohmann")
set(ARGPARSE_DIR     "${CMAKE_SOURCE_DIR}/../external/argparse")

# ---- Solver sources ----
set(SOLVER_SOURCES
    src/main.cpp
    src/common/types.hpp
    src/common/gas_model.hpp
    src/io/json_reader.hpp
    src/io/json_reader.cpp
    src/mesh/cgns_reader.hpp
    src/mesh/cgns_reader.cpp
    src/mesh/mesh.hpp
    src/mesh/mesh.cpp
    src/mesh/geometry.hpp
    src/mesh/geometry.cpp
    src/partition/metis_partitioner.hpp
    src/partition/metis_partitioner.cpp
    src/partition/mpi_distributor.hpp
    src/partition/mpi_distributor.cpp
    src/partition/halo_exchange.hpp
    src/partition/halo_exchange.cpp
    src/solver/residual.hpp
    src/solver/residual.cpp
    src/solver/gradients.hpp
    src/solver/gradients.cpp
    src/solver/reconstruct.hpp
    src/solver/reconstruct.cpp
    src/solver/limiter.hpp
    src/solver/limiter.cpp
    src/solver/inviscid_flux.hpp
    src/solver/inviscid_flux.cpp
    src/solver/viscous_flux.hpp
    src/solver/viscous_flux.cpp
    src/solver/boundary.hpp
    src/solver/boundary.cpp
    src/solver/implicit_solver.hpp
    src/solver/implicit_solver.cpp
    src/solver/time_stepper.hpp
    src/solver/time_stepper.cpp
    src/solver/solver_setup.hpp
    src/solver/solver_setup.cpp
    src/output/csv_writer.hpp
    src/output/csv_writer.cpp
    src/output/vtk_writer.hpp
    src/output/vtk_writer.cpp
    src/output/metadata.hpp
    src/output/metadata.cpp
)

add_executable(cfd_solver ${SOLVER_SOURCES})

# ---- Include directories ----
target_include_directories(cfd_solver PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${EIGEN_DIR}
    ${FMT_DIR}
    ${NLOHMANN_DIR}
    ${CGNS_INCLUDE_DIR}
)

# ---- Link libraries ----
target_link_libraries(cfd_solver PUBLIC
    MPI::MPI_CXX
    ${CGNS_LIB}
    ${HDF5_LIB}
    ${Z_LIB}
    ${METIS_LIB}
)

if(PARMETIS_LIB)
    target_link_libraries(cfd_solver PUBLIC ${PARMETIS_LIB})
endif()

# ---- Compile definitions ----
target_compile_definitions(cfd_solver PRIVATE
    SOLVER_HAS_PARALLEL_METIS=$<BOOL:${PARMETIS_LIB}>
)

# ---- Compiler flags ----
target_compile_options(cfd_solver PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -O3 -march=native>
)
