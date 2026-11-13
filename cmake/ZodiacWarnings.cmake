# zodiac_target_warnings(<target>) applies the project warning set to one of our own targets.
function(zodiac_target_warnings target)
target_compile_options(${target} PRIVATE -Wall -Wextra)
endfunction()
