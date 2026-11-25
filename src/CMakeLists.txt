# miniz reads brush packs that come as .zip files (BrushPack.cpp). It's C, built as it comes, without this project's warnings.
add_library(doodlebold_miniz STATIC ${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c)
target_include_directories(doodlebold_miniz SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party/miniz)
set_target_properties(doodlebold_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# doodlebold_core: document model, raster, placeholder brush engine, input model. Depends on QtCore/QtGui only (no widgets),
# so it can be unit-tested headless and reused by tools.
add_library(doodlebold_core STATIC
  core/brush/BrushResources.cpp core/brush/BrushResources.h
  core/brush/BrushPack.cpp core/brush/BrushPack.h
  ${CMAKE_SOURCE_DIR}/resources/brushes/brushes.qrc
  core/brush/BrushSettings.cpp core/brush/BrushSettings.h
  core/color/ColorModels.cpp core/color/ColorModels.h
  core/text/TextContent.cpp core/text/TextContent.h
  core/text/TextLayout.cpp core/text/TextLayout.h
  core/color/ColorState.cpp core/color/ColorState.h
  core/color/ColorAssets.cpp core/color/ColorAssets.h
  core/color/ColorAssetsStore.cpp core/color/ColorAssetsStore.h
  core/animation/Animation.cpp core/animation/Animation.h
  core/animation/AnimationOps.cpp core/animation/AnimationOps.h
  core/animation/AnimationVideo.cpp core/animation/AnimationVideo.h
  core/animation/VideoPictureLoader.cpp core/animation/VideoPictureLoader.h
  core/document/Document.cpp core/document/Document.h
  core/document/DocumentCommands.cpp core/document/DocumentCommands.h
  core/document/DocumentContent.h
  core/document/DocumentState.h
  core/document/EditError.cpp core/document/EditError.h
  core/document/NewCanvas.cpp core/document/NewCanvas.h
  core/document/LayerOps.cpp core/document/LayerOps.h
  core/document/Clipboard.cpp core/document/Clipboard.h
  core/document/SelectionEdits.cpp core/document/SelectionEdits.h
  core/document/TransformEdits.cpp core/document/TransformEdits.h
  core/document/Projection.cpp core/document/Projection.h
  core/document/FloatingPreview.cpp core/document/FloatingPreview.h
  core/document/TransformSession.cpp core/document/TransformSession.h
  core/effects/ColorEffect.cpp core/effects/ColorEffect.h
  core/effects/EffectJob.cpp core/effects/EffectJob.h
  core/effects/EffectRender.cpp core/effects/EffectRender.h
  core/effects/EffectSampling.cpp core/effects/EffectSampling.h
  core/effects/EffectSession.cpp core/effects/EffectSession.h
  core/effects/EffectSettings.cpp core/effects/EffectSettings.h
  core/io/DoodleFormat.cpp core/io/DoodleFormat.h
  core/io/ByteIO.h
  core/io/DocumentFormat.h
  core/io/FileError.cpp core/io/FileError.h
  core/io/FileIo.cpp core/io/FileIo.h
  core/io/ImageConvert.cpp core/io/ImageConvert.h
  core/io/ImageFormat.cpp core/io/ImageFormat.h
  core/io/JpegCodec.cpp core/io/JpegCodec.h
  core/io/JpegPreview.cpp core/io/JpegPreview.h
  core/io/FormatRegistry.cpp core/io/FormatRegistry.h
  core/io/PluginFormat.cpp core/io/PluginFormat.h
  core/io/PluginLoader.cpp core/io/PluginLoader.h
  core/io/PsdFormat.cpp core/io/PsdFormat.h
  core/io/RecoveryStore.cpp core/io/RecoveryStore.h
  core/layers/BlendMode.cpp core/layers/BlendMode.h
  core/layers/BlendOps.cpp core/layers/BlendOps.h
  core/layers/CanvasBackground.cpp core/layers/CanvasBackground.h
  core/layers/Compositor.cpp core/layers/Compositor.h
  core/layers/LayerId.h
  core/layers/LayerPaper.h
  core/layers/PaperEffect.h
  core/layers/LayerStack.cpp core/layers/LayerStack.h
  core/paint/SelectionStrokeController.cpp core/paint/SelectionStrokeController.h
  core/paint/StrokeController.cpp core/paint/StrokeController.h
  core/paint/StrokeMemory.cpp core/paint/StrokeMemory.h
  core/paint/StrokeSmoother.cpp core/paint/StrokeSmoother.h
  core/paint/CloneStamp.cpp core/paint/CloneStamp.h
  core/paint/QuickShape.cpp core/paint/QuickShape.h
  core/raster/Channel.h core/raster/TiledImage.cpp core/raster/TiledImage.h
  core/stroke/EventBacklog.cpp core/stroke/EventBacklog.h
  core/stroke/PaintTimer.cpp core/stroke/PaintTimer.h
  core/stroke/StrokeWorker.cpp core/stroke/StrokeWorker.h
  core/util/ThreadPriority.cpp core/util/ThreadPriority.h
  core/stroke/RebuiltStroke.cpp core/stroke/RebuiltStroke.h
  core/stroke/RoundTip.cpp core/stroke/RoundTip.h
  core/stroke/PaperGrain.cpp core/stroke/PaperGrain.h
  core/stroke/Bristles.cpp core/stroke/Bristles.h
  core/stroke/StampForm.cpp core/stroke/StampForm.h
  core/stroke/PressureCurve.cpp core/stroke/PressureCurve.h
  core/stroke/StampTip.cpp core/stroke/StampTip.h
  core/stroke/DabTip.cpp core/stroke/DabTip.h
  # DabTip.cpp's parts, which it includes: listed so they show with it, not compiled on their own
  core/stroke/DabTipValues.inc core/stroke/DabTipCore.inc core/stroke/DabTipLanes.inc core/stroke/DabTipEvents.inc core/stroke/DabTipBrushGroups.inc core/stroke/DabTipRecords.inc core/stroke/DabTipMixing.inc core/stroke/DabTipLaying.inc core/stroke/DabTipGrids.inc core/stroke/DabTipStamps.inc core/stroke/DabTipSmudge.inc core/stroke/DabTipBlur.inc
  core/stroke/StrokeBuffer.cpp core/stroke/StrokeBuffer.h
  core/stroke/StrokePainter.h
  core/stroke/StrokeMerge.cpp core/stroke/StrokeMerge.h
  core/stroke/StrokePath.cpp core/stroke/StrokePath.h
  core/stroke/StrokeRecording.cpp core/stroke/StrokeRecording.h
  core/stroke/StrokeReplay.cpp core/stroke/StrokeReplay.h
  core/stroke/TimingLog.cpp core/stroke/TimingLog.h
  core/stroke/Variant.cpp core/stroke/Variant.h
  core/selection/MagicWand.cpp core/selection/MagicWand.h core/selection/WandMode.h
  core/selection/SelectionMask.cpp core/selection/SelectionMask.h
  core/selection/SelectionOps.cpp core/selection/SelectionOps.h
  core/selection/SelectionOutline.cpp core/selection/SelectionOutline.h
  core/transform/Lift.cpp core/transform/Lift.h
  core/transform/Resample.cpp core/transform/Resample.h
  core/options/AppOptions.cpp core/options/AppOptions.h
  core/options/KeyTable.cpp core/options/KeyTable.h
  core/tools/FixedTool.cpp core/tools/FixedTool.h
  core/tools/SizeList.cpp core/tools/SizeList.h
  core/tools/ToolBox.cpp core/tools/ToolBox.h
  core/tools/ToolBoxJson.cpp core/tools/ToolBoxJson.h
  core/tools/ToolBoxStore.cpp core/tools/ToolBoxStore.h
  core/util/CpuCores.h
  core/util/ParallelFor.h
  core/util/WorkerPool.cpp core/util/WorkerPool.h
  core/util/Result.h
  core/timelapse/Timelapse.cpp core/timelapse/Timelapse.h
  core/timelapse/TimelapseExport.cpp core/timelapse/TimelapseExport.h
  core/timelapse/TimelapseFile.cpp core/timelapse/TimelapseFile.h
  core/timelapse/TimelapseRecorder.cpp core/timelapse/TimelapseRecorder.h
  core/timelapse/TimelapseReplay.cpp core/timelapse/TimelapseReplay.h
  core/ffmpeg/ExternalProcess.cpp core/ffmpeg/ExternalProcess.h
  core/ffmpeg/Ffmpeg.cpp core/ffmpeg/Ffmpeg.h
  core/ffmpeg/FrameEncoder.cpp core/ffmpeg/FrameEncoder.h
  core/view/ViewNavigator.cpp core/view/ViewNavigator.h
  core/view/ViewState.h
  core/view/ViewText.cpp core/view/ViewText.h
  core/input/InputLog.cpp core/input/InputLog.h
  core/util/ProjectLinks.cpp core/util/ProjectLinks.h
  core/util/BuildInfo.cpp core/util/BuildInfo.h
  core/util/UpdateCheck.cpp core/util/UpdateCheck.h
  core/util/BuildInfoValues.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuildInfoValues.cpp
  core/input/InputSample.h
  core/input/TabletStats.cpp core/input/TabletStats.h
)
target_include_directories(doodlebold_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The plugin interface, a C header of its own (plugins/include/).
target_include_directories(doodlebold_core PUBLIC ${CMAKE_SOURCE_DIR}/plugins/include)
# libjpeg-turbo writes the JPEG files; CMake's FindJPEG finds it on every platform.
find_package(JPEG REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(doodlebold_core PUBLIC Qt6::Core Qt6::Gui Threads::Threads)
target_link_libraries(doodlebold_core PRIVATE JPEG::JPEG doodlebold_miniz)
# Hash characters shown: 7, git's and GitHub's own short length, so a reported hash reads as it does there.
set(doodlebold_commit_length 7)
# The commit only where GitHub names it, so no build needs git; a local build shows the version alone.
set(DOODLEBOLD_VERSION "${DOODLEBOLD_DISPLAY_VERSION}")
string(SUBSTRING "$ENV{GITHUB_SHA}" 0 ${doodlebold_commit_length} DOODLEBOLD_COMMIT)
configure_file(core/util/BuildInfoValues.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuildInfoValues.cpp @ONLY)
doodlebold_set_warnings(doodlebold_core)
# The painting files use AVX2: only these files get the flag, so the rest of the program, main() among it, still starts on any
# x86-64 processor and can say this one isn't supported (platform::processorIsSupported). -mavx2 alone, never -march=x86-64-v3 or
# -mfma: fused multiply-adds would move pixels (cmake/FloatingPoint.cmake).
set_source_files_properties(core/stroke/DabTip.cpp core/stroke/StampTip.cpp core/stroke/StrokeMerge.cpp
  PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/arch:AVX2,-mavx2>")

# doodlebold_ui: widgets. Platform-specific code lives only in platform/.
add_library(doodlebold_ui STATIC
  ui/MainWindow.cpp ui/MainWindow.h
  ui/MainWindowCommands.cpp
  ui/EffectController.cpp ui/EffectController.h
  ui/EffectDialog.cpp ui/EffectDialog.h
  ui/MainWindowKeys.cpp
  ui/MainWindowAnimation.cpp
  ui/animation/AnimationController.cpp ui/animation/AnimationController.h
  ui/animation/AnimationDialogs.cpp ui/animation/AnimationDialogs.h
  ui/animation/FrameStrip.cpp ui/animation/FrameStrip.h
  ui/animation/SoundPlayback.cpp ui/animation/SoundPlayback.h
  ui/animation/TimelinePanel.cpp ui/animation/TimelinePanel.h
  ui/animation/TrackHeaders.cpp ui/animation/TrackHeaders.h
  ui/animation/TimeRuler.cpp ui/animation/TimeRuler.h
  ui/animation/TrackLabels.h
  ui/animation/TimelineTitleButtons.cpp ui/animation/TimelineTitleButtons.h
  ui/MenuSpec.cpp ui/MenuSpec.h
  ui/CanvasView.cpp ui/CanvasView.h
  ui/FileController.cpp ui/FileController.h
  ui/JpegQualityDialog.cpp ui/JpegQualityDialog.h
  ui/NewCanvasDialog.cpp ui/NewCanvasDialog.h
  ui/MemoryNotice.cpp ui/MemoryNotice.h
  ui/UpdateNotice.cpp ui/UpdateNotice.h
  ui/OptionsDialog.cpp ui/OptionsDialog.h
  ui/ResourcesPage.cpp ui/ResourcesPage.h
  ui/PackImportDialog.cpp ui/PackImportDialog.h
  ui/StrokeTimingReadout.cpp ui/StrokeTimingReadout.h
  ui/KeysPage.cpp ui/KeysPage.h
  ui/HoverWheelGuard.cpp ui/HoverWheelGuard.h
  ui/Components.cpp ui/Components.h
  ui/ButtonTips.cpp ui/ButtonTips.h
  ui/QuickBar.cpp ui/QuickBar.h
  ui/FloatingLayerRenderer.cpp ui/FloatingLayerRenderer.h
  ui/DocumentTabs.cpp ui/DocumentTabs.h
  ui/ViewSelector.cpp ui/ViewSelector.h
  ui/UsageMeters.cpp ui/UsageMeters.h
  ui/Toast.cpp ui/Toast.h
  ui/AboutDialog.cpp ui/AboutDialog.h
  ui/CrashReportDialog.cpp ui/CrashReportDialog.h
  ui/RecoveryController.cpp ui/RecoveryController.h
  ui/RecoveryDialog.cpp ui/RecoveryDialog.h
  ui/SelectionGrowDialog.cpp ui/SelectionGrowDialog.h
  ui/TabletDiagnosticsDialog.cpp ui/TabletDiagnosticsDialog.h
  ui/TabletInput.cpp ui/TabletInput.h
  ui/color/AssetListPopup.cpp ui/color/AssetListPopup.h
  ui/color/ColorSliderViews.cpp ui/color/ColorSliderViews.h
  ui/color/ColorWheelView.cpp ui/color/ColorWheelView.h
  ui/color/SwatchesView.cpp ui/color/SwatchesView.h
  ui/panels/ColorPanel.cpp ui/panels/ColorPanel.h
  ui/panels/FixedToolPanels.cpp ui/panels/FixedToolPanels.h
  ui/panels/BucketPanel.cpp ui/panels/BucketPanel.h
  ui/panels/ControlTips.cpp ui/panels/ControlTips.h
  ui/panels/LayerPanel.cpp ui/panels/LayerPanel.h
  ui/panels/LayerTreeModel.cpp ui/panels/LayerTreeModel.h
  ui/panels/BrushPreviewStrip.cpp ui/panels/BrushPreviewStrip.h
  ui/panels/BrushResourceList.cpp ui/panels/BrushResourceList.h
  ui/panels/BrushResourceSection.cpp ui/panels/BrushResourceSection.h
  ui/panels/ParameterGrid.h
  ui/panels/RowHandle.cpp ui/panels/RowHandle.h
  ui/panels/NavigatorPanel.cpp ui/panels/NavigatorPanel.h
  ui/panels/StabilizerCombo.cpp ui/panels/StabilizerCombo.h
  ui/panels/ToolIcons.cpp ui/panels/ToolIcons.h
  ui/panels/ToolPanel.cpp ui/panels/ToolPanel.h
  ui/panels/ToolParameters.cpp ui/panels/ToolParameters.h
  ui/panels/ToolTray.cpp ui/panels/ToolTray.h
  ui/GlassPopups.cpp ui/GlassPopups.h
  ui/PanelCard.cpp ui/PanelCard.h
  ui/ColumnDivider.cpp ui/ColumnDivider.h
  ui/Theme.cpp ui/Theme.h
  ui/ValueBar.cpp ui/ValueBar.h
  ui/DoodleboldStyle.cpp ui/DoodleboldStyle.h
  ui/timelapse/RecordingPill.cpp ui/timelapse/RecordingPill.h
  ui/timelapse/TimelapseController.cpp ui/timelapse/TimelapseController.h
  ui/timelapse/TimelapseDialogs.cpp ui/timelapse/TimelapseDialogs.h
  ui/timelapse/TimelapseReplayView.cpp ui/timelapse/TimelapseReplayView.h
  ui/tools/CanvasTool.h
  ui/tools/CanvasCursor.cpp ui/tools/CanvasCursor.h
  ui/tools/SelectionOverlay.cpp ui/tools/SelectionOverlay.h
  ui/tools/SelectionPenTool.cpp ui/tools/SelectionPenTool.h
  ui/tools/BucketTool.cpp ui/tools/BucketTool.h
  ui/tools/SelectionTools.cpp ui/tools/SelectionTools.h
  ui/tools/TextTool.cpp ui/tools/TextTool.h
  ui/tools/QuickShapePill.cpp ui/tools/QuickShapePill.h
  ui/tools/QuickShapeTool.cpp ui/tools/QuickShapeTool.h
  ui/tools/TransformTools.cpp ui/tools/TransformTools.h
  platform/BackgroundBlur.cpp platform/BackgroundBlur.h
  platform/CrashReport.cpp platform/CrashReport.h
  platform/PenCursor.cpp platform/PenCursor.h
  platform/Platform.cpp platform/Platform.h
  platform/SystemUsage.cpp platform/SystemUsage.h
)
# The pen's cursor on Wayland (platform/PenCursor): Doodlebold's own tablet-v2 / cursor-shape-v1 client code, generated
# with wayland-scanner where wayland-client and wayland-protocols are installed. Without them PenCursor is a stub.
if(UNIX AND NOT APPLE)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
  endif()
endif()
if(WAYLAND_CLIENT_FOUND AND WAYLAND_PROTOCOLS_DIR AND WAYLAND_SCANNER)
  enable_language(C)
  set(wayland_gen ${CMAKE_CURRENT_BINARY_DIR}/wayland)
  file(MAKE_DIRECTORY ${wayland_gen})
  set(wayland_sources)
  foreach(protocol unstable/tablet/tablet-unstable-v2 staging/cursor-shape/cursor-shape-v1)
    get_filename_component(name ${protocol} NAME)
    set(xml ${WAYLAND_PROTOCOLS_DIR}/${protocol}.xml)
    add_custom_command(OUTPUT ${wayland_gen}/${name}-client.h ${wayland_gen}/${name}.c
      COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${wayland_gen}/${name}-client.h
      COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${wayland_gen}/${name}.c
      DEPENDS ${xml} VERBATIM)
    list(APPEND wayland_sources ${wayland_gen}/${name}-client.h ${wayland_gen}/${name}.c)
  endforeach()
  target_sources(doodlebold_ui PRIVATE ${wayland_sources})
  set_source_files_properties(${wayland_sources} PROPERTIES COMPILE_OPTIONS "-w")  # generated code
  target_include_directories(doodlebold_ui PRIVATE ${wayland_gen})
  target_link_libraries(doodlebold_ui PRIVATE PkgConfig::WAYLAND_CLIENT)
  target_compile_definitions(doodlebold_ui PRIVATE DOODLEBOLD_WAYLAND_PEN_CURSOR=1)
  message(STATUS "Wayland pen cursor: on (${WAYLAND_PROTOCOLS_DIR})")
  # Blur behind translucent popups (platform/BackgroundBlur): ext-background-effect-v1 (staging in recent
  # wayland-protocols; 1.49 here). Without it BackgroundBlur is a stub and the popups stay opaque.
  set(blur_xml ${WAYLAND_PROTOCOLS_DIR}/staging/ext-background-effect/ext-background-effect-v1.xml)
  if(EXISTS ${blur_xml})
    set(blur_out ${wayland_gen}/ext-background-effect-v1-client.h ${wayland_gen}/ext-background-effect-v1.c)
    add_custom_command(OUTPUT ${blur_out}
      COMMAND ${WAYLAND_SCANNER} client-header ${blur_xml} ${wayland_gen}/ext-background-effect-v1-client.h
      COMMAND ${WAYLAND_SCANNER} private-code ${blur_xml} ${wayland_gen}/ext-background-effect-v1.c
      DEPENDS ${blur_xml} VERBATIM)
    target_sources(doodlebold_ui PRIVATE ${blur_out})
    set_source_files_properties(${blur_out} PROPERTIES COMPILE_OPTIONS "-w")
    target_compile_definitions(doodlebold_ui PRIVATE DOODLEBOLD_WAYLAND_BLUR=1)
    message(STATUS "Wayland background blur: on")
  else()
    message(STATUS "Wayland background blur: off (wayland-protocols without ext-background-effect-v1)")
  endif()
else()
  message(STATUS "Wayland pen cursor: off (wayland-client, wayland-protocols or wayland-scanner not found)")
endif()
target_include_directories(doodlebold_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(doodlebold_ui PUBLIC doodlebold_core Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg)
# Only the update check goes on the network (ui/UpdateNotice), to read GitHub's latest release.
target_link_libraries(doodlebold_ui PRIVATE Qt6::Network)
# The animation's sound clips play with it through Qt Multimedia where it is installed; without it the animation plays silently
# (ui/animation/SoundPlayback). Saving and exporting the clips needs neither.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Multimedia)
if(TARGET Qt6::Multimedia)
  target_link_libraries(doodlebold_ui PRIVATE Qt6::Multimedia)
  target_compile_definitions(doodlebold_ui PRIVATE DOODLEBOLD_HAS_MULTIMEDIA=1)
  message(STATUS "Animation sound playback: on (Qt Multimedia)")
else()
  message(STATUS "Animation sound playback: off (Qt Multimedia not found)")
endif()
if(WIN32)
  target_link_libraries(doodlebold_ui PRIVATE psapi)  # GetProcessMemoryInfo (platform/SystemUsage)
  target_link_libraries(doodlebold_ui PRIVATE dwmapi)  # the acrylic backdrop (platform/BackgroundBlur)
  target_link_libraries(doodlebold_ui PRIVATE opengl32)  # CanvasView calls glTexImage2D itself; MinGW's Qt doesn't bring it in
endif()
doodlebold_set_warnings(doodlebold_ui)

# Resources are compiled into the executable: an unreferenced resource object inside a static library would be
# dropped by the linker.
add_executable(doodlebold WIN32 MACOSX_BUNDLE app/main.cpp ${CMAKE_SOURCE_DIR}/resources/doodlebold.qrc)
if(WIN32)
  target_sources(doodlebold PRIVATE ${CMAKE_SOURCE_DIR}/resources/doodlebold.rc)  # the icon of the .exe
endif()
target_link_libraries(doodlebold PRIVATE doodlebold_ui)
doodlebold_set_warnings(doodlebold)
set_target_properties(doodlebold PROPERTIES OUTPUT_NAME doodlebold)

include(GNUInstallDirs)
install(TARGETS doodlebold RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
if(MSVC)
  # The MSVC runtime goes next to doodlebold.exe: without it, Windows lacking the Visual C++ Redistributable can't start it.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
  include(InstallRequiredSystemLibraries)
  # Qt's DLLs and plugins beside the exe, as windeployqt puts them (the windows-zip preset packs the result).
  qt_generate_deploy_app_script(TARGET doodlebold OUTPUT_SCRIPT qt_deploy NO_TRANSLATIONS)
  install(SCRIPT ${qt_deploy})
  # libjpeg-turbo's DLL too: Qt's deploy script copies only Qt's, and vcpkg copies it only into the build folder.
  install(FILES "$<TARGET_FILE_DIR:doodlebold>/jpeg62.dll" DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(MINGW)
  # MinGW has no windeployqt: cmake/MinGWDeploy.cmake copies Qt's plugins and every DLL the exe and they need from the sysroot.
  set(DOODLEBOLD_MINGW_ROOT "" CACHE PATH "The MinGW sysroot whose bin/ and lib/qt6/plugins/ hold what the zip ships")
  install(CODE "set(DOODLEBOLD_EXE \"$<TARGET_FILE:doodlebold>\")\nset(DOODLEBOLD_MINGW_ROOT \"${DOODLEBOLD_MINGW_ROOT}\")\nset(DOODLEBOLD_OBJDUMP \"${CMAKE_OBJDUMP}\")")
  install(SCRIPT ${CMAKE_SOURCE_DIR}/cmake/MinGWDeploy.cmake)
endif()
if(UNIX AND NOT APPLE)
  install(FILES ${CMAKE_SOURCE_DIR}/resources/doodlebold.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  foreach(size 16 24 32 48 64 128 256 512)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/sizes/doodlebold-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME doodlebold.png)
  endforeach()
  install(FILES ${CMAKE_SOURCE_DIR}/resources/doodlebold-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
          RENAME doodlebold.xml)
endif()
# Doodlebold's own MIT licence, shipped with every package.
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/doodlebold)
install(FILES ${CMAKE_SOURCE_DIR}/third_party/Inter-OFL.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/doodlebold)
# ... the LGPL / GPL texts that go with Qt and the libraries in the AppImage, and the list of everything third-party.
install(FILES ${CMAKE_SOURCE_DIR}/third_party/LGPL-3.0.txt ${CMAKE_SOURCE_DIR}/third_party/GPL-3.0.txt
              ${CMAKE_SOURCE_DIR}/third_party/LGPL-2.1.txt ${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/doodlebold)
