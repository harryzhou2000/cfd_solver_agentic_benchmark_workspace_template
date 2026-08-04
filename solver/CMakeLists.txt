cmake_minimum_required(VERSION 3.20)
project(SaturnCFD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MPI REQUIRED COMPONENTS CXX)

set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install"
    CACHE PATH "Root of the benchmark compiled CFD dependencies")
get_filename_component(CFD_EXTERNALS_ROOT "${CFD_EXTERNALS_ROOT}" ABSOLUTE)
set(CFD_HEADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external" CACHE PATH
    "Root of benchmark header-only dependencies")
get_filename_component(CFD_HEADER_ROOT "${CFD_HEADER_ROOT}" ABSOLUTE)

find_path(CGNS_INCLUDE_DIR cgnslib.h HINTS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
find_library(CGNS_LIBRARY NAMES cgns HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path(METIS_INCLUDE_DIR metis.h HINTS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
find_library(METIS_LIBRARY NAMES metis HINTS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp
          HINTS "${CFD_HEADER_ROOT}/nlohmann" NO_DEFAULT_PATH REQUIRED)

add_executable(saturn_cfd
  src/main.cpp
  src/case_config.cpp
  src/mesh.cpp
  src/partition.cpp
  src/physics.cpp
  src/solver.cpp
  src/output.cpp
)
target_include_directories(saturn_cfd PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CGNS_INCLUDE_DIR}"
  "${METIS_INCLUDE_DIR}"
  "${NLOHMANN_INCLUDE_DIR}"
)
target_link_libraries(saturn_cfd PRIVATE MPI::MPI_CXX "${CGNS_LIBRARY}" "${METIS_LIBRARY}")
target_compile_options(saturn_cfd PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Wnon-virtual-dtor>
)
set_target_properties(saturn_cfd PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib"
)

include(CTest)
if(BUILD_TESTING)
  add_executable(saturn_unit_tests tests/unit_tests.cpp src/physics.cpp src/case_config.cpp)
  target_include_directories(saturn_unit_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${NLOHMANN_INCLUDE_DIR}")
  target_link_libraries(saturn_unit_tests PRIVATE MPI::MPI_CXX)
  target_compile_options(saturn_unit_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
  )
  add_test(NAME saturn_unit_tests COMMAND saturn_unit_tests)
endif()
