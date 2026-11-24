# The `lint` target checks the project's own code (src/, plugins/, tests/) with clang-format and clang-tidy, and fails on
# anything either reports; `format` rewrites the files as clang-format lays them out. Their settings are .clang-format and
# .clang-tidy at the top of the repository. clang-tidy reads compile_commands.json, so `lint` builds everything first: it
# needs the files Qt's moc generates. `cmake --workflow --preset lint` also builds with every warning an error.
#
# doodlebold_add_lint_targets() is called at the end of the top-level CMakeLists.txt, once every target exists.

option(DOODLEBOLD_WARNINGS_AS_ERRORS "Treat the project's compiler warnings as errors (the lint preset)" OFF)

function(doodlebold_collect_targets directory out)
  get_property(targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirectories DIRECTORY ${directory} PROPERTY SUBDIRECTORIES)
  foreach(subdirectory IN LISTS subdirectories)
    doodlebold_collect_targets(${subdirectory} more)
    list(APPEND targets ${more})
  endforeach()
  set(${out} ${targets} PARENT_SCOPE)
endfunction()

function(doodlebold_add_lint_targets)
  find_program(DOODLEBOLD_CLANG_FORMAT clang-format)
  find_program(DOODLEBOLD_RUN_CLANG_TIDY run-clang-tidy)
  if(NOT DOODLEBOLD_CLANG_FORMAT OR NOT DOODLEBOLD_RUN_CLANG_TIDY)
    message(STATUS "lint and format targets need clang-format and run-clang-tidy (clang-tools-extra)")
    return()
  endif()
  set(own ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/plugins ${CMAKE_SOURCE_DIR}/tests)
  set(patterns)
  foreach(dir IN LISTS own)
    list(APPEND patterns ${dir}/*.cpp ${dir}/*.h ${dir}/*.c ${dir}/*.inc)
  endforeach()
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${patterns})

  add_custom_target(format
    COMMAND ${DOODLEBOLD_CLANG_FORMAT} -i ${sources}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "clang-format: rewriting the project's sources"
    VERBATIM)

  # run-clang-tidy picks the files from compile_commands.json whose paths match the pattern: every translation unit of ours.
  add_custom_target(lint
    COMMAND ${DOODLEBOLD_CLANG_FORMAT} --dry-run --Werror ${sources}
    COMMAND ${DOODLEBOLD_RUN_CLANG_TIDY} -quiet -p ${CMAKE_BINARY_DIR} -warnings-as-errors=* "^${CMAKE_SOURCE_DIR}/(src|plugins|tests)/.*\\.(cpp|c)$"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "clang-format and clang-tidy over the project's sources"
    VERBATIM)
  doodlebold_collect_targets(${CMAKE_SOURCE_DIR} targets)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "UTILITY" AND NOT type STREQUAL "INTERFACE_LIBRARY")
      add_dependencies(lint ${target})
    endif()
  endforeach()
endfunction()
