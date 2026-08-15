cmake_minimum_required(VERSION 3.16)
project(cfd_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# External dependency locations
if(NOT DEFINED CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT $ENV{CFD_EXTERNALS_ROOT})
  else()
    set(CFD_EXTERNALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/cfd_externals/install")
  endif()
endif()
if(NOT DEFINED EXTERNALS_DIR)
  if(DEFINED ENV{EXTERNALS_DIR})
    set(EXTERNALS_DIR $ENV{EXTERNALS_DIR})
  else()
    set(EXTERNALS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external")
  endif()
endif()

message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")
message(STATUS "EXTERNALS_DIR      = ${EXTERNALS_DIR}")

find_package(MPI REQUIRED COMPONENTS CXX)

find_path(CGNS_INCLUDE_DIR cgnslib.h PATHS ${CFD_EXTERNALS_ROOT}/include NO_DEFAULT_PATH)
find_library(CGNS_LIBRARY cgns PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH)
find_library(METIS_LIBRARY metis PATHS ${CFD_EXTERNALS_ROOT}/lib NO_DEFAULT_PATH)
find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp PATHS ${EXTERNALS_DIR}/nlohmann NO_DEFAULT_PATH)

if(NOT CGNS_INCLUDE_DIR OR NOT CGNS_LIBRARY)
  message(FATAL_ERROR "CGNS not found under ${CFD_EXTERNALS_ROOT}")
endif()
if(NOT METIS_LIBRARY)
  message(FATAL_ERROR "METIS not found under ${CFD_EXTERNALS_ROOT}")
endif()
if(NOT NLOHMANN_INCLUDE_DIR)
  message(FATAL_ERROR "nlohmann_json not found under ${EXTERNALS_DIR}/nlohmann")
endif()

add_executable(cfd_solver
  src/main.cpp
  src/config.cpp
  src/mesh.cpp
  src/numerics.cpp
  src/driver.cpp
)

target_include_directories(cfd_solver PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CGNS_INCLUDE_DIR}
  ${NLOHMANN_INCLUDE_DIR}
)

target_link_libraries(cfd_solver PRIVATE
  MPI::MPI_CXX
  ${CGNS_LIBRARY}
  ${METIS_LIBRARY}
  m
)

target_compile_options(cfd_solver PRIVATE -O3 -Wextra -Wno-unused-parameter)

# RPATH for shared libs at runtime
get_filename_component(CFD_EXTERNALS_LIB "${CFD_EXTERNALS_ROOT}/lib" ABSOLUTE)
set_target_properties(cfd_solver PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_LIB}"
  INSTALL_RPATH "${CFD_EXTERNALS_LIB}"
)

install(TARGETS cfd_solver RUNTIME DESTINATION bin)
