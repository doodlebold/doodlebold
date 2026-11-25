# Included before project(), which needs the version number.

# Read from the environment because the release jobs configure through `cmake --workflow` and ci/ scripts, which take no -D.
set(DOODLEBOLD_RELEASE_VERSION "$ENV{DOODLEBOLD_RELEASE_VERSION}" CACHE STRING
    "The GitHub release's tag, vX.Y.Z or X.Y.Z; empty outside a release")

# An RPM version cannot hold "-", so a tag such as v1.2.0-beta could not be packaged.
set(doodlebold_tag_pattern "^v?([0-9]+\\.[0-9]+\\.[0-9]+)$")

if(DOODLEBOLD_RELEASE_VERSION)
  if(NOT DOODLEBOLD_RELEASE_VERSION MATCHES "${doodlebold_tag_pattern}")
    message(FATAL_ERROR "The release tag \"${DOODLEBOLD_RELEASE_VERSION}\" is not vX.Y.Z or X.Y.Z (for example v1.2.0). "
                        "Publish the release again under such a tag.")
  endif()
  set(DOODLEBOLD_VERSION_NUMBER "${CMAKE_MATCH_1}")
  set(DOODLEBOLD_DISPLAY_VERSION "${CMAKE_MATCH_1}")
else()
  # The last release's version, which release.yml writes there after each release, so a build needs no git to know it.
  set(doodlebold_version_file "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${doodlebold_version_file}")
  file(STRINGS "${doodlebold_version_file}" doodlebold_last_release LIMIT_COUNT 1)
  if(NOT doodlebold_last_release MATCHES "${doodlebold_tag_pattern}")
    message(FATAL_ERROR "VERSION holds \"${doodlebold_last_release}\", not a version X.Y.Z.")
  endif()
  set(DOODLEBOLD_VERSION_NUMBER "${CMAKE_MATCH_1}")
  # So a build of unreleased work can't pass for the release it follows.
  set(DOODLEBOLD_DISPLAY_VERSION "${DOODLEBOLD_VERSION_NUMBER}-dev")
endif()
message(STATUS "Doodlebold version ${DOODLEBOLD_DISPLAY_VERSION}")
