# Stamp the current git revision into version.h.
#
# Run both at configure time (included directly, so the first compile has a
# header) and as a build-time custom target (via cmake -P, so the revision is
# refreshed on every build).  When run with -P the CNS2D_* variables are passed
# on the command line; when included, they are already set in the parent scope.
if(NOT DEFINED CNS2D_SOURCE_DIR)
  set(CNS2D_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(NOT DEFINED CNS2D_VERSION_IN)
  set(CNS2D_VERSION_IN "${CNS2D_SOURCE_DIR}/src/version.h.in")
endif()
if(NOT DEFINED CNS2D_VERSION_OUT)
  set(CNS2D_VERSION_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h")
endif()

set(CNS2D_GIT_REVISION "")
find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CNS2D_SOURCE_DIR}"
    OUTPUT_VARIABLE CNS2D_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  # Mark a dirty worktree explicitly, so a result produced from uncommitted
  # changes cannot be attributed to a clean commit.
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${CNS2D_SOURCE_DIR}"
    OUTPUT_VARIABLE CNS2D_GIT_DIRTY
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT CNS2D_GIT_DIRTY STREQUAL "" AND NOT CNS2D_GIT_REVISION STREQUAL "")
    set(CNS2D_GIT_REVISION "${CNS2D_GIT_REVISION}-dirty")
  endif()
endif()

# Write via a temporary file and only replace the real header when the contents
# change, so an unchanged revision does not force a rebuild of the whole tree.
#
# When the contents DO change, the translation units that embed the revision must
# be recompiled.  Ninja/Make track the generated header as a dependency of those
# objects, but only if the header is newer than them -- and copy_if_different
# preserves nothing about the sources that read it, so the touch below makes the
# dependency edge fire reliably.
configure_file("${CNS2D_VERSION_IN}" "${CNS2D_VERSION_OUT}.tmp" @ONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
                        "${CNS2D_VERSION_OUT}.tmp" "${CNS2D_VERSION_OUT}"
                RESULT_VARIABLE cns2d_version_differs
                OUTPUT_QUIET ERROR_QUIET)
if(NOT cns2d_version_differs EQUAL 0)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy
                          "${CNS2D_VERSION_OUT}.tmp" "${CNS2D_VERSION_OUT}")
endif()
