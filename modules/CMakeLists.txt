# zodiac_ship_module(<name>)
#
# Marks a module of this folder as shipped: the kit installs its zodiac_mod_<name>.dll into modules/, and zodiac_kit_stage installs its static archive and stamp
# into modules/static. Only modules of this folder are shipped; the fixture modules of tests/ are never installed.
function(zodiac_ship_module name)
if(WIN32)
install(FILES "$<TARGET_FILE:zodiac_mod_${name}>" DESTINATION modules)
endif()
set_property(GLOBAL APPEND PROPERTY ZODIAC_SHIPPED_MODULES ${name})
endfunction()
# The example module doubles as a template for module authors and as a test module, and the kit ships it.
zodiac_add_module(example SOURCES example/example.cpp)
zodiac_ship_module(example)
