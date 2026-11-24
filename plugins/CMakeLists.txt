# File format plugins. The interface is the C header in include/; a plugin needs nothing else, neither Qt nor
# the program's code.

# doodlebold_add_format_plugin(<target> OUTPUT_DIRECTORY <dir> SOURCES <files>...): a plugin as a loadable module, its file
# named <target> with the platform's extension, exporting only its entry function.
function(doodlebold_add_format_plugin target)
  cmake_parse_arguments(PLUGIN "" "OUTPUT_DIRECTORY" "SOURCES" ${ARGN})
  add_library(${target} MODULE ${PLUGIN_SOURCES})
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/plugins/include)
  set_target_properties(${target} PROPERTIES PREFIX "" C_STANDARD 99 C_VISIBILITY_PRESET hidden
                        LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIRECTORY}" RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIRECTORY}")
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${DOODLEBOLD_WARNINGS_AS_ERRORS}>:/WX>)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
                           $<$<BOOL:${DOODLEBOLD_WARNINGS_AS_ERRORS}>:-Werror>)
  endif()
endfunction()

# The example, built into the "plugins" folder next to the program, where a build run from its folder finds it. It isn't
# installed: packages hold only the program.
doodlebold_add_format_plugin(farbfeld OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:doodlebold>/plugins" SOURCES examples/farbfeld/farbfeld.c)
