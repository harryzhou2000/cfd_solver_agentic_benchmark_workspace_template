cmake_minimum_required(VERSION 3.16)
project(cfd2d LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT DEFINED CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "$ENV{CFD_EXTERNALS_ROOT}" CACHE PATH "cfd_externals install root")
endif()
if(NOT CFD_EXTERNALS_ROOT)
  set(CFD_EXTERNALS_ROOT "/opt/external/cfd_externals/install")
endif()
message(STATUS "CFD_EXTERNALS_ROOT = ${CFD_EXTERNALS_ROOT}")
if(NOT DEFINED EXTERNAL_HEADERS_ROOT)
  set(EXTERNAL_HEADERS_ROOT "/opt/external" CACHE PATH "root of header-only packages")
endif()
find_package(MPI REQUIRED)
find_package(OpenMP REQUIRED)
set(CGNS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(CGNS_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libcgns.so")
set(HDF5_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libhdf5.so")
set(HDF5_HL_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libhdf5_hl.so")
set(ZLIB_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libz.so")
set(METIS_INCLUDE_DIR "${CFD_EXTERNALS_ROOT}/include")
set(METIS_LIBRARY "${CFD_EXTERNALS_ROOT}/lib/libmetis.so")
add_executable(cfd2d src/main.cpp src/mesh.cpp src/physics.cpp src/solver.cpp src/output.cpp)
target_include_directories(cfd2d PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CGNS_INCLUDE_DIR} ${METIS_INCLUDE_DIR}
  ${EXTERNAL_HEADERS_ROOT}/eigen
  ${EXTERNAL_HEADERS_ROOT}/nlohmann
  ${EXTERNAL_HEADERS_ROOT}/fmt/include)
target_link_libraries(cfd2d PRIVATE
  ${CGNS_LIBRARY} ${HDF5_LIBRARY} ${HDF5_HL_LIBRARY} ${ZLIB_LIBRARY} ${METIS_LIBRARY}
  MPI::MPI_CXX)
target_link_libraries(cfd2d PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(cfd2d PRIVATE -O3 -march=native -ffast-math -fno-trapping-math -Wall -Wno-unused)
set_target_properties(cfd2d PROPERTIES
  BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib"
  INSTALL_RPATH "${CFD_EXTERNALS_ROOT}/lib")
