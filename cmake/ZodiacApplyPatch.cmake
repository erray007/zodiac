# Applies a patch to a fetched dependency's source tree: the PATCH_COMMAND of cmake/Dependencies.cmake, run as
# cmake -DGIT=<git> -DPATCH=<patch file> -DSOURCE_DIR=<source tree> -P ZodiacApplyPatch.cmake (any other -D, such as the hashes of the patch and of
# this script that make FetchContent run the step again when either changes, is ignored).
# The step can run again on a tree it already patched (FetchContent repeats the patch step when its command changes, without extracting again):
# every file an earlier run changed is first made the archive's again, from <file>.zodiac-orig, the pristine copy kept the first time. That
# includes files the current patch no longer changes, whose pristine copies are then removed.
# The files the patch changes then get LF line endings and lose trailing blanks, as the patch's text has them (the AngelScript SDK ships CRLF; the
# repository stores text with LF and its editing tools drop trailing blanks), and git apply applies the patch exactly, without fuzz, or fails.
# The script writes LF itself, before and after git apply, so the result does not depend on git's line ending settings: file(WRITE) writes text
# (every LF as CRLF on Windows), so files are written by file(CONFIGURE ... NEWLINE_STYLE LF), with every @ escaped so nothing is substituted.
# git apply runs outside any repository: GIT_CEILING_DIRECTORIES keeps git from finding the checkout that may hold the dependency folder (.deps is
# inside it), in which git would read the patch's paths from the checkout's root and skip every file outside the working directory.
foreach(variable GIT PATCH SOURCE_DIR)
if(NOT DEFINED ${variable})
message(FATAL_ERROR "ZodiacApplyPatch.cmake needs -D${variable}=...")
endif()
endforeach()

# zodiac_write_lf(<file>): rewrites <file> with LF line endings and no trailing blanks. file(CONFIGURE) writes only when the content changes,
# so a file already in that form keeps its timestamp (the restore from <file>.zodiac-orig before it has already touched every changed file).
set(ZODIAC_AT "@")
function(zodiac_write_lf file)
file(READ "${file}" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(REGEX REPLACE "[ \t]+\n" "\n" content "${content}")
string(REGEX REPLACE "[ \t]+$" "" content "${content}")
# file(CONFIGURE) also acts on #cmakedefine lines, which it could not write back unchanged.
if(content MATCHES "#cmakedefine")
message(FATAL_ERROR "${file} has a #cmakedefine line, which ZodiacApplyPatch.cmake cannot write unchanged")
endif()
string(REPLACE "@" "@ZODIAC_AT@" content "${content}")
file(CONFIGURE OUTPUT "${file}" CONTENT "${content}" @ONLY NEWLINE_STYLE LF)
endfunction()

file(STRINGS "${PATCH}" targets REGEX "^\\+\\+\\+ b/")
if(NOT targets)
message(FATAL_ERROR "${PATCH} changes no file")
endif()
set(files "")
foreach(target IN LISTS targets)
string(REGEX REPLACE "^\\+\\+\\+ b/" "" relative "${target}")
list(APPEND files "${SOURCE_DIR}/${relative}")
endforeach()
file(GLOB_RECURSE pristines "${SOURCE_DIR}/*.zodiac-orig")
foreach(pristine IN LISTS pristines)
string(REGEX REPLACE "\\.zodiac-orig$" "" file "${pristine}")
file(COPY_FILE "${pristine}" "${file}")
if(NOT file IN_LIST files)
file(REMOVE "${pristine}")
endif()
endforeach()
foreach(file IN LISTS files)
if(NOT EXISTS "${file}")
message(FATAL_ERROR "${PATCH} changes ${file}, which does not exist")
endif()
if(NOT EXISTS "${file}.zodiac-orig")
file(COPY_FILE "${file}" "${file}.zodiac-orig")
endif()
zodiac_write_lf("${file}")
endforeach()
cmake_path(GET SOURCE_DIR PARENT_PATH parent)
set(ENV{GIT_CEILING_DIRECTORIES} "${parent}")
unset(ENV{GIT_DIR})
unset(ENV{GIT_WORK_TREE})
# core.autocrlf=false: git reads and writes the files unconverted, whatever the user's setting (Git for Windows defaults to true).
execute_process(COMMAND "${GIT}" -c core.autocrlf=false apply --whitespace=nowarn "${PATCH}" WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
message(FATAL_ERROR "Applying ${PATCH} to ${SOURCE_DIR} failed (${result}):\n${output}")
endif()
# LF again, should an attribute or setting have made git write anything else.
foreach(file IN LISTS files)
zodiac_write_lf("${file}")
endforeach()
message(STATUS "Applied ${PATCH}")
