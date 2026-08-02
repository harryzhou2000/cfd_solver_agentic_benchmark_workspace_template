cmake_minimum_required(VERSION 3.20)

project(cfd_solver LANGUAGES CXX)

option(CFD_BUILD_TESTING "Build CFD solver foundation tests" ON)
set(CFD_EXTERNALS_ROOT "" CACHE PATH
    "Root of DNDSR-style external dependencies (contains include/, lib/, etc.)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MPI REQUIRED COMPONENTS CXX)

set(_cfd_json_include_hints
    "${CFD_EXTERNALS_ROOT}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann"
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/nlohmann_json/include")

find_path(CFD_NLOHMANN_JSON_INCLUDE_DIR
    NAMES nlohmann/json.hpp
    HINTS ${_cfd_json_include_hints}
    DOC "Directory containing nlohmann/json.hpp")

if(NOT CFD_NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR
        "nlohmann/json.hpp was not found. Set CFD_EXTERNALS_ROOT to the external "
        "installation root or provide the repository's external/nlohmann dependency.")
endif()

find_path(CFD_CGNS_INCLUDE_DIR NAMES cgnslib.h
    HINTS "${CFD_EXTERNALS_ROOT}/include"
    DOC "Directory containing cgnslib.h")
find_library(CFD_CGNS_LIBRARY NAMES cgns
    HINTS "${CFD_EXTERNALS_ROOT}/lib" "${CFD_EXTERNALS_ROOT}/lib64"
    NO_DEFAULT_PATH)
find_path(CFD_METIS_INCLUDE_DIR NAMES metis.h
    HINTS "${CFD_EXTERNALS_ROOT}/include"
    DOC "Directory containing metis.h")
find_library(CFD_METIS_LIBRARY NAMES metis
    HINTS "${CFD_EXTERNALS_ROOT}/lib" "${CFD_EXTERNALS_ROOT}/lib64"
    NO_DEFAULT_PATH)

if(NOT CFD_CGNS_INCLUDE_DIR OR NOT CFD_CGNS_LIBRARY)
    message(FATAL_ERROR "CGNS was not found below CFD_EXTERNALS_ROOT=${CFD_EXTERNALS_ROOT}")
endif()
if(NOT CFD_METIS_INCLUDE_DIR OR NOT CFD_METIS_LIBRARY)
    message(FATAL_ERROR "METIS was not found below CFD_EXTERNALS_ROOT=${CFD_EXTERNALS_ROOT}")
endif()

add_library(cfd_core
    src/case_config.cpp
    src/mesh.cpp
    src/output.cpp
    src/partition.cpp
    src/physics.cpp
    src/reconstruction.cpp
    src/solver.cpp)

target_include_directories(cfd_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CFD_NLOHMANN_JSON_INCLUDE_DIR}"
    PRIVATE
        "${CFD_CGNS_INCLUDE_DIR}"
        "${CFD_METIS_INCLUDE_DIR}")

target_link_libraries(cfd_core
    PUBLIC MPI::MPI_CXX
    PRIVATE "${CFD_CGNS_LIBRARY}" "${CFD_METIS_LIBRARY}")

set_target_properties(cfd_core PROPERTIES
    BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib;${CFD_EXTERNALS_ROOT}/lib64")

add_executable(cfd_solver src/main.cpp)
target_link_libraries(cfd_solver PRIVATE cfd_core)
set_target_properties(cfd_solver PROPERTIES
    BUILD_RPATH "${CFD_EXTERNALS_ROOT}/lib;${CFD_EXTERNALS_ROOT}/lib64")

target_compile_features(cfd_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(cfd_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(cfd_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(CFD_BUILD_TESTING)
    include(CTest)

    add_executable(case_config_tests tests/case_config_tests.cpp)
    target_link_libraries(case_config_tests PRIVATE cfd_core)
    target_compile_definitions(case_config_tests PRIVATE
        CFD_BENCHMARK_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/cases")

    add_test(NAME case_config_tests COMMAND case_config_tests)

    add_executable(physics_tests tests/physics_tests.cpp)
    target_link_libraries(physics_tests PRIVATE cfd_core)
    add_test(NAME physics_tests COMMAND physics_tests)

    add_executable(reconstruction_tests tests/reconstruction_tests.cpp)
    target_link_libraries(reconstruction_tests PRIVATE cfd_core)
    add_test(NAME reconstruction_tests COMMAND reconstruction_tests)

    add_executable(output_tests tests/output_tests.cpp)
    target_link_libraries(output_tests PRIVATE cfd_core)
    target_compile_definitions(output_tests PRIVATE
        CFD_BENCHMARK_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/cases")
    add_test(NAME output_tests COMMAND output_tests)

    add_test(NAME supervise_transient_case_tests
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_supervise_transient_case.sh")

    add_executable(mesh_tests tests/test_mesh.cpp)
    target_link_libraries(mesh_tests PRIVATE cfd_core)
    target_compile_definitions(mesh_tests PRIVATE
        CFD_BENCHMARK_MESH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../cfd_solver_agentic_benchmark/inputs/meshes")
    add_test(NAME mesh_np1 COMMAND mesh_tests)
    foreach(_ranks IN ITEMS 2 8)
        add_test(NAME mesh_np${_ranks}
            COMMAND "${MPIEXEC_EXECUTABLE}" ${MPIEXEC_NUMPROC_FLAG} ${_ranks}
                    ${MPIEXEC_PREFLAGS} $<TARGET_FILE:mesh_tests> ${MPIEXEC_POSTFLAGS})
    endforeach()
endif()
