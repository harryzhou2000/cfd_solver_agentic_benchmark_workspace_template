cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

enable_testing()

# ---------------------------------------------------------------------------
# External dependency roots
# ---------------------------------------------------------------------------
# STRING (not PATH) cache type: PATH would auto-absolutize relative -D
# overrides against the build dir before the script can normalize them.
set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install"
    CACHE STRING "Root of compiled CFD externals (CGNS/METIS/ParMETIS/HDF5)")
set(EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/../external"
    CACHE STRING "Root of header-only external libraries (Eigen/nlohmann/fmt)")

# Normalize relative -D overrides against the source dir (not the build dir),
# so `cmake .. -DCFD_EXTERNALS_ROOT=../external/...` works as documented.
get_filename_component(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT}" ABSOLUTE
                       BASE_DIR "${CMAKE_SOURCE_DIR}")
get_filename_component(EXTERNAL_DIR "${EXTERNAL_DIR}" ABSOLUTE
                       BASE_DIR "${CMAKE_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# MPI (required)
# ---------------------------------------------------------------------------
find_package(MPI REQUIRED COMPONENTS CXX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${MPI_CXX_COMPILE_FLAGS}")

# ---------------------------------------------------------------------------
# HDF5: try the CMake package first, fall back to manual linking.
# Discovered before CGNS: the cgns-config imported target references an
# `hdf5-shared` target name from the machine that built CGNS, so we must be
# able to supply it.
# ---------------------------------------------------------------------------
find_package(HDF5 QUIET COMPONENTS C)
if(HDF5_FOUND)
  set(HDF5_LIBRARIES hdf5::hdf5)
  set(HDF5_FOUND_LIB "find_package(HDF5) (hdf5::hdf5)")
else()
  find_path(HDF5_INCLUDE_DIR hdf5.h
            HINTS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
  find_library(HDF5_LIBRARY NAMES hdf5
               HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
  if(HDF5_INCLUDE_DIR AND HDF5_LIBRARY)
    set(HDF5_LIBRARIES "${HDF5_LIBRARY}" -lz -ldl)
    set(HDF5_FOUND_LIB "manual (${HDF5_LIBRARY} -lz -ldl)")
  else()
    set(HDF5_LIBRARIES "")
    set(HDF5_FOUND_LIB "NOT FOUND (CGNS config may carry its own HDF5)")
  endif()
endif()

# ---------------------------------------------------------------------------
# CGNS: try the CMake package first, fall back to manual linking
# ---------------------------------------------------------------------------
set(CGNS_FOUND_LIB "")
if(EXISTS "${CFD_EXTERNALS_ROOT}/lib/cmake/cgns/cgns-config.cmake")
  find_package(cgns CONFIG QUIET
               HINTS "${CFD_EXTERNALS_ROOT}/lib/cmake/cgns" NO_DEFAULT_PATH)
endif()

if(cgns_FOUND AND TARGET CGNS::cgns_shared AND HDF5_LIBRARY)
  # The imported CGNS target expects an `hdf5-shared` target to exist; the
  # CMake package that produced it is not installed here, so supply one
  # pointing at the HDF5 library we just found.
  if(NOT TARGET hdf5-shared)
    add_library(hdf5-shared SHARED IMPORTED)
    set_target_properties(hdf5-shared PROPERTIES
                          IMPORTED_LOCATION "${HDF5_LIBRARY}")
  endif()
  set(CGNS_LIBRARIES CGNS::cgns_shared)
  set(CGNS_FOUND_LIB "cgns-config.cmake (CGNS::cgns_shared)")
else()
  find_path(CGNS_INCLUDE_DIR cgnslib.h
            HINTS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
  find_library(CGNS_LIBRARY NAMES cgns
               HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
  if(NOT CGNS_INCLUDE_DIR OR NOT CGNS_LIBRARY)
    message(FATAL_ERROR "CGNS not found: neither cgns-config.cmake nor "
            "libcgns in ${CFD_EXTERNALS_ROOT}")
  endif()
  set(CGNS_LIBRARIES "${CGNS_LIBRARY}" -lhdf5 -lz -ldl)
  set(CGNS_FOUND_LIB "manual (-lcgns -lhdf5 -lz -ldl)")
endif()

# ---------------------------------------------------------------------------
# METIS / ParMETIS (no CMake configs - manual linking)
# ---------------------------------------------------------------------------
find_library(METIS_LIBRARY NAMES metis
             HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(PARMETIS_LIBRARY NAMES parmetis
             HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
if(NOT METIS_LIBRARY OR NOT PARMETIS_LIBRARY)
  message(FATAL_ERROR "METIS/ParMETIS not found in ${CFD_EXTERNALS_ROOT}/lib")
endif()
set(METIS_LIBRARIES "${PARMETIS_LIBRARY}" "${METIS_LIBRARY}")

# ---------------------------------------------------------------------------
# Header-only libraries: Eigen, nlohmann/json, fmt
# ---------------------------------------------------------------------------
find_path(EIGEN_INCLUDE_DIR Eigen/Core
          HINTS "${EXTERNAL_DIR}/eigen" NO_DEFAULT_PATH)
find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp
          HINTS "${EXTERNAL_DIR}/nlohmann" NO_DEFAULT_PATH)
find_path(FMT_INCLUDE_DIR fmt/core.h
          HINTS "${EXTERNAL_DIR}/fmt/include" NO_DEFAULT_PATH)

foreach(_hdr EIGEN_INCLUDE_DIR NLOHMANN_INCLUDE_DIR FMT_INCLUDE_DIR)
  if(NOT ${_hdr})
    message(FATAL_ERROR "Header-only dependency '${_hdr}' not found under ${EXTERNAL_DIR}")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# Core library (all solver modules except main.cpp)
# ---------------------------------------------------------------------------
file(GLOB SOLVER_LIB_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM SOLVER_LIB_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(cfd_solver_lib STATIC ${SOLVER_LIB_SOURCES})

target_include_directories(cfd_solver_lib PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  ${MPI_CXX_INCLUDE_DIRS}
  ${CGNS_INCLUDE_DIR}
  ${HDF5_INCLUDE_DIR}
  "${CFD_EXTERNALS_ROOT}/include"
  "${EIGEN_INCLUDE_DIR}"
  "${NLOHMANN_INCLUDE_DIR}"
  "${FMT_INCLUDE_DIR}")

target_link_libraries(cfd_solver_lib PUBLIC
  ${MPI_CXX_LIBRARIES}
  ${CGNS_LIBRARIES}
  ${HDF5_LIBRARIES}
  ${METIS_LIBRARIES})

target_compile_options(cfd_solver_lib PUBLIC -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(cfd_solver "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(cfd_solver PRIVATE cfd_solver_lib)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(smoke_mpi "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp")
target_link_libraries(smoke_mpi PRIVATE cfd_solver_lib)

add_test(NAME smoke_mpi
         COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2
                 $<TARGET_FILE:smoke_mpi>)

add_executable(test_mesh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mesh.cpp")
target_link_libraries(test_mesh PRIVATE cfd_solver_lib)

add_test(NAME mesh_naca COMMAND test_mesh
         "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns")
add_test(NAME mesh_cylinder COMMAND test_mesh
         "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/CylinderB1.cgns")

add_executable(test_partition "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_partition.cpp")
target_link_libraries(test_partition PRIVATE cfd_solver_lib)

add_test(NAME partition_naca COMMAND test_partition
         "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns")
add_test(NAME partition_cylinder COMMAND test_partition
         "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/CylinderB1.cgns")

add_executable(test_halo "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_halo.cpp")
target_link_libraries(test_halo PRIVATE cfd_solver_lib)

add_test(NAME halo_naca
         COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4
                 $<TARGET_FILE:test_halo>
                 "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns")

add_executable(test_flux "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_flux.cpp")
target_link_libraries(test_flux PRIVATE cfd_solver_lib)

add_test(NAME flux_functions COMMAND test_flux)

add_executable(test_residual "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_residual.cpp")
target_link_libraries(test_residual PRIVATE cfd_solver_lib)

add_test(NAME residual_naca
         COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4
                 $<TARGET_FILE:test_residual>
                 "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns")
add_test(NAME residual_cylinder
         COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4
                 $<TARGET_FILE:test_residual>
                 "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/CylinderB1.cgns")

add_executable(test_forces "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_forces.cpp")
target_link_libraries(test_forces PRIVATE cfd_solver_lib)

add_test(NAME forces_naca
         COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4
                 $<TARGET_FILE:test_forces>
                 "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns")

add_executable(test_gradient "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gradient.cpp")
target_link_libraries(test_gradient PRIVATE cfd_solver_lib)

add_test(NAME gradient_lsq COMMAND test_gradient)

add_executable(test_limiter "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_limiter.cpp")
target_link_libraries(test_limiter PRIVATE cfd_solver_lib)

add_test(NAME limiter_barth COMMAND test_limiter)

add_executable(test_solver "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_solver.cpp")
target_link_libraries(test_solver PRIVATE cfd_solver_lib)

add_test(NAME solver_naca
         COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4
                 $<TARGET_FILE:test_solver>
                 "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns")

add_executable(test_transient "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_transient.cpp")
target_link_libraries(test_transient PRIVATE cfd_solver_lib)

add_test(NAME transient_cyl
         COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2
                 $<TARGET_FILE:test_transient>
                 "${CMAKE_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/CylinderB1.cgns")

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== cfd_solver dependency summary ===")
message(STATUS "  C++ standard      : C++17 (${CMAKE_CXX_COMPILER})")
message(STATUS "  MPI               : ${MPI_CXX_COMPILER} (OpenMPI via find_package)")
message(STATUS "  CFD_EXTERNALS_ROOT: ${CFD_EXTERNALS_ROOT}")
message(STATUS "  EXTERNAL_DIR      : ${EXTERNAL_DIR}")
message(STATUS "  CGNS              : ${CGNS_FOUND_LIB}")
message(STATUS "  HDF5              : ${HDF5_FOUND_LIB}")
message(STATUS "  METIS             : ${METIS_LIBRARY}")
message(STATUS "  ParMETIS          : ${PARMETIS_LIBRARY}")
message(STATUS "  Eigen             : ${EIGEN_INCLUDE_DIR}")
message(STATUS "  nlohmann/json     : ${NLOHMANN_INCLUDE_DIR}")
message(STATUS "  fmt               : ${FMT_INCLUDE_DIR}")
message(STATUS "  Tests             : smoke_mpi, mesh_naca, mesh_cylinder,")
message(STATUS "                     partition_naca, partition_cylinder, halo_naca,")
message(STATUS "                     flux_functions, residual_naca,")
message(STATUS "                     residual_cylinder, forces_naca,")
message(STATUS "                     gradient_lsq, limiter_barth, solver_naca")
message(STATUS "====================================")
message(STATUS "")
