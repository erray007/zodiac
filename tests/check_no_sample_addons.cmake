# cmake -DNM=<nm> -DARCHIVE=<libangelscript.a> -P check_no_sample_addons.cmake
#
# The test test_angelscript_has_no_sample_addons (spec 8.4): fails unless ARCHIVE, the AngelScript library games link (the kit's
# lib/release/libangelscript.a on Windows), defines asCreateScriptEngine (so nm read its symbols) and holds no symbol of the sample add-ons
# Zodiac's types replace: scriptstdstring (RegisterStdString*), scriptarray (RegisterScriptArray, CScriptArray) and scriptdictionary
# (RegisterScriptDictionary*, CScriptDictionary, CScriptDictValue). Those live in angelscript_sample_addons, which only tests/bench links.
foreach(variable NM ARCHIVE)
if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
message(FATAL_ERROR "check_no_sample_addons: ${variable} is not set")
endif()
endforeach()
if(NOT EXISTS "${ARCHIVE}")
message(FATAL_ERROR "check_no_sample_addons: ${ARCHIVE} does not exist")
endif()
execute_process(COMMAND "${NM}" "${ARCHIVE}" RESULT_VARIABLE result OUTPUT_VARIABLE symbols ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
message(FATAL_ERROR "check_no_sample_addons: ${NM} failed on ${ARCHIVE} (${result}): ${errors}")
endif()
if(NOT symbols MATCHES "asCreateScriptEngine")
message(FATAL_ERROR "check_no_sample_addons: ${ARCHIVE} does not define asCreateScriptEngine, so its symbols were not read")
endif()
string(REGEX MATCHALL "[^\r\n]*(RegisterStdString|RegisterScriptArray|RegisterScriptDictionary|CScriptArray|CScriptDictionary|CScriptDictValue)[^\r\n]*" found "${symbols}")
if(found)
list(JOIN found "\n" shown)
message(FATAL_ERROR "check_no_sample_addons: ${ARCHIVE} holds sample add-on symbols:\n${shown}")
endif()
message(STATUS "check_no_sample_addons: ${ARCHIVE} holds no sample add-on symbol")
