cmake_minimum_required(VERSION 3.20)

project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CFD_EXTERNALS_ROOT "" CACHE PATH
    "Root of the supplied CFD dependency install (include/, lib/, bin/)")
if(NOT CFD_EXTERNALS_ROOT AND DEFINED ENV{CFD_EXTERNALS_ROOT})
  set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
endif()
if(NOT CFD_EXTERNALS_ROOT)
  get_filename_component(_workspace_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  set(CFD_EXTERNALS_ROOT "${_workspace_root}/external/cfd_externals/install")
endif()

find_package(MPI REQUIRED COMPONENTS CXX)

find_path(CGNS_INCLUDE_DIR cgnslib.h
  HINTS "${CFD_EXTERNALS_ROOT}/include")
find_library(CGNS_LIBRARY NAMES cgns
  HINTS "${CFD_EXTERNALS_ROOT}/lib")
find_path(METIS_INCLUDE_DIR metis.h
  HINTS "${CFD_EXTERNALS_ROOT}/include")
find_library(METIS_LIBRARY NAMES metis
  HINTS "${CFD_EXTERNALS_ROOT}/lib")
find_library(HDF5_LIBRARY NAMES hdf5
  HINTS "${CFD_EXTERNALS_ROOT}/lib")
find_library(ZLIB_LIBRARY NAMES z
  HINTS "${CFD_EXTERNALS_ROOT}/lib")

foreach(required_var CGNS_INCLUDE_DIR CGNS_LIBRARY METIS_INCLUDE_DIR METIS_LIBRARY)
  if(NOT ${required_var})
    message(FATAL_ERROR "Required CFD dependency was not found: ${required_var}. "
      "Set -DCFD_EXTERNALS_ROOT=<install-prefix>.")
  endif()
endforeach()

get_filename_component(_workspace_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(NLOHMANN_INCLUDE_DIR "${_workspace_root}/external/nlohmann")
if(NOT EXISTS "${NLOHMANN_INCLUDE_DIR}/nlohmann/json.hpp")
  message(FATAL_ERROR "nlohmann/json.hpp was not found at ${NLOHMANN_INCLUDE_DIR}")
endif()

add_executable(cfd_solver
  src/main.cpp
  src/config.cpp
  src/physics.cpp
  src/mesh.cpp
  src/partition.cpp
  src/solver.cpp
  src/io.cpp
)

target_include_directories(cfd_solver PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${NLOHMANN_INCLUDE_DIR}"
  "${CGNS_INCLUDE_DIR}"
  "${METIS_INCLUDE_DIR}"
)
target_link_libraries(cfd_solver PRIVATE MPI::MPI_CXX "${CGNS_LIBRARY}" "${METIS_LIBRARY}")
if(HDF5_LIBRARY)
  target_link_libraries(cfd_solver PRIVATE "${HDF5_LIBRARY}")
endif()
if(ZLIB_LIBRARY)
  target_link_libraries(cfd_solver PRIVATE "${ZLIB_LIBRARY}")
endif()

set_target_properties(cfd_solver PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib"
)

target_compile_options(cfd_solver PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion>
)

add_executable(mesh_diagnostics
  tests/mesh_diagnostics.cpp
  src/config.cpp
  src/mesh.cpp
)
target_include_directories(mesh_diagnostics PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${NLOHMANN_INCLUDE_DIR}"
  "${CGNS_INCLUDE_DIR}"
)
target_link_libraries(mesh_diagnostics PRIVATE "${CGNS_LIBRARY}")
if(HDF5_LIBRARY)
  target_link_libraries(mesh_diagnostics PRIVATE "${HDF5_LIBRARY}")
endif()
if(ZLIB_LIBRARY)
  target_link_libraries(mesh_diagnostics PRIVATE "${ZLIB_LIBRARY}")
endif()
set_target_properties(mesh_diagnostics PROPERTIES BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib")
target_compile_options(mesh_diagnostics PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion>
)
