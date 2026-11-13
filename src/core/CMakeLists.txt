add_library(zodiac_core STATIC host.cpp host.hpp)
target_include_directories(zodiac_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(zodiac_core PUBLIC angelscript zodiac_plugin_abi)
zodiac_target_warnings(zodiac_core)
