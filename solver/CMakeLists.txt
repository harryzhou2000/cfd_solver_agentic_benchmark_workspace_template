cmake_minimum_required(VERSION 3.16)
project(cfd_solver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

set(CFD_EXTERNALS_ROOT "" CACHE PATH "Root of compiled CFD external libraries")
if(CFD_EXTERNALS_ROOT STREQUAL "")
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
  else()
    set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install")
  endif()
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

set(EXTERNAL_ROOT "" CACHE PATH "Root of header-only external packages")
if(EXTERNAL_ROOT STREQUAL "")
  if(DEFINED ENV{EXTERNAL_ROOT})
    set(EXTERNAL_ROOT "$ENV{EXTERNAL_ROOT}")
  else()
    set(EXTERNAL_ROOT "${CMAKE_SOURCE_DIR}/../external")
  endif()
endif()
message(STATUS "EXTERNAL_ROOT = ${EXTERNAL_ROOT}")

find_package(MPI REQUIRED)

set(CGNS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(CGNS_LIB_DIR "${CFD_EXTERNALS_ROOT}/lib")
find_library(CGNS_LIBRARY cgns PATHS ${CGNS_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
find_library(HDF5_LIBRARY hdf5 PATHS ${CGNS_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
find_library(HDF5_HL_LIBRARY hdf5_hl PATHS ${CGNS_LIB_DIR} NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY z PATHS ${CGNS_LIB_DIR} NO_DEFAULT_PATH)
if(NOT ZLIB_LIBRARY)
  find_library(ZLIB_LIBRARY z)
endif()

find_library(METIS_LIBRARY metis PATHS ${CGNS_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
find_library(PARMETIS_LIBRARY parmetis PATHS ${CGNS_LIB_DIR} NO_DEFAULT_PATH)

set(EIGEN_INCLUDE_DIR "${EXTERNAL_ROOT}/eigen")
set(NLOHMANN_INCLUDE_DIR "${EXTERNAL_ROOT}/nlohmann")
set(FMT_INCLUDE_DIR "${EXTERNAL_ROOT}/fmt/include")

set(SOLVER_SOURCES
  src/main.cpp
  src/CgnsReader.cpp
  src/MeshBuilder.cpp
  src/Partitioner.cpp
  src/MpiHalo.cpp
  src/BoundaryConditions.cpp
  src/Gradient.cpp
  src/Reconstruction.cpp
  src/Residual.cpp
  src/ImplicitSolver.cpp
  src/SteadySolver.cpp
  src/TransientSolver.cpp
  src/OutputManager.cpp
)

add_executable(cfd_solver ${SOLVER_SOURCES})

target_include_directories(cfd_solver PRIVATE
  src/
  ${MPI_CXX_INCLUDE_DIRS}
  ${CGNS_INCLUDE_DIR}
  ${EIGEN_INCLUDE_DIR}
  ${NLOHMANN_INCLUDE_DIR}
  ${FMT_INCLUDE_DIR}
)

set(LINK_LIBS
  ${MPI_CXX_LIBRARIES}
  ${CGNS_LIBRARY}
  ${HDF5_LIBRARY}
  ${METIS_LIBRARY}
)
if(HDF5_HL_LIBRARY)
  list(APPEND LINK_LIBS ${HDF5_HL_LIBRARY})
endif()
if(ZLIB_LIBRARY)
  list(APPEND LINK_LIBS ${ZLIB_LIBRARY})
endif()
if(PARMETIS_LIBRARY)
  list(APPEND LINK_LIBS ${PARMETIS_LIBRARY})
endif()

target_link_libraries(cfd_solver PRIVATE ${LINK_LIBS} dl m)

target_compile_definitions(cfd_solver PRIVATE
  $<$<BOOL:${PARMETIS_LIBRARY}>:HAVE_PARMETIS>
)

install(TARGETS cfd_solver DESTINATION bin)
message(STATUS "CGNS library: ${CGNS_LIBRARY}")
message(STATUS "METIS library: ${METIS_LIBRARY}")

