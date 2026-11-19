# Applies a patch to a fetched dependency's source tree: the PATCH_COMMAND of cmake/Dependencies.cmake, run as
# cmake -DGIT=<git> -DPATCH=<patch file> -DSOURCE_DIR=<source tree> -P ZodiacApplyPatch.cmake (any other -D, such as the patch's hash that makes
# FetchContent run the step again when the patch changes, is ignored).
# Each file the patch changes is first made the same as the archive's (restored from <file>.zodiac-orig, the pristine copy kept the first time), so
# the step can run again on a tree it already patched: FetchContent repeats the patch step when its command changes, without extracting again.
# The files then lose trailing blanks and git apply reads them with LF line endings (core.autocrlf=input), as the patch's text has them (the
# AngelScript SDK ships CRLF; the repository stores text with LF and its editing tools drop trailing blanks), applies the patch exactly, without
# fuzz, or fails, and writes LF. (file(WRITE) cannot write LF itself: on Windows it writes text, every LF as CRLF.)
# git apply runs outside any repository: GIT_CEILING_DIRECTORIES keeps git from finding the checkout that may hold the dependency folder (.deps is
# inside it), in which git would read the patch's paths from the checkout's root and skip every file outside the working directory.
foreach(variable GIT PATCH SOURCE_DIR)
if(NOT DEFINED ${variable})
message(FATAL_ERROR "ZodiacApplyPatch.cmake needs -D${variable}=...")
endif()
endforeach()
file(STRINGS "${PATCH}" targets REGEX "^\\+\\+\\+ b/")
if(NOT targets)
message(FATAL_ERROR "${PATCH} changes no file")
endif()
foreach(target IN LISTS targets)
string(REGEX REPLACE "^\\+\\+\\+ b/" "" relative "${target}")
set(file "${SOURCE_DIR}/${relative}")
set(pristine "${file}.zodiac-orig")
if(EXISTS "${pristine}")
file(COPY_FILE "${pristine}" "${file}")
elseif(EXISTS "${file}")
file(COPY_FILE "${file}" "${pristine}")
else()
message(FATAL_ERROR "${PATCH} changes ${relative}, which ${SOURCE_DIR} does not have")
endif()
file(READ "${file}" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(REGEX REPLACE "[ \t]+\n" "\n" content "${content}")
string(REGEX REPLACE "[ \t]+$" "" content "${content}")
file(WRITE "${file}" "${content}")
endforeach()
cmake_path(GET SOURCE_DIR PARENT_PATH parent)
set(ENV{GIT_CEILING_DIRECTORIES} "${parent}")
unset(ENV{GIT_DIR})
unset(ENV{GIT_WORK_TREE})
# core.autocrlf=input also overrides a user's setting (Git for Windows defaults to true, which would write CRLF); core.safecrlf=false keeps the
# CRLF to LF conversion from being refused.
execute_process(COMMAND "${GIT}" -c core.autocrlf=input -c core.safecrlf=false apply --whitespace=nowarn "${PATCH}" WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
message(FATAL_ERROR "Applying ${PATCH} to ${SOURCE_DIR} failed (${result}):\n${output}")
endif()
message(STATUS "Applied ${PATCH}")
