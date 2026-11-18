# cmake -DNM=<nm> [-DARCHIVE_DIR=<dir>] [-DARCHIVES=<a.a|b.a>] -P check_no_sample_addons.cmake
#
# The test test_angelscript_has_no_sample_addons (spec 8.4): the archives games link hold no symbol of the sample add-ons Zodiac's types
# replace: scriptstdstring (RegisterStdString*), scriptarray (RegisterScriptArray, CScriptArray) and scriptdictionary
# (RegisterScriptDictionary*, CScriptDictionary, CScriptDictValue). Those live in angelscript_sample_addons, which only tests/bench links.
# The archives checked are every *.a directly in ARCHIVE_DIR (the kit's lib/release on Windows: libangelscript.a,
# libzodiac_core_release.a, libzodiac_release_stub.a and any other the kit stages there; lib/release/crt holds the toolchain's own runtime
# and is not ours) plus every file in ARCHIVES. It fails unless at least one archive is checked, nm reads every one of them (each lists
# defined symbols), one of them defines asCreateScriptEngine (so the AngelScript library is among them), and none holds an add-on symbol;
# each offending archive is named with its symbols.
# Release archives hold LLVM bitcode (full LTO, cmake/ZodiacReleaseFlags.cmake); NM must then read bitcode, as llvm-nm (CMAKE_NM of llvm-mingw,
# and of cmake/toolchain-clang-linux.cmake on Linux) does. llvm-nm prints a bitcode symbol with dashes in place of the address ("---------------- T name"), a native one with hex digits.
if(NOT DEFINED NM OR "${NM}" STREQUAL "")
message(FATAL_ERROR "check_no_sample_addons: NM is not set")
endif()
set(archives "")
if(DEFINED ARCHIVE_DIR AND NOT "${ARCHIVE_DIR}" STREQUAL "")
if(NOT IS_DIRECTORY "${ARCHIVE_DIR}")
message(FATAL_ERROR "check_no_sample_addons: ${ARCHIVE_DIR} is not a directory")
endif()
file(GLOB staged LIST_DIRECTORIES false "${ARCHIVE_DIR}/*.a")
list(APPEND archives ${staged})
endif()
if(DEFINED ARCHIVES)
# Separated by | so the list survives add_test's command line.
string(REPLACE "|" ";" listed "${ARCHIVES}")
list(APPEND archives ${listed})
endif()
list(REMOVE_DUPLICATES archives)
if(NOT archives)
message(FATAL_ERROR "check_no_sample_addons: no archive to check (set ARCHIVE_DIR or ARCHIVES)")
endif()
set(engine_found FALSE)
set(failures "")
foreach(archive IN LISTS archives)
if(NOT EXISTS "${archive}")
message(FATAL_ERROR "check_no_sample_addons: ${archive} does not exist")
endif()
execute_process(COMMAND "${NM}" "${archive}" RESULT_VARIABLE result OUTPUT_VARIABLE symbols ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
message(FATAL_ERROR "check_no_sample_addons: ${NM} failed on ${archive} (${result}): ${errors}")
endif()
if(NOT symbols MATCHES "[\r\n][0-9a-fA-F-]* *[TDBR] ")
message(FATAL_ERROR "check_no_sample_addons: ${NM} listed no defined symbol in ${archive}, so its symbols were not read")
endif()
if(symbols MATCHES " T _?asCreateScriptEngine[\r\n]")
set(engine_found TRUE)
endif()
string(REGEX MATCHALL "[^\r\n]*(RegisterStdString|RegisterScriptArray|RegisterScriptDictionary|CScriptArray|CScriptDictionary|CScriptDictValue)[^\r\n]*" found "${symbols}")
if(found)
list(JOIN found "\n" shown)
string(APPEND failures "\n${archive} holds sample add-on symbols:\n${shown}")
endif()
endforeach()
if(failures)
message(FATAL_ERROR "check_no_sample_addons:${failures}")
endif()
if(NOT engine_found)
message(FATAL_ERROR "check_no_sample_addons: no checked archive defines asCreateScriptEngine, so the AngelScript library was not among them")
endif()
list(LENGTH archives count)
message(STATUS "check_no_sample_addons: ${count} archive(s) hold no sample add-on symbol: ${archives}")
