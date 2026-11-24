# doodlebold_set_warnings(<target>): the warnings for the project's own code, C and C++. Qt's headers come in through its
# imported targets and miniz's through a SYSTEM include folder, so neither is warned about.
# -Wno-sign-conversion: GCC's -Wconversion leaves signed/unsigned mixes out in C++ and Clang's takes them in; this keeps the
# two compilers the same, and -Wsign-compare (in -Wall) still flags the comparisons.
function(doodlebold_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 $<$<BOOL:${DOODLEBOLD_WARNINGS_AS_ERRORS}>:/WX>)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
                           $<$<BOOL:${DOODLEBOLD_WARNINGS_AS_ERRORS}>:-Werror>)
  endif()
endfunction()
