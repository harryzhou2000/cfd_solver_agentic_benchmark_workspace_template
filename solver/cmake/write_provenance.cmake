if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED REPOSITORY_ROOT OR NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "write_provenance.cmake requires GIT_EXECUTABLE, REPOSITORY_ROOT, OUTPUT_FILE")
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${REPOSITORY_ROOT}" rev-parse HEAD
  OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE revision_status ERROR_QUIET)
string(LENGTH "${revision}" revision_length)
if(NOT revision_status EQUAL 0 OR NOT revision_length EQUAL 40 OR
   NOT revision MATCHES "^[0-9a-fA-F]+$")
  message(FATAL_ERROR "cannot determine parent repository git revision")
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${REPOSITORY_ROOT}" status --porcelain --untracked-files=normal
  OUTPUT_VARIABLE status_text RESULT_VARIABLE status_status ERROR_QUIET)
if(NOT status_status EQUAL 0)
  message(FATAL_ERROR "cannot determine parent repository dirty state")
endif()
if(status_text STREQUAL "")
  set(dirty false)
else()
  set(dirty true)
endif()

get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
set(content "#pragma once\n#define CFD_GIT_REVISION \"${revision}\"\n#define CFD_SOURCE_DIRTY ${dirty}\n")
set(temporary "${OUTPUT_FILE}.tmp")
file(WRITE "${temporary}" "${content}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${temporary}" "${OUTPUT_FILE}")
file(REMOVE "${temporary}")
