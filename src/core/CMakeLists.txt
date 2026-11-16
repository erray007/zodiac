if(WIN32)
set(ZODIAC_PLATFORM_DIR windows)
else()
set(ZODIAC_PLATFORM_DIR posix)
endif()
set(ZODIAC_PLATFORM_SOURCES
${PROJECT_SOURCE_DIR}/src/platform/${ZODIAC_PLATFORM_DIR}/dynamic_library.cpp
${PROJECT_SOURCE_DIR}/src/platform/${ZODIAC_PLATFORM_DIR}/console.cpp
)
add_library(zodiac_core STATIC host.cpp host.hpp project_file.cpp project_file.hpp text.cpp text.hpp dynamic_library.hpp path_utf8.hpp module_registry.cpp module_registry.hpp diagnostics.cpp diagnostics.hpp script_runtime.cpp script_runtime.hpp ${ZODIAC_PLATFORM_SOURCES} ${PROJECT_SOURCE_DIR}/src/platform/console.hpp)
target_include_directories(zodiac_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(zodiac_core PRIVATE "ZODIAC_MODULE_SUFFIX=\"${CMAKE_SHARED_LIBRARY_SUFFIX}\"")
target_link_libraries(zodiac_core PUBLIC angelscript zodiac_plugin_abi ${CMAKE_DL_LIBS})
target_link_libraries(zodiac_core PRIVATE tomlplusplus::tomlplusplus)
zodiac_target_warnings(zodiac_core)
