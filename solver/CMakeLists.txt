cmake_minimum_required(VERSION 3.16)
project(cfd2d_solver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CFD_EXTERNALS_ROOT "" CACHE PATH "Path to cfd_externals install root")
if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
endif()

find_package(MPI REQUIRED)

set(EIGEN_ROOT "" CACHE PATH "Path to eigen root")
if(NOT EIGEN_ROOT)
  set(EIGEN_ROOT "$ENV{EIGEN_ROOT}")
endif()
if(EXISTS "${EIGEN_ROOT}/Eigen")
  set(EIGEN_INCLUDE_DIR "${EIGEN_ROOT}")
elseif(EXISTS "${CFD_EXTERNALS_ROOT}/../eigen/Eigen")
  set(EIGEN_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/../eigen")
endif()

set(NLOHMANN_ROOT "" CACHE PATH "Path to nlohmann root")
if(NOT NLOHMANN_ROOT)
  set(NLOHMANN_ROOT "$ENV{NLOHMANN_ROOT}")
endif()
if(EXISTS "${NLOHMANN_ROOT}/nlohmann")
  set(NLOHMANN_INCLUDE_DIR "${NLOHMANN_ROOT}")
elseif(EXISTS "${CFD_EXTERNALS_ROOT}/../nlohmann/nlohmann")
  set(NLOHMANN_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/../nlohmann")
endif()

set(FMT_ROOT "" CACHE PATH "Path to fmt root")
if(NOT FMT_ROOT)
  set(FMT_ROOT "$ENV{FMT_ROOT}")
endif()
if(EXISTS "${FMT_ROOT}/include/fmt")
  set(FMT_INCLUDE_DIR "${FMT_ROOT}/include")
elseif(EXISTS "${CFD_EXTERNALS_ROOT}/../fmt/include/fmt")
  set(FMT_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/../fmt/include")
endif()

set(CGNS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(CGNS_LIB_DIR "${CFD_EXTERNALS_ROOT}/lib")

include_directories(
  ${CGNS_INCLUDE_DIR}
  ${EIGEN_INCLUDE_DIR}
  ${NLOHMANN_INCLUDE_DIR}
  ${FMT_INCLUDE_DIR}
  ${MPI_CXX_INCLUDE_DIRS}
)

link_directories(${CGNS_LIB_DIR})

add_executable(cfd2d
  src/main.cpp
  src/cgns_reader.cpp
  src/mesh.cpp
  src/partition.cpp
  src/solver.cpp
  src/fluxes.cpp
  src/reconstruction.cpp
  src/implicit.cpp
  src/output.cpp
  src/case_io.cpp
)

target_compile_options(cfd2d PRIVATE -O3 -Wall -Wno-unused-parameter -Wno-unused-variable)

target_link_libraries(cfd2d PRIVATE
  MPI::MPI_CXX
  cgns
  metis
  hdf5
  z
)

install(TARGETS cfd2d RUNTIME DESTINATION bin)
