cmake_minimum_required(VERSION 3.16)
project(cfd2d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(MPI REQUIRED)

set(CFD_EXTERNALS_ROOT "" CACHE PATH "Path to cfd_externals/install")
if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install")
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

set(EXT_INCLUDE "${CFD_EXTERNALS_ROOT}/include")
set(EXT_LIB "${CFD_EXTERNALS_ROOT}/lib")

set(EIGEN_INCLUDE "${CMAKE_SOURCE_DIR}/../external/eigen" CACHE PATH "Eigen include")
set(FMT_INCLUDE "${CMAKE_SOURCE_DIR}/../external/fmt/include" CACHE PATH "fmt include")
set(NLOHMANN_INCLUDE "${CMAKE_SOURCE_DIR}/../external/nlohmann" CACHE PATH "nlohmann_json include")

include_directories(
  ${CMAKE_SOURCE_DIR}/src
  ${EXT_INCLUDE}
  ${EIGEN_INCLUDE}
  ${FMT_INCLUDE}
  ${NLOHMANN_INCLUDE}
)

link_directories(${EXT_LIB})

add_executable(cfd2d
  src/main.cpp
  src/mesh.cpp
  src/solver.cpp
  src/output.cpp
)

target_link_libraries(cfd2d PRIVATE
  MPI::MPI_CXX
  cgns hdf5 z
  metis
)

target_compile_options(cfd2d PRIVATE -Wall -Wno-unused-parameter -Wno-unused-variable)
set_target_properties(cfd2d PROPERTIES
  BUILD_RPATH "${EXT_LIB}"
  INSTALL_RPATH "${EXT_LIB}"
)
