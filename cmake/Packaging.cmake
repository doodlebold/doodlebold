# Linux: an RPM for Fedora (the fedora-rpm preset, CPack's RPM generator). Windows: a ZIP of the installed files, which the install
# step fills with Qt's DLLs and plugins (src/CMakeLists.txt): the windows-zip and windows-mingw-zip presets.
set(CPACK_PACKAGE_NAME doodlebold)
set(CPACK_PACKAGE_VENDOR "doodlebold")
set(CPACK_PACKAGE_CONTACT "doodlebold")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION "A digital painting application: layers, selections and timelapse recording; opens and saves Photoshop files.")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}")
if(UNIX AND NOT APPLE)
  set(CPACK_GENERATOR RPM)
  set(CPACK_RPM_PACKAGE_LICENSE "MIT AND OFL-1.1")
  set(CPACK_RPM_PACKAGE_GROUP "Applications/Graphics")
  # 1 unless given: release.yml passes 1.fc<Fedora version>. Installing a local package of the same version again takes dnf reinstall.
  set(DOODLEBOLD_RPM_RELEASE "1" CACHE STRING "RPM release of the package")
  set(CPACK_RPM_PACKAGE_RELEASE "${DOODLEBOLD_RPM_RELEASE}")
  set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
  set(CPACK_RPM_PACKAGE_AUTOREQ ON)
  # Plugins Qt loads at run time, which the automatic dependencies cannot see: Wayland, and the SVG icon engine.
  # and the programs timelapse export runs (FFmpeg from Fedora or RPM Fusion: either provides them).
  set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtwayland, qt6-qtsvg, /usr/bin/ffmpeg, /usr/bin/ffprobe")
  set(CPACK_RPM_DEBUGINFO_PACKAGE OFF)
  # Directories every Fedora system owns already: the package must not claim them.
  set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
      /usr/share/applications /usr/share/icons /usr/share/icons/hicolor
      /usr/share/icons/hicolor/16x16 /usr/share/icons/hicolor/16x16/apps /usr/share/icons/hicolor/24x24
      /usr/share/icons/hicolor/24x24/apps /usr/share/icons/hicolor/32x32 /usr/share/icons/hicolor/32x32/apps
      /usr/share/icons/hicolor/48x48 /usr/share/icons/hicolor/48x48/apps /usr/share/icons/hicolor/64x64
      /usr/share/icons/hicolor/64x64/apps /usr/share/icons/hicolor/128x128 /usr/share/icons/hicolor/128x128/apps
      /usr/share/icons/hicolor/256x256 /usr/share/icons/hicolor/256x256/apps /usr/share/icons/hicolor/512x512
      /usr/share/icons/hicolor/512x512/apps /usr/share/mime /usr/share/mime/packages /usr/share/licenses)
else()
  set(CPACK_GENERATOR ZIP)
  set(CPACK_PACKAGE_FILE_NAME "Doodlebold-${PROJECT_VERSION}-windows-x64")
endif()
include(CPack)
