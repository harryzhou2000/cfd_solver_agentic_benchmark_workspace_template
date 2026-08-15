cmake_minimum_required(VERSION 3.20)

project(cfd_solver LANGUAGES CXX)

include(CTest)

set(_cfd_externals_default "")
if(DEFINED ENV{CFD_EXTERNALS_ROOT})
  set(_cfd_externals_default "$ENV{CFD_EXTERNALS_ROOT}")
endif()
set(CFD_EXTERNALS_ROOT "${_cfd_externals_default}" CACHE PATH
    "Prefix containing the CFD external dependency installation")
unset(_cfd_externals_default)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MPI REQUIRED COMPONENTS CXX)

find_path(CFD_CGNS_INCLUDE_DIR NAMES cgnslib.h
  HINTS "${CFD_EXTERNALS_ROOT}/include")
find_library(CFD_CGNS_LIBRARY NAMES cgns cgnsd
  HINTS "${CFD_EXTERNALS_ROOT}/lib" "${CFD_EXTERNALS_ROOT}/lib64")
if(NOT CFD_CGNS_INCLUDE_DIR OR NOT CFD_CGNS_LIBRARY)
  message(FATAL_ERROR
    "CGNS was not found. Set CFD_EXTERNALS_ROOT to its install prefix "
    "(expected include/cgnslib.h and lib/libcgns.*).")
endif()

find_path(CFD_METIS_INCLUDE_DIR NAMES metis.h
  HINTS "${CFD_EXTERNALS_ROOT}/include")
find_library(CFD_METIS_LIBRARY NAMES metis
  HINTS "${CFD_EXTERNALS_ROOT}/lib" "${CFD_EXTERNALS_ROOT}/lib64")
if(NOT CFD_METIS_INCLUDE_DIR OR NOT CFD_METIS_LIBRARY)
  message(FATAL_ERROR
    "METIS was not found. Set CFD_EXTERNALS_ROOT to its install prefix "
    "(expected include/metis.h and lib/libmetis.*).")
endif()

# CGNS may carry HDF5 transitively. Detect a direct HDF5 installation for
# diagnostics without rejecting valid self-contained CGNS installations.
find_package(HDF5 QUIET COMPONENTS CXX)

add_library(cfd_core
  src/Json.cpp
  src/CaseConfig.cpp
  src/Mesh.cpp
  src/DistributedMesh.cpp
  src/Physics.cpp
  src/Reconstruction.cpp
  src/SpatialOperator.cpp
  src/Output.cpp
  src/Solver.cpp)
add_library(cfd::core ALIAS cfd_core)

target_include_directories(cfd_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    "${CFD_CGNS_INCLUDE_DIR}"
    "${CFD_METIS_INCLUDE_DIR}")

target_link_libraries(cfd_core
  PUBLIC MPI::MPI_CXX
  PRIVATE
    "${CFD_CGNS_LIBRARY}"
    "${CFD_METIS_LIBRARY}")

if(HDF5_FOUND)
  target_link_libraries(cfd_core PRIVATE HDF5::HDF5)
endif()

target_compile_features(cfd_core PUBLIC cxx_std_17)

add_executable(cfd_solver src/main.cpp)
target_link_libraries(cfd_solver PRIVATE cfd::core)

if(BUILD_TESTING)
  add_executable(case_config_tests tests/CaseConfigTests.cpp)
  target_link_libraries(case_config_tests PRIVATE cfd::core)
  target_compile_definitions(case_config_tests PRIVATE
    CFD_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/cases")
  add_test(NAME case_config_tests COMMAND case_config_tests)

  add_executable(mesh_tests tests/test_mesh.cpp)
  target_link_libraries(mesh_tests PRIVATE cfd::core)
  add_test(NAME mesh_naca_tests
    COMMAND mesh_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns")
  add_test(NAME mesh_cylinder_tests
    COMMAND mesh_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/CylinderB1.cgns")

  add_executable(distributed_mesh_tests tests/test_distributed_mesh.cpp)
  target_link_libraries(distributed_mesh_tests PRIVATE cfd::core)
  add_test(NAME distributed_mesh_naca_tests
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2
            ${MPIEXEC_PREFLAGS} $<TARGET_FILE:distributed_mesh_tests>
            "${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns"
            ${MPIEXEC_POSTFLAGS})
  add_test(NAME distributed_mesh_cylinder_tests
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2
            ${MPIEXEC_PREFLAGS} $<TARGET_FILE:distributed_mesh_tests>
            "${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes/CylinderB1.cgns"
            ${MPIEXEC_POSTFLAGS})

  add_executable(physics_tests tests/PhysicsTests.cpp)
  target_link_libraries(physics_tests PRIVATE cfd::core)
  add_test(NAME physics_tests COMMAND physics_tests)

  add_executable(output_tests tests/OutputTests.cpp)
  target_link_libraries(output_tests PRIVATE cfd::core)
  add_test(NAME output_tests COMMAND output_tests)

  add_executable(spatial_operator_tests tests/SpatialOperatorTests.cpp)
  target_link_libraries(spatial_operator_tests PRIVATE cfd::core)
  add_test(NAME spatial_operator_tests COMMAND spatial_operator_tests)

  add_executable(halo_exchange_tests tests/HaloExchangeTests.cpp)
  target_link_libraries(halo_exchange_tests PRIVATE cfd::core)
  add_test(NAME halo_exchange_tests
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2
            ${MPIEXEC_PREFLAGS} $<TARGET_FILE:halo_exchange_tests> ${MPIEXEC_POSTFLAGS})

  add_executable(partition_failure_tests tests/PartitionFailureTests.cpp)
  target_link_libraries(partition_failure_tests PRIVATE cfd::core)
  add_test(NAME partition_failure_tests
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2
            ${MPIEXEC_PREFLAGS} $<TARGET_FILE:partition_failure_tests> ${MPIEXEC_POSTFLAGS})

  # C++ assert-based test oracles must remain active even when the solver itself
  # is built with Release flags containing NDEBUG.
  foreach(_cfd_test_target
      case_config_tests mesh_tests distributed_mesh_tests physics_tests output_tests
      spatial_operator_tests halo_exchange_tests partition_failure_tests)
    if(MSVC)
      target_compile_options(${_cfd_test_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${_cfd_test_target} PRIVATE -UNDEBUG)
    endif()
  endforeach()
endif()
