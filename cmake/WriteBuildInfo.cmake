# Run at every build, not at configure, so the commit is the one built. configure_file leaves an unchanged file alone, so an
# unchanged commit recompiles nothing.

# Hash characters shown: 7, git's and GitHub's own short length, so a reported hash reads as it does there. Git gives more where
# 7 would match two commits.
set(commit_length 7)

set(DOODLEBOLD_COMMIT "")
if(GIT)
  execute_process(COMMAND "${GIT}" rev-parse --short=${commit_length} HEAD
                  WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE failed
                  OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT failed)
    set(DOODLEBOLD_COMMIT "${commit}")
  endif()
endif()
# The Fedora container installs git only after the checkout, so that checkout has no history to read.
if(NOT DOODLEBOLD_COMMIT AND DEFINED ENV{GITHUB_SHA})
  string(SUBSTRING "$ENV{GITHUB_SHA}" 0 ${commit_length} DOODLEBOLD_COMMIT)
endif()

set(DOODLEBOLD_VERSION "${VERSION}")
configure_file("${IN}" "${OUT}" @ONLY)
