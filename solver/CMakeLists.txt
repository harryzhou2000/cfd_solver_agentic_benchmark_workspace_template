cmake_minimum_required(VERSION 3.18)

project(agentic_cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
  else()
    get_filename_component(_workspace_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    set(CFD_EXTERNALS_ROOT "${_workspace_root}/external/cfd_externals/install")
  endif()
endif()

find_package(MPI REQUIRED COMPONENTS CXX)

set(CFD_EXTERNALS_INCLUDE "${CFD_EXTERNALS_ROOT}/include")
set(CFD_EXTERNALS_LIB "${CFD_EXTERNALS_ROOT}/lib")

add_library(cfd_solver_core
  src/case_config.cpp
  src/cgns_mesh.cpp
  src/mesh_geometry.cpp
  src/partition.cpp
  src/state.cpp
  src/fvm.cpp
  src/output.cpp
  src/solver.cpp
)

target_include_directories(cfd_solver_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CFD_EXTERNALS_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann"
)

target_link_directories(cfd_solver_core PUBLIC "${CFD_EXTERNALS_LIB}")
target_link_libraries(cfd_solver_core PUBLIC MPI::MPI_CXX cgns hdf5 metis z)

target_compile_options(cfd_solver_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(agentic_cfd src/main.cpp)
target_link_libraries(agentic_cfd PRIVATE cfd_solver_core)

add_executable(cgns_probe tools/cgns_probe.cpp)
target_include_directories(cgns_probe PRIVATE "${CFD_EXTERNALS_INCLUDE}")
target_link_directories(cgns_probe PRIVATE "${CFD_EXTERNALS_LIB}")
target_link_libraries(cgns_probe PRIVATE cgns hdf5 z)
target_compile_options(cgns_probe PRIVATE -Wall -Wextra -Wpedantic)
