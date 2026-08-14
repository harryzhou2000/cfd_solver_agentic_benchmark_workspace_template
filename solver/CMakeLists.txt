cmake_minimum_required(VERSION 3.20)

project(cfd_foundation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CFD_EXTERNALS_ROOT "" CACHE PATH "Root of the installed CFD external libraries")
set(BENCHMARK_ROOT "" CACHE PATH "Root of cfd_solver_agentic_benchmark")

find_package(Git REQUIRED)
set(CFD_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${CFD_REPOSITORY_ROOT}" rev-parse HEAD
  OUTPUT_VARIABLE CFD_CONFIGURE_GIT_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE CFD_GIT_STATUS)
if(NOT CFD_GIT_STATUS EQUAL 0)
  message(FATAL_ERROR "Unable to read parent git revision")
endif()
message(STATUS "CFD parent git revision at configure: ${CFD_CONFIGURE_GIT_REVISION}")

set(CFD_PROVENANCE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cfd/provenance.hpp")
add_custom_target(cfd_provenance ALL
  COMMAND "${CMAKE_COMMAND}"
          "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
          "-DREPOSITORY_ROOT=${CFD_REPOSITORY_ROOT}"
          "-DOUTPUT_FILE=${CFD_PROVENANCE_HEADER}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_provenance.cmake"
  BYPRODUCTS "${CFD_PROVENANCE_HEADER}"
  COMMENT "Refreshing build provenance from parent git repository")

if(NOT CFD_EXTERNALS_ROOT)
  message(FATAL_ERROR "CFD_EXTERNALS_ROOT must name the external install prefix")
endif()

find_package(MPI REQUIRED COMPONENTS CXX)
find_path(CGNS_INCLUDE_DIR cgnslib.h HINTS "${CFD_EXTERNALS_ROOT}/include" REQUIRED NO_DEFAULT_PATH)
find_library(CGNS_LIBRARY NAMES cgns HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)
find_path(METIS_INCLUDE_DIR metis.h HINTS "${CFD_EXTERNALS_ROOT}/include" REQUIRED NO_DEFAULT_PATH)
find_library(METIS_LIBRARY NAMES metis HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
  HINTS
    "${CFD_EXTERNALS_ROOT}/include"
    "${CFD_EXTERNALS_ROOT}/../../nlohmann"
    "${CFD_EXTERNALS_ROOT}/../../../nlohmann"
  REQUIRED)

add_library(cfd_foundation
  src/boundary.cpp
  src/config.cpp
  src/flux.cpp
  src/gas.cpp
  src/mesh.cpp
  src/output.cpp
  src/partition.cpp
  src/reconstruction.cpp
  src/residual.cpp
  src/solver.cpp)
target_include_directories(cfd_foundation
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
          "${CGNS_INCLUDE_DIR}" "${METIS_INCLUDE_DIR}" "${NLOHMANN_JSON_INCLUDE_DIR}")
add_dependencies(cfd_foundation cfd_provenance)
target_link_libraries(cfd_foundation PUBLIC MPI::MPI_CXX "${CGNS_LIBRARY}" "${METIS_LIBRARY}")
target_compile_options(cfd_foundation PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)
set_target_properties(cfd_foundation PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib")

add_executable(cfd_solver src/main.cpp)
target_link_libraries(cfd_solver PRIVATE cfd_foundation)
target_include_directories(cfd_solver PRIVATE "${NLOHMANN_JSON_INCLUDE_DIR}")
target_compile_options(cfd_solver PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)
set_target_properties(cfd_solver PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib")

include(CTest)
if(BUILD_TESTING)
  if(NOT BENCHMARK_ROOT)
    message(FATAL_ERROR "BENCHMARK_ROOT is required when BUILD_TESTING=ON")
  endif()
  add_executable(foundation_tests tests/foundation_tests.cpp)
  target_link_libraries(foundation_tests PRIVATE cfd_foundation)
  target_include_directories(foundation_tests PRIVATE "${NLOHMANN_JSON_INCLUDE_DIR}")
  target_compile_definitions(foundation_tests PRIVATE BENCHMARK_ROOT="${BENCHMARK_ROOT}")
  target_compile_options(foundation_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)

  add_executable(numerics_tests tests/numerics_tests.cpp)
  target_link_libraries(numerics_tests PRIVATE cfd_foundation)
  target_compile_definitions(numerics_tests PRIVATE BENCHMARK_ROOT="${BENCHMARK_ROOT}")
  target_compile_options(numerics_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)

  add_executable(output_tests tests/output_tests.cpp)
  target_link_libraries(output_tests PRIVATE cfd_foundation)
  target_include_directories(output_tests PRIVATE "${NLOHMANN_JSON_INCLUDE_DIR}")
  target_compile_definitions(output_tests PRIVATE
    BENCHMARK_ROOT="${BENCHMARK_ROOT}"
    CFD_SOLVER_PATH="$<TARGET_FILE:cfd_solver>")
  target_compile_options(output_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>)

  add_test(NAME foundation_serial COMMAND foundation_tests)
  add_test(NAME numerics_serial COMMAND numerics_tests)
  add_test(NAME output_serial COMMAND output_tests)
  foreach(n IN ITEMS 2 8)
    add_test(NAME foundation_mpi_${n}
      COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${n}
              ${MPIEXEC_PREFLAGS} $<TARGET_FILE:foundation_tests> ${MPIEXEC_POSTFLAGS})
    add_test(NAME numerics_mpi_${n}
      COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${n}
              ${MPIEXEC_PREFLAGS} $<TARGET_FILE:numerics_tests> ${MPIEXEC_POSTFLAGS})
    add_test(NAME output_mpi_${n}
      COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${n}
              ${MPIEXEC_PREFLAGS} $<TARGET_FILE:output_tests> ${MPIEXEC_POSTFLAGS})
  endforeach()
  add_test(NAME cli_malformed COMMAND cfd_solver bad-command)
  set_tests_properties(cli_malformed PROPERTIES WILL_FAIL TRUE)
  add_test(NAME cli_missing_case
    COMMAND cfd_solver solve --case "${CMAKE_CURRENT_BINARY_DIR}/missing-case.json"
            --output "${CMAKE_CURRENT_BINARY_DIR}/missing-case-output")
  set_tests_properties(cli_missing_case PROPERTIES WILL_FAIL TRUE)
  add_test(NAME cli_smoke_diagnostic
    COMMAND cfd_solver solve
            --case "${BENCHMARK_ROOT}/inputs/cases/naca0012_m015_inviscid.json"
            --output "${CMAKE_CURRENT_BINARY_DIR}/cli-smoke-diagnostic"
            --max-steps 1 --report-level brief)
  set_tests_properties(cli_smoke_diagnostic PROPERTIES WILL_FAIL TRUE TIMEOUT 60)
  add_test(NAME cli_smoke_artifacts
    COMMAND output_tests --verify-cli-smoke
            "${CMAKE_CURRENT_BINARY_DIR}/cli-smoke-diagnostic")
  add_test(NAME cli_smoke_resume_budget
    COMMAND cfd_solver solve
            --case "${BENCHMARK_ROOT}/inputs/cases/naca0012_m015_inviscid.json"
            --output "${CMAKE_CURRENT_BINARY_DIR}/cli-smoke-diagnostic"
            --restart "${CMAKE_CURRENT_BINARY_DIR}/cli-smoke-diagnostic/restart_checkpoint.bin"
            --resume-output --max-steps 2 --report-level brief)
  set_tests_properties(cli_smoke_resume_budget PROPERTIES
    DEPENDS cli_smoke_diagnostic WILL_FAIL TRUE TIMEOUT 60)
  set_tests_properties(cli_smoke_artifacts PROPERTIES DEPENDS cli_smoke_resume_budget)
  set_tests_properties(foundation_serial foundation_mpi_2 foundation_mpi_8
    numerics_serial numerics_mpi_2 numerics_mpi_8
    output_serial output_mpi_2 output_mpi_8 cli_malformed cli_missing_case
    cli_smoke_diagnostic cli_smoke_resume_budget cli_smoke_artifacts PROPERTIES
    ENVIRONMENT "OMPI_ALLOW_RUN_AS_ROOT=1;OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1;OMPI_MCA_rmaps_base_oversubscribe=1")
endif()
