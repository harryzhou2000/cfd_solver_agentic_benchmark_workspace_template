cmake_minimum_required(VERSION 3.16)
project(cfd2d LANGUAGES CXX C)

set(SOLVER_NAME "cfd2d-unstructured")
set(SOLVER_VERSION "1.0.0")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------- dependencies
# Locations follow the DNDSR external-resource convention.  Both roots can be
# overridden on the command line or through environment variables.
if(NOT CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
  else()
    set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install")
  endif()
endif()
if(NOT CFD_HEADER_ROOT)
  if(DEFINED ENV{CFD_HEADER_ROOT})
    set(CFD_HEADER_ROOT $ENV{CFD_HEADER_ROOT})
  else()
    set(CFD_HEADER_ROOT "${CMAKE_SOURCE_DIR}/../external")
  endif()
endif()
get_filename_component(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT}" ABSOLUTE)
get_filename_component(CFD_HEADER_ROOT "${CFD_HEADER_ROOT}" ABSOLUTE)
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")
message(STATUS "CFD_HEADER_ROOT    = ${CFD_HEADER_ROOT}")

find_package(MPI REQUIRED COMPONENTS CXX)

find_path(CGNS_INCLUDE_DIR cgnslib.h HINTS "${CFD_EXTERNALS_ROOT}/include" REQUIRED)
find_library(CGNS_LIBRARY NAMES cgns HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(HDF5_C_LIBRARY NAMES hdf5 HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_library(ZLIB_LIBRARY NAMES z HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)
find_path(METIS_INCLUDE_DIR metis.h HINTS "${CFD_EXTERNALS_ROOT}/include" REQUIRED)
find_library(METIS_LIBRARY NAMES metis HINTS "${CFD_EXTERNALS_ROOT}/lib" REQUIRED)

find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp
          HINTS "${CFD_HEADER_ROOT}/nlohmann" "${CFD_EXTERNALS_ROOT}/include" REQUIRED)
find_path(EIGEN_INCLUDE_DIR signature_of_eigen3_matrix_library
          HINTS "${CFD_HEADER_ROOT}/eigen" "${CFD_EXTERNALS_ROOT}/include/eigen3")

message(STATUS "CGNS    : ${CGNS_LIBRARY}")
message(STATUS "HDF5    : ${HDF5_C_LIBRARY}")
message(STATUS "METIS   : ${METIS_LIBRARY}")
message(STATUS "nlohmann: ${NLOHMANN_INCLUDE_DIR}")

# ---------------------------------------------------------------- version info
find_package(Git QUIET)
set(SOLVER_GIT_REVISION "unknown")
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE SOLVER_GIT_REVISION
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
endif()
configure_file(${CMAKE_SOURCE_DIR}/src/core/Version.hpp.in
               ${CMAKE_BINARY_DIR}/generated/core/Version.hpp @ONLY)

# ---------------------------------------------------------------- solver library
add_library(cfd2d_core
  src/core/Log.cpp
  src/core/CaseConfig.cpp
  src/mesh/GlobalMesh.cpp
  src/mesh/CgnsReader.cpp
  src/mesh/Partitioner.cpp
  src/mesh/LocalMesh.cpp
  src/mesh/MeshDistributor.cpp
  src/parallel/Comm.cpp
  src/numerics/SpatialOperator.cpp
  src/numerics/ImplicitSolver.cpp
  src/numerics/RunLoop.cpp
  src/numerics/Verification.cpp
  src/post/Forces.cpp
  src/io/OutputWriter.cpp
)
target_include_directories(cfd2d_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/generated
  ${CGNS_INCLUDE_DIR}
  ${METIS_INCLUDE_DIR}
  ${NLOHMANN_INCLUDE_DIR}
)
if(EIGEN_INCLUDE_DIR)
  target_include_directories(cfd2d_core PUBLIC ${EIGEN_INCLUDE_DIR})
endif()
target_link_libraries(cfd2d_core PUBLIC
  MPI::MPI_CXX
  ${CGNS_LIBRARY}
  ${HDF5_C_LIBRARY}
  ${ZLIB_LIBRARY}
  ${METIS_LIBRARY}
)
target_compile_options(cfd2d_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(cfd2d src/main.cpp)
target_link_libraries(cfd2d PRIVATE cfd2d_core)
target_compile_options(cfd2d PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---------------------------------------------------------------- unit tests
enable_testing()
find_path(DOCTEST_INCLUDE_DIR doctest.h
          HINTS "${CFD_HEADER_ROOT}/doctest" "${CFD_HEADER_ROOT}/doctest/doctest"
                "${CFD_EXTERNALS_ROOT}/include/doctest")
if(DOCTEST_INCLUDE_DIR)
  add_executable(cfd2d_tests tests/unit_tests.cpp)
  target_include_directories(cfd2d_tests PRIVATE ${DOCTEST_INCLUDE_DIR})
  target_link_libraries(cfd2d_tests PRIVATE cfd2d_core)
  add_test(NAME unit COMMAND cfd2d_tests)
else()
  message(STATUS "doctest not found; unit tests disabled")
endif()

# Parallel-consistency regression test (needs mpirun and the Python venv).
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/mpi_consistency.sh)
  add_test(NAME mpi_consistency
           COMMAND ${CMAKE_SOURCE_DIR}/tests/mpi_consistency.sh
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(mpi_consistency PROPERTIES TIMEOUT 900)
endif()

# CGNS structure dump helper used while developing the mesh reader.
add_executable(cgns_inspect tools/cgns_inspect.c)
target_include_directories(cgns_inspect PRIVATE ${CGNS_INCLUDE_DIR})
target_link_libraries(cgns_inspect PRIVATE ${CGNS_LIBRARY} ${HDF5_C_LIBRARY} ${ZLIB_LIBRARY} m)
