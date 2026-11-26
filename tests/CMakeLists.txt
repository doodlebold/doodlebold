find_package(GTest REQUIRED)
include(GoogleTest)

# Labels pick what a test preset runs (CMakePresets.json): unit, ui, tool-scripts, brush-pixels-subset, brush-pixels-all,
# brush-pixels-cores and speed-budget. tools/README.md says which preset runs which.
# The benches and hand-run probes are timing tools, not tests: built only when asked for, as tools/compare_brush_speed.py does.
option(DOODLEBOLD_BUILD_BENCHES "Build the benches and hand-run probes, and their speed-budget tests" OFF)

# Test code compiles at -O1 without debug info, about half the time; the code under test keeps its own flags.
add_compile_options("$<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:GNU,Clang>>:-O1;-g0>")

add_executable(doodlebold_unit_tests
  unit/test_stroke_painter.cpp
  unit/test_brush_settings.cpp
  unit/test_clone_stamp.cpp
  unit/test_quick_shape.cpp
  unit/test_stroke_smoother.cpp
  unit/test_rebuilt_stroke.cpp
  unit/test_event_backlog.cpp
  unit/test_dab_tip.cpp
  unit/test_stamp_tip.cpp
  unit/test_mixing_pictures.cpp
  unit/test_water_color_checks.cpp
  unit/test_stroke_replay.cpp
  unit/test_color_panel_core.cpp
  unit/test_text_content.cpp
  unit/test_document_undo.cpp
  unit/test_tablet_stats.cpp
  unit/test_tiled_image.cpp
  unit/test_off_canvas.cpp
  unit/test_toolbox.cpp
  unit/test_brush_resources.cpp
  unit/test_brush_pack.cpp
  unit/test_layer_stack.cpp
  unit/test_compositor.cpp
  unit/test_layer_paper.cpp
  unit/test_format_plugins.cpp
  unit/test_plugin_loader.cpp
  unit/test_ffmpeg.cpp
  unit/test_app_identity.cpp
  unit/test_layer_ops.cpp
  unit/test_view_navigator.cpp
  unit/test_view_text.cpp
  unit/test_cpu_cores.cpp
  unit/test_selection.cpp
  unit/test_area_fill.cpp
  unit/test_document_selection.cpp
  unit/test_selection_painting.cpp
  unit/test_transform.cpp
  unit/test_document_transform.cpp
  unit/test_fixed_tools.cpp
  unit/test_selection_outline.cpp
  unit/test_transform_session.cpp
  unit/test_transform_gpu_preview.cpp
  unit/test_doodle_format.cpp
  unit/test_animation.cpp
  unit/test_animation_video.cpp
  unit/test_image_formats.cpp
  unit/test_layer_commands.cpp
  unit/test_layer_masks.cpp
  unit/test_psd_format.cpp
  unit/test_canvas_background.cpp
  unit/test_new_canvas.cpp
  unit/test_jpeg_codec.cpp
  unit/test_jpeg_preview.cpp
  unit/test_timelapse.cpp
  unit/test_project_links.cpp
  unit/test_build_info.cpp
  unit/test_update_check.cpp
  unit/test_effects.cpp
  unit/test_recovery_store.cpp
  unit/IoTestSupport.h
  unit/FfmpegTestSupport.h
)
target_link_libraries(doodlebold_unit_tests PRIVATE doodlebold_core doodlebold_miniz GTest::gtest_main)
target_compile_definitions(doodlebold_unit_tests PRIVATE DOODLEBOLD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
doodlebold_set_warnings(doodlebold_unit_tests)
# Format plugins for test_plugin_loader, each in a folder of its own.
set(test_plugins "${CMAKE_CURRENT_BINARY_DIR}/test-plugins")
doodlebold_add_format_plugin(test_plugin_layer_dump OUTPUT_DIRECTORY "${test_plugins}/layer_dump" SOURCES plugins/layer_dump.c)
foreach(variant NO_ENTRY FUTURE_VERSION DECLINES BROKEN_FORMATS CLAIMS)
  string(TOLOWER ${variant} folder)
  doodlebold_add_format_plugin(test_plugin_${folder} OUTPUT_DIRECTORY "${test_plugins}/${folder}" SOURCES plugins/bad_plugins.c)
  target_compile_definitions(test_plugin_${folder} PRIVATE ${variant})
  list(APPEND test_plugin_targets test_plugin_${folder})
endforeach()
add_dependencies(doodlebold_unit_tests farbfeld test_plugin_layer_dump ${test_plugin_targets} doodlebold-plugin-host)
target_compile_definitions(doodlebold_unit_tests PRIVATE DOODLEBOLD_TEST_PLUGIN_DIR="${test_plugins}"
                           DOODLEBOLD_EXAMPLE_PLUGIN_DIR="$<TARGET_FILE_DIR:doodlebold>/plugins"
                           DOODLEBOLD_PLUGIN_HOST="$<TARGET_FILE:doodlebold-plugin-host>")
# A home of their own, as the UI tests have: the tests never see the real settings or files of this computer.
set(unit_home "${CMAKE_CURRENT_BINARY_DIR}/unit-home")
file(MAKE_DIRECTORY "${unit_home}")
gtest_discover_tests(doodlebold_unit_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60 PROPERTIES LABELS unit ENVIRONMENT
  "HOME=${unit_home};XDG_CONFIG_HOME=${unit_home}/config;XDG_DATA_HOME=${unit_home}/data;XDG_CACHE_HOME=${unit_home}/cache")

# UI tests: need QApplication (offscreen platform), so they live in their own executable.
add_executable(doodlebold_ui_tests ui/test_document_tabs.cpp ui/test_color_panel.cpp ui/test_text_layers.cpp ui/test_platform.cpp ui/test_theme.cpp ui/test_value_bar.cpp ui/test_glass_popups.cpp ui/test_dialog_glass.cpp ui/test_brush_resource_rows.cpp ui/test_timelapse_ui.cpp ui/test_timeline_ui.cpp ui/test_layer_panel.cpp
                               ui/test_canvas_background_stroke.cpp ui/test_navigator_panel.cpp ui/test_tool_panel.cpp ui/test_selection_tools.cpp ui/test_selection_menus.cpp ui/test_file_ui.cpp ui/test_menus.cpp ui/test_options.cpp ui/test_memory_notice.cpp ui/test_update_notice.cpp ui/test_quick_bar.cpp ui/test_window_margins.cpp ui/test_panel_divider.cpp ui/test_button_tips.cpp ui/test_about.cpp ui/test_crash_report.cpp ui/test_toast.cpp ui/test_held_keys.cpp ui/test_quick_shapes.cpp ui/test_effects_ui.cpp ui/test_recovery.cpp
                               ui/test_resources_page.cpp ui/test_bucket_and_keys.cpp ui/test_tool_tips.cpp ui/test_canvas_display.cpp
                               ui/ScreenshotSupport.h
                               ${CMAKE_SOURCE_DIR}/resources/doodlebold.qrc)  # icons and theme, as in the app
target_link_libraries(doodlebold_ui_tests PRIVATE doodlebold_ui GTest::gtest)
doodlebold_set_warnings(doodlebold_ui_tests)
target_compile_definitions(doodlebold_ui_tests PRIVATE DOODLEBOLD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# Eight shards run side by side; each gets its own home so the tests that reset settings never meet.
set(DOODLEBOLD_UI_SHARDS 8)
# Qt's own Linux builds have no OpenGL in the offscreen platform: there the UI tests need xcb on a virtual X display.
set(DOODLEBOLD_UI_TEST_PLATFORM "offscreen" CACHE STRING "Qt platform the UI tests run on")
math(EXPR last_shard "${DOODLEBOLD_UI_SHARDS} - 1")
foreach(shard RANGE ${last_shard})
  set(home "${CMAKE_CURRENT_BINARY_DIR}/ui-shard-${shard}")
  file(MAKE_DIRECTORY "${home}")
  add_test(NAME doodlebold_ui_tests_${shard} COMMAND doodlebold_ui_tests)
  set_tests_properties(doodlebold_ui_tests_${shard} PROPERTIES LABELS ui ENVIRONMENT
    "QT_QPA_PLATFORM=${DOODLEBOLD_UI_TEST_PLATFORM};GTEST_TOTAL_SHARDS=${DOODLEBOLD_UI_SHARDS};GTEST_SHARD_INDEX=${shard};HOME=${home};XDG_CONFIG_HOME=${home}/config;XDG_DATA_HOME=${home}/data;XDG_CACHE_HOME=${home}/cache")
endforeach()

if(DOODLEBOLD_BUILD_BENCHES)
  add_executable(doodlebold_transform_bench ui/transform_bench.cpp)
  target_link_libraries(doodlebold_transform_bench PRIVATE doodlebold_ui)
  doodlebold_set_warnings(doodlebold_transform_bench)

  add_executable(doodlebold_window_drag_bench ui/window_drag_bench.cpp)
  target_link_libraries(doodlebold_window_drag_bench PRIVATE doodlebold_ui)
  doodlebold_set_warnings(doodlebold_window_drag_bench)

  add_executable(doodlebold_stroke_view_bench ui/stroke_view_bench.cpp)
  target_link_libraries(doodlebold_stroke_view_bench PRIVATE doodlebold_ui)
  doodlebold_set_warnings(doodlebold_stroke_view_bench)

  add_executable(doodlebold_recorded_strokes_bench unit/recorded_strokes_bench.cpp)
  target_link_libraries(doodlebold_recorded_strokes_bench PRIVATE doodlebold_core)
  doodlebold_set_warnings(doodlebold_recorded_strokes_bench)
  add_test(NAME recorded_3k_strokes COMMAND doodlebold_recorded_strokes_bench --check ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/recorded-3k-strokes.jsonl
           ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/recorded-3k-tools.json)
  set_tests_properties(recorded_3k_strokes PROPERTIES LABELS speed-budget RUN_SERIAL TRUE)


  add_executable(doodlebold_stroke_speed_bench unit/stroke_speed_bench.cpp)
  target_link_libraries(doodlebold_stroke_speed_bench PRIVATE doodlebold_core)
  doodlebold_set_warnings(doodlebold_stroke_speed_bench)
  add_test(NAME stroke_speed_small_tips COMMAND doodlebold_stroke_speed_bench ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pen-sample.jsonl --small-tips)
  set_tests_properties(stroke_speed_small_tips PROPERTIES LABELS speed-budget RUN_SERIAL TRUE)

  # Renders one reference case to a PNG, by hand.
  add_executable(doodlebold_stamp_survey unit/stamp_survey.cpp)
  target_link_libraries(doodlebold_stamp_survey PRIVATE doodlebold_core Qt6::Gui)
  doodlebold_set_warnings(doodlebold_stamp_survey)

  add_executable(doodlebold_replay_case unit/replay_case.cpp)
  target_link_libraries(doodlebold_replay_case PRIVATE doodlebold_core)
  doodlebold_set_warnings(doodlebold_replay_case)

  add_executable(doodlebold_tile_size_bench unit/tile_size_bench.cpp)
  target_link_libraries(doodlebold_tile_size_bench PRIVATE doodlebold_core)
  doodlebold_set_warnings(doodlebold_tile_size_bench)

  add_executable(doodlebold_new_canvas_bench ui/new_canvas_bench.cpp)
  target_link_libraries(doodlebold_new_canvas_bench PRIVATE doodlebold_ui)
  doodlebold_set_warnings(doodlebold_new_canvas_bench)
  add_executable(doodlebold_selection_pen_bench ui/selection_pen_bench.cpp)
  target_link_libraries(doodlebold_selection_pen_bench PRIVATE doodlebold_ui)
  doodlebold_set_warnings(doodlebold_selection_pen_bench)


  add_executable(doodlebold_big_canvas_bench unit/big_canvas_bench.cpp)
  target_link_libraries(doodlebold_big_canvas_bench PRIVATE doodlebold_core)
  doodlebold_set_warnings(doodlebold_big_canvas_bench)
endif()

# The brush pixels, the golden results (tools/README.md): every case painted again and compared with the checksums in
# fixtures/brush-pixels, to the bit. brush-pixels-all runs every case in parts, so a failure names its tools; one at a time, as each
# uses every core and the widest strokes hold up to about 2.6 GB. brush-pixels-subset is the part worth running before every commit:
# every tool at 200 px and at 1500 px over paint, and two stabilizer sweeps. brush-pixels-cores paints every case again on 2 and on 4
# threads, as machines with that many cores do: the pixels mustn't depend on it.
add_executable(doodlebold_brush_pixels unit/brush_pixels.cpp)
target_link_libraries(doodlebold_brush_pixels PRIVATE doodlebold_core)
target_compile_definitions(doodlebold_brush_pixels PRIVATE DOODLEBOLD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
doodlebold_set_warnings(doodlebold_brush_pixels)
# checksums.txt is Linux x86-64's with glibc; a platform whose maths library rounds differently names its own (unit/brush_pixels.cpp).
set(DOODLEBOLD_BRUSH_PIXELS_CHECKSUMS "checksums.txt" CACHE STRING "The checksum file in tests/fixtures/brush-pixels to compare with")
set(brush_pixels_check doodlebold_brush_pixels --check ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/brush-pixels
    --checksums ${DOODLEBOLD_BRUSH_PIXELS_CHECKSUMS} --out ${CMAKE_CURRENT_BINARY_DIR}/brush-pixels-differences)
foreach(part grid/pencil- grid/brush- grid/watercolor- grid/marker- grid/eraser- grid/blur- grid/smudge- grid/pixelart- grid/clone-
             forms/ stabilizer/ background/ recorded/ backlog/ settings/)
  string(REGEX REPLACE "[-/]$" "" name ${part})
  string(REPLACE "/" "_" name ${name})
  add_test(NAME brush_pixels_${name} COMMAND ${brush_pixels_check} --only ${part})
  set_tests_properties(brush_pixels_${name} PROPERTIES LABELS brush-pixels-all RUN_SERIAL TRUE)
endforeach()
set(subset_parts -200px-s4- -1500px-s4-painted stabilizer/recorded-c1-pencil stabilizer/recorded-c5-brush)
set(subset_names 200px 1500px_painted stabilizer_pencil stabilizer_brush)
foreach(part name IN ZIP_LISTS subset_parts subset_names)
  add_test(NAME brush_pixels_subset_${name} COMMAND ${brush_pixels_check} --only ${part})
  set_tests_properties(brush_pixels_subset_${name} PROPERTIES LABELS brush-pixels-subset RUN_SERIAL TRUE)
endforeach()
foreach(cores 2 4)
  add_test(NAME brush_pixels_on_${cores}_cores COMMAND ${brush_pixels_check} --cores ${cores})
  set_tests_properties(brush_pixels_on_${cores}_cores PROPERTIES LABELS brush-pixels-cores RUN_SERIAL TRUE)
endforeach()

# The Python tools' own tests (among them the brush pictures no golden case paints) and the UI theme-token lint (tools/lint_ui_theme_tokens.py).
find_package(Python3 3.10 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME tool_scripts COMMAND Python3::Interpreter -B -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools/tests)
  add_test(NAME ui_theme_tokens COMMAND Python3::Interpreter -B ${CMAKE_SOURCE_DIR}/tools/lint_ui_theme_tokens.py)
  set_tests_properties(tool_scripts ui_theme_tokens PROPERTIES LABELS tool-scripts)
endif()
