# Floating-point maths gives the same result on every machine and in every build type (tools/README.md, "Brush pixels").
# A stroke's pixels must not depend on the compiler, its optimization level or the processor, so for every file of the
# project, tests included (they compile the painting headers' inline maths too):
# - no contraction: a * b + c is never fused into one multiply-add, which rounds once instead of twice. x86-64 without
#   -mfma has no such instruction, but ARM64 does, and GCC fuses by default, Clang within an expression;
# - MSVC keeps /fp:precise, which since Visual Studio 2022 doesn't contract either (only /fp:contract would);
# - nothing that lets the compiler reorder or approximate (-ffast-math, /fp:fast), and no processor-specific code
#   (-march=native, a CPU model, /arch:AVX2): the build runs on any x86-64 or ARM64 machine.
# -fno-math-errno changes no value: the maths functions just don't set errno (nothing here reads it after them), so sqrt
# and fmod become single instructions. The reference set is the same with it, and the first engine paints 3 to 4 % faster.
if(MSVC)
  if(MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "Visual Studio 2022 or later is needed: earlier versions contract under /fp:precise")
  endif()
  add_compile_options(/fp:precise)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(/clang:-ffp-contract=off)  # clang-cl's /fp:precise still contracts within an expression
  endif()
else()
  add_compile_options(-ffp-contract=off -fno-math-errno)
endif()
