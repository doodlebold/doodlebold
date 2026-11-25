# Included before project(), which needs the version number.

# Read from the environment because the release jobs configure through `cmake --workflow` and ci/ scripts, which take no -D.
set(DOODLEBOLD_RELEASE_VERSION "$ENV{DOODLEBOLD_RELEASE_VERSION}" CACHE STRING
    "The GitHub release's tag, vX.Y.Z or X.Y.Z; empty outside a release")

# The version when git can see no release tag, as in CI's shallow checkouts: 1.0.0, the first release's number.
set(doodlebold_version_without_tags 1.0.0)
# An RPM version cannot hold "-", so a tag such as v1.2.0-beta could not be packaged.
set(doodlebold_tag_pattern "^v?([0-9]+\\.[0-9]+\\.[0-9]+)$")

find_package(Git QUIET)

if(DOODLEBOLD_RELEASE_VERSION)
  if(NOT DOODLEBOLD_RELEASE_VERSION MATCHES "${doodlebold_tag_pattern}")
    message(FATAL_ERROR "The release tag \"${DOODLEBOLD_RELEASE_VERSION}\" is not vX.Y.Z or X.Y.Z (for example v1.2.0). "
                        "Publish the release again under such a tag.")
  endif()
  set(DOODLEBOLD_VERSION_NUMBER "${CMAKE_MATCH_1}")
  set(DOODLEBOLD_DISPLAY_VERSION "${CMAKE_MATCH_1}")
else()
  set(DOODLEBOLD_VERSION_NUMBER "${doodlebold_version_without_tags}")
  if(GIT_FOUND)
    # --exclude: a pre-release tag such as v1.5.0-beta, nearer than the last release, would otherwise hide it.
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0 --match "v[0-9]*" --match "[0-9]*" --exclude "*-*"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE last_tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(last_tag MATCHES "${doodlebold_tag_pattern}")
      set(DOODLEBOLD_VERSION_NUMBER "${CMAKE_MATCH_1}")
    endif()
  endif()
  # So a build of unreleased work can't pass for the release it follows.
  set(DOODLEBOLD_DISPLAY_VERSION "${DOODLEBOLD_VERSION_NUMBER}-dev")
endif()
message(STATUS "Doodlebold version ${DOODLEBOLD_DISPLAY_VERSION}")
