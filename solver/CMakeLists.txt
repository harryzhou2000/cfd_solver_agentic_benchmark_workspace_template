cmake_minimum_required(VERSION 3.16)
project(omo_cfd_solver VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- External dependency root ----
set(CFD_EXTERNALS_ROOT "" CACHE PATH "Root of external/cfd_externals/install")
if(NOT CFD_EXTERNALS_ROOT)
  if(DEFINED ENV{CFD_EXTERNALS_ROOT})
    set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install")
    set(CFD_EXTERNALS_ROOT "${CMAKE_SOURCE_DIR}/../external/cfd_externals/install")
  endif()
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")

# ---- MPI ----
find_package(MPI REQUIRED CXX)

# ---- CGNS / HDF5 ----
set(CGNS_ROOT "${CFD_EXTERNALS_ROOT}" CACHE PATH "CGNS install root")
set(HDF5_ROOT "${CFD_EXTERNALS_ROOT}" CACHE PATH "HDF5 install root")

find_library(CGNS_LIB cgns PATHS "${CGNS_ROOT}/lib" NO_DEFAULT_PATH)
find_path(CGNS_INCLUDE cgnslib.h PATHS "${CGNS_ROOT}/include" NO_DEFAULT_PATH)
if(CGNS_LIB AND CGNS_INCLUDE)
  message(STATUS "Found CGNS: ${CGNS_LIB}")
  add_library(cgns_dep INTERFACE)
  target_link_libraries(cgns_dep INTERFACE ${CGNS_LIB})
  target_include_directories(cgns_dep INTERFACE ${CGNS_INCLUDE})
else()
  message(FATAL_ERROR "CGNS not found under ${CGNS_ROOT}")
endif()

find_library(HDF5_LIB hdf5 PATHS "${HDF5_ROOT}/lib" NO_DEFAULT_PATH)
find_library(HDF5_HL_LIB hdf5_hl PATHS "${HDF5_ROOT}/lib" NO_DEFAULT_PATH)
if(HDF5_LIB AND HDF5_HL_LIB)
  add_library(hdf5_dep INTERFACE)
  target_link_libraries(hdf5_dep INTERFACE ${HDF5_LIB} ${HDF5_HL_LIB})
  target_include_directories(hdf5_dep INTERFACE "${HDF5_ROOT}/include")
else()
  message(FATAL_ERROR "HDF5 not found under ${HDF5_ROOT}")
endif()

# ---- METIS ----
find_library(METIS_LIB metis PATHS "${CFD_EXTERNALS_ROOT}/lib" NO_DEFAULT_PATH)
find_path(METIS_INCLUDE metis.h PATHS "${CFD_EXTERNALS_ROOT}/include" NO_DEFAULT_PATH)
if(METIS_LIB AND METIS_INCLUDE)
  message(STATUS "Found METIS: ${METIS_LIB}")
else()
  message(FATAL_ERROR "METIS not found")
endif()

# ---- Header-only Eigen ----
set(EIGEN_ROOT "${CMAKE_SOURCE_DIR}/../external/eigen" CACHE PATH "Eigen root")
if(NOT EXISTS "${EIGEN_ROOT}/Eigen/Core")
  if(EXISTS "${CMAKE_SOURCE_DIR}/../external/eigen/Eigen/Core")
    set(EIGEN_ROOT "${CMAKE_SOURCE_DIR}/../external/eigen" CACHE PATH "Eigen root" FORCE)
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/../external/eigen/eigen3/Eigen/Core")
    set(EIGEN_ROOT "${CMAKE_SOURCE_DIR}/../external/eigen/eigen3" CACHE PATH "Eigen root" FORCE)
  endif()
endif()
message(STATUS "EIGEN_ROOT = ${EIGEN_ROOT}")

# ---- nlohmann_json ----
set(NLOHMANN_ROOT "${CMAKE_SOURCE_DIR}/../external/nlohmann" CACHE PATH "nlohmann_json root")
# nlohmann_json headers are at external/nlohmann/nlohmann/json.hpp (include as <nlohmann/json.hpp>)
# The include root is external/nlohmann/ itself

# ---- fmt ----
set(FMT_ROOT "${CMAKE_SOURCE_DIR}/../external/fmt" CACHE PATH "fmt root")
if(NOT EXISTS "${FMT_ROOT}/fmt/core.h")
  if(EXISTS "${CMAKE_SOURCE_DIR}/../external/fmt/include/fmt/core.h")
    set(FMT_ROOT "${CMAKE_SOURCE_DIR}/../external/fmt/include" CACHE PATH "fmt root" FORCE)
  endif()
endif()

# ---- Solver library ----
file(GLOB_RECURSE SOLVER_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_library(solver_lib STATIC ${SOLVER_SOURCES})
target_include_directories(solver_lib PUBLIC
  "${CMAKE_SOURCE_DIR}/src"
  "${EIGEN_ROOT}"
  "${NLOHMANN_ROOT}"
  "${FMT_ROOT}"
  "${METIS_INCLUDE}"
)
target_link_libraries(solver_lib PUBLIC
  MPI::MPI_CXX
  cgns_dep
  hdf5_dep
  ${METIS_LIB}
)
target_compile_options(solver_lib PRIVATE -Wall -Wextra -O3 -march=native)

# ---- Solver executable ----
add_executable(omo_cfd_solver src/main.cpp)
target_link_libraries(omo_cfd_solver PRIVATE solver_lib)

# ---- Python venv setup hint ----
message(STATUS "Build complete. Run solver with: mpirun -np N ./omo_cfd_solver solve --case <case.json> --output <dir>")
