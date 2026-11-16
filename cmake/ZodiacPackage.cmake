# zodiac_package()
#
# Windows only. Creates the target zodiac_zip, the portable kit:
# - dist/Zodiac is emptied and filled by "cmake --install" (the layout of spec 8.1) plus README.txt, copied from packaging/README-zip.txt,
# - dist/zodiac-<version>-windows-x86_64.zip holds that folder, with Zodiac/ as its only top-level entry, made by cmake -E tar --format=zip.
# The target builds what the install copies (both tools, the staged toolchain files and the shipped modules) first, but not the tests.
# Call it after zodiac_kit_stage and every zodiac_add_module call.
include_guard(GLOBAL)
function(zodiac_package)
set(dist "${CMAKE_SOURCE_DIR}/dist")
set(kit "${dist}/Zodiac")
set(zip "${dist}/zodiac-${PROJECT_VERSION}-windows-x86_64.zip")
file(MAKE_DIRECTORY "${dist}")
add_custom_target(zodiac_zip
COMMAND "${CMAKE_COMMAND}" -E rm -rf "${kit}" "${zip}"
COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --prefix "${kit}"
COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/packaging/README-zip.txt" "${kit}/README.txt"
COMMAND "${CMAKE_COMMAND}" -E tar cf "${zip}" --format=zip -- Zodiac
WORKING_DIRECTORY "${dist}"
COMMENT "Packaging ${zip}"
VERBATIM)
add_dependencies(zodiac_zip zodiac zodiacw zodiac_kit_stage)
get_property(shipped_modules GLOBAL PROPERTY ZODIAC_SHIPPED_MODULES)
foreach(module IN LISTS shipped_modules)
add_dependencies(zodiac_zip zodiac_mod_${module})
endforeach()
endfunction()
