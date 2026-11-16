# zodiac_add_module(<name> SOURCES <files...> [DEPENDS <module names...>] [DEFINITIONS <defs...>] [OUTPUT_DIRECTORY <dir>] [NO_STATIC])
#
# Creates zodiac_mod_<name>: a SHARED library whose file is zodiac_mod_<name><shared suffix> (no "lib" prefix, so the
# name is identical on every platform apart from the extension), written to ${CMAKE_BINARY_DIR}/modules by default.
# Also creates zodiac_mod_<name>_static (STATIC, compiled with ZODIAC_MODULE_STATIC and ZODIAC_MODULE_ID=<name>) for hosts that link modules in:
# its ZODIAC_DEFINE_MODULE emits the record zodiac_static_module_<name>, which the final link must force in with -u zodiac_static_module_<name>.
# DEPENDS names other zodiac modules: it only orders the builds, the runtime dependency comes from the module info.
include_guard(GLOBAL)
function(zodiac_add_module name)
cmake_parse_arguments(ZM "NO_STATIC" "OUTPUT_DIRECTORY" "SOURCES;DEPENDS;DEFINITIONS" ${ARGN})
if(NOT ZM_SOURCES)
message(FATAL_ERROR "zodiac_add_module(${name}): SOURCES is required")
endif()
if(NOT ZM_OUTPUT_DIRECTORY)
set(ZM_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules")
endif()
set(shared zodiac_mod_${name})
add_library(${shared} SHARED ${ZM_SOURCES})
set_target_properties(${shared} PROPERTIES
PREFIX ""
RUNTIME_OUTPUT_DIRECTORY "${ZM_OUTPUT_DIRECTORY}"
LIBRARY_OUTPUT_DIRECTORY "${ZM_OUTPUT_DIRECTORY}"
ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/module_import_libs"
CXX_VISIBILITY_PRESET hidden
C_VISIBILITY_PRESET hidden
VISIBILITY_INLINES_HIDDEN ON
)
target_link_libraries(${shared} PRIVATE zodiac_plugin_abi)
zodiac_target_warnings(${shared})
if(ZM_DEFINITIONS)
target_compile_definitions(${shared} PRIVATE ${ZM_DEFINITIONS})
endif()
foreach(dependency IN LISTS ZM_DEPENDS)
add_dependencies(${shared} zodiac_mod_${dependency})
endforeach()
if(NOT ZM_NO_STATIC)
set(static ${shared}_static)
add_library(${static} STATIC ${ZM_SOURCES})
set_target_properties(${static} PROPERTIES POSITION_INDEPENDENT_CODE ON ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/module_import_libs")
target_compile_definitions(${static} PRIVATE ZODIAC_MODULE_STATIC ZODIAC_MODULE_ID=${name} ${ZM_DEFINITIONS})
target_link_libraries(${static} PUBLIC zodiac_plugin_abi)
zodiac_target_warnings(${static})
foreach(dependency IN LISTS ZM_DEPENDS)
add_dependencies(${static} zodiac_mod_${dependency}_static)
endforeach()
endif()
endfunction()
