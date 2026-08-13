cmake_minimum_required(VERSION 3.20)

project(AeroFV VERSION 0.1.0 LANGUAGES CXX)

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  RESULT_VARIABLE AEROFV_GIT_RESULT
  OUTPUT_VARIABLE AEROFV_GIT_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT AEROFV_GIT_RESULT EQUAL 0)
  set(AEROFV_GIT_REVISION "unknown")
else()
  execute_process(
    COMMAND git diff --quiet --ignore-submodules --
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE AEROFV_GIT_DIRTY
    ERROR_QUIET
  )
  if(NOT AEROFV_GIT_DIRTY EQUAL 0)
    string(APPEND AEROFV_GIT_REVISION "-dirty")
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AEROFV_BUILD_TESTS "Build AeroFV unit and integration tests" ON)
set(CFD_EXTERNALS_ROOT "" CACHE PATH
    "Optional DNDSR-style prefix containing include/, lib/, and cmake/")

find_package(MPI REQUIRED COMPONENTS CXX)

set(_dependency_include_hints)
set(_dependency_library_hints)
if(CFD_EXTERNALS_ROOT)
  list(APPEND _dependency_include_hints "${CFD_EXTERNALS_ROOT}/include")
  list(APPEND _dependency_library_hints "${CFD_EXTERNALS_ROOT}/lib"
       "${CFD_EXTERNALS_ROOT}/lib64")
endif()

find_path(CGNS_INCLUDE_DIR NAMES cgnslib.h HINTS ${_dependency_include_hints}
          REQUIRED)
find_library(CGNS_LIBRARY NAMES cgns HINTS ${_dependency_library_hints}
             REQUIRED)
find_path(METIS_INCLUDE_DIR NAMES metis.h HINTS ${_dependency_include_hints}
          REQUIRED)
find_library(METIS_LIBRARY NAMES metis HINTS ${_dependency_library_hints}
             REQUIRED)

add_library(aerofv_core
  src/case_config.cpp
  src/cli.cpp
  src/halo.cpp
  src/mesh.cpp
  src/numerics.cpp
  src/output.cpp
  src/partition.cpp
  src/physics.cpp
  src/solver.cpp
)
target_include_directories(aerofv_core
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  PRIVATE "${CGNS_INCLUDE_DIR}" "${METIS_INCLUDE_DIR}"
)
target_link_libraries(aerofv_core
  PUBLIC MPI::MPI_CXX
  PRIVATE "${CGNS_LIBRARY}" "${METIS_LIBRARY}"
)
target_compile_definitions(aerofv_core PUBLIC
  AEROFV_VERSION="${PROJECT_VERSION}"
  AEROFV_GIT_REVISION="${AEROFV_GIT_REVISION}"
)
target_compile_options(aerofv_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

add_executable(aerofv src/main.cpp)
target_link_libraries(aerofv PRIVATE aerofv_core)

include(GNUInstallDirs)
install(TARGETS aerofv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(AEROFV_BUILD_TESTS)
  enable_testing()
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

  add_test(NAME generate_report_python
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_generate_report.py")

  add_executable(test_case_config tests/test_case_config.cpp)
  target_link_libraries(test_case_config PRIVATE aerofv_core)
  add_test(NAME case_config
    COMMAND test_case_config "${CMAKE_CURRENT_SOURCE_DIR}/inputs/cases")

  add_executable(test_physics tests/test_physics.cpp)
  target_link_libraries(test_physics PRIVATE aerofv_core)
  add_test(NAME physics COMMAND test_physics)

  add_executable(test_reconstruction_order tests/test_reconstruction_order.cpp)
  target_link_libraries(test_reconstruction_order PRIVATE aerofv_core)
  add_test(NAME reconstruction_order COMMAND test_reconstruction_order)

  add_executable(test_steady_recovery tests/test_steady_recovery.cpp)
  target_link_libraries(test_steady_recovery PRIVATE aerofv_core)
  add_test(NAME steady_recovery COMMAND test_steady_recovery)

  add_executable(test_mesh tests/test_mesh.cpp)
  target_link_libraries(test_mesh PRIVATE aerofv_core)
  add_test(NAME mesh_naca
    COMMAND test_mesh "${CMAKE_CURRENT_SOURCE_DIR}/inputs/meshes/NACA0012_H2.cgns")
  add_test(NAME mesh_cylinder
    COMMAND test_mesh "${CMAKE_CURRENT_SOURCE_DIR}/inputs/meshes/CylinderB1.cgns")

  add_executable(test_partition_mpi tests/test_partition_mpi.cpp)
  target_link_libraries(test_partition_mpi PRIVATE aerofv_core)
  add_test(NAME partition_mpi
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4
            $<TARGET_FILE:test_partition_mpi>
            "${CMAKE_CURRENT_SOURCE_DIR}/inputs/meshes/CylinderB1.cgns")

  add_executable(test_halo_mpi tests/test_halo_mpi.cpp)
  target_link_libraries(test_halo_mpi PRIVATE aerofv_core)
  add_test(NAME halo_mpi
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4
            $<TARGET_FILE:test_halo_mpi>)
endif()
