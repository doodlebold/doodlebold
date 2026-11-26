#pragma once

// gtest catches a crash in a test on Windows and prints an empty stack, so where it was is printed here first, with the build's
// symbols. Elsewhere it does nothing: a crash there ends the test program, and the crash report or a core dump has the stack.
void printCrashStacks();
