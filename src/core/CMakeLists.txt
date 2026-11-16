if(WIN32)
set(ZODIAC_PLATFORM_DIR windows)
else()
set(ZODIAC_PLATFORM_DIR posix)
endif()
set(ZODIAC_PLATFORM_SOURCES
${PROJECT_SOURCE_DIR}/src/platform/${ZODIAC_PLATFORM_DIR}/dynamic_library.cpp
${PROJECT_SOURCE_DIR}/src/platform/${ZODIAC_PLATFORM_DIR}/console.cpp
${PROJECT_SOURCE_DIR}/src/platform/${ZODIAC_PLATFORM_DIR}/executable_path.cpp
${PROJECT_SOURCE_DIR}/src/platform/${ZODIAC_PLATFORM_DIR}/run_process.cpp
)
set(ZODIAC_CORE_SOURCES host.cpp host.hpp project_file.cpp project_file.hpp text.cpp text.hpp dynamic_library.hpp path_utf8.hpp module_registry.cpp module_registry.hpp diagnostics.cpp diagnostics.hpp script_runtime.cpp script_runtime.hpp kit_layout.cpp kit_layout.hpp payload.cpp payload.hpp coff_object.cpp coff_object.hpp static_modules.hpp types/unicode.cpp types/unicode.hpp types/unicode_tables.cpp types/unicode_tables.hpp types/string.cpp types/string.hpp types/script_guard.hpp types/string_binding.cpp types/string_binding.hpp types/exception_binding.cpp types/array.cpp types/array.hpp types/array_fast_paths.cpp ${ZODIAC_PLATFORM_SOURCES} ${PROJECT_SOURCE_DIR}/src/platform/console.hpp ${PROJECT_SOURCE_DIR}/src/platform/process.hpp)
# zodiac_core is the dev configuration (zodiac.exe); zodiac_core_release is the configuration linked into release executables (lib/release in the kit).
# Both come from the same sources; the release one adds the static module record walk and ZODIAC_RELEASE=1, which plan 4's dev-only checks key on.
function(zodiac_core_target name)
add_library(${name} STATIC ${ARGN})
target_include_directories(${name} PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(${name} PRIVATE "ZODIAC_MODULE_SUFFIX=\"${CMAKE_SHARED_LIBRARY_SUFFIX}\"")
target_link_libraries(${name} PUBLIC angelscript zodiac_plugin_abi ${CMAKE_DL_LIBS})
target_link_libraries(${name} PRIVATE tomlplusplus::tomlplusplus)
zodiac_target_warnings(${name})
endfunction()
# pe_imports is for zodiac build only (shipping the DLLs dynamic modules import), so release executables' core leaves it out.
zodiac_core_target(zodiac_core ${ZODIAC_CORE_SOURCES} pe_imports.cpp pe_imports.hpp)
zodiac_core_target(zodiac_core_release ${ZODIAC_CORE_SOURCES} static_modules.cpp)
target_compile_definitions(zodiac_core_release PUBLIC ZODIAC_RELEASE=1)
