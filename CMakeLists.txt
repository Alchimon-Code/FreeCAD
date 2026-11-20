# SPDX-License-Identifier: LGPL-2.1-or-later
cmake_minimum_required(VERSION 3.24)
project(PlaneDraft VERSION 0.2.0 LANGUAGES C CXX)

include(CTest)
option(PLANEDRAFT_BUILD_GUI "Build the Qt application and command host" ON)
option(PLANEDRAFT_TEST_EXCHANGE_AUDIT "Verify DXF and page exports with external Python test readers" OFF)
option(PLANEDRAFT_TEST_GL "Register the desktop OpenGL framebuffer smoke test in CTest" OFF)
option(PLANEDRAFT_SANITIZE "Instrument host, modules and tests with ASan/UBSan (GCC/Clang)" OFF)
option(PLANEDRAFT_BUNDLE_LIBREDWG
    "Build the pinned LibreDWG converters and ship them beside the application" ON)
option(PD_ENABLE_LIBREDWG "Exchange DWG through separate LibreDWG processes (never linked)" ON)
if(PLANEDRAFT_SANITIZE)
    if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Use a GCC/Clang headless build for ASan+UBSan")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
if(MSVC)
    add_compile_options(/utf-8 /W4 $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(PD_ENABLE_LIBREDWG AND PLANEDRAFT_BUNDLE_LIBREDWG)
    include(cmake/libredwg.cmake)
endif()

add_library(pd_core STATIC
    core/math/geometry.cpp core/math/curves.cpp core/math/edges.cpp
    core/edit/plan.cpp core/edit/trim_extend.cpp core/edit/offset_fillet.cpp core/edit/construct.cpp
    core/drafting/dimension.cpp core/drafting/hatch.cpp core/drafting/drawing.cpp core/drafting/layout.cpp
    core/entities/primitives.cpp core/entities/block.cpp
    core/spatial/r_tree.cpp core/document/transaction.cpp core/document/document.cpp)
target_compile_features(pd_core PUBLIC cxx_std_20)
target_include_directories(pd_core PUBLIC core/include)
if(WIN32)
    target_compile_definitions(pd_core PUBLIC NOMINMAX)
endif()

add_subdirectory(io)

# Analytic picking and CPU mesh generation are testable even in the Qt-free preset.
add_library(pd_view_math STATIC render/camera2d.cpp render/strokes.cpp render/tessellation.cpp
    render/snap_engine.cpp render/selection_manager.cpp render/scene_cache.cpp render/overlay.cpp)
target_include_directories(pd_view_math PUBLIC render)
target_link_libraries(pd_view_math PUBLIC pd_core)

add_library(pd_sdk INTERFACE)
target_include_directories(pd_sdk INTERFACE sdk/include)
# Only this interface target is a third-party ABI. No internal C++ class crosses a DLL boundary.
add_library(pd_extension_host STATIC sdk/host/extension_host.cpp)
target_include_directories(pd_extension_host PUBLIC sdk/host)
target_link_libraries(pd_extension_host PUBLIC pd_core)
add_library(pd_plugins STATIC plugins/plugin_manager.cpp plugins/native_call.cpp plugins/geometry_abi.cpp)
target_include_directories(pd_plugins PUBLIC plugins)
target_link_libraries(pd_plugins PUBLIC pd_extension_host PRIVATE pd_sdk ${CMAKE_DL_LIBS})
add_library(pd_scripting STATIC scripting/lisp_reader.cpp scripting/lisp_engine.cpp scripting/lisp_dom.cpp)
target_include_directories(pd_scripting PUBLIC scripting)
target_link_libraries(pd_scripting PUBLIC pd_extension_host)
function(pd_module target source folder)
    add_library(${target} MODULE ${source})
    target_link_libraries(${target} PRIVATE pd_sdk)
    target_compile_definitions(${target} PRIVATE PD_BUILD_PLUGIN)
    set_target_properties(${target} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${folder}" LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${folder}")
endfunction()
pd_module(sample_line plugins/sample/sample_line.cpp plugins)
pd_module(frame_generator examples/plugins/frame_generator/frame.c plugins)
configure_file(examples/scripts/pattern.pdl examples/scripts/pattern.pdl COPYONLY)

if(PLANEDRAFT_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Svg LinguistTools)
    if(BUILD_TESTING)
        # Only the UI test binary drives real key/mouse events.
        find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    endif()
    add_library(pd_editor STATIC editor/edit_controller.cpp editor/drafting_commands.cpp)
    target_include_directories(pd_editor PUBLIC editor)
    target_link_libraries(pd_editor PUBLIC pd_core Qt6::Core)
    set_target_properties(pd_editor PROPERTIES AUTOMOC ON)
    add_library(pd_commands STATIC ui/command_session.cpp ui/layout_commands.cpp ui/language.cpp)
    target_include_directories(pd_commands PUBLIC ui)
    target_link_libraries(pd_commands PUBLIC pd_editor pd_io pd_plugins pd_scripting Qt6::Core)

    add_library(pd_render STATIC render/viewport.cpp render/scene_renderer.cpp render/qt_text.cpp
        render/pd_viewport_shaders.qrc)
    set_target_properties(pd_render PROPERTIES AUTORCC ON)
    target_include_directories(pd_render PUBLIC render)
    target_link_libraries(pd_render PUBLIC pd_editor pd_view_math Qt6::OpenGLWidgets Qt6::OpenGL)

    add_library(pd_ui STATIC ui/main_window.cpp ui/desktop_chrome.cpp ui/drawing_page.cpp
        ui/property_inspector.cpp ui/layer_manager.cpp ui/ribbon.cpp resources/pd_icons.qrc)
    set_target_properties(pd_ui PROPERTIES AUTORCC ON)
    target_link_libraries(pd_ui PUBLIC pd_commands pd_render Qt6::Widgets Qt6::Svg)
    set_target_properties(pd_commands pd_render pd_ui PROPERTIES AUTOMOC ON)

    qt_add_executable(PlaneDraft ui/main.cpp)
    target_compile_definitions(PlaneDraft PRIVATE PD_VERSION="${PROJECT_VERSION}")
    set_target_properties(PlaneDraft PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(PlaneDraft PRIVATE pd_ui)
    if(WIN32)
        # vcpkg handles linked DLLs; the QPA backend is loaded by name, not linked.
        function(pd_copy_qpa target backend)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${backend}>"
                        "$<TARGET_FILE_DIR:${target}>/platforms/" VERBATIM)
        endfunction()
        pd_copy_qpa(PlaneDraft Qt6::QWindowsIntegrationPlugin)
    endif()

    # Our command catalog is embedded. Qt's standard file/color/dialog buttons use a separate
    # replaceable upstream catalog (also beside the executable in the development tree).
    find_file(pd_qtbase_ru qtbase_ru.qm HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/translations/Qt6"
        PATH_SUFFIXES translations share/qt6/translations REQUIRED)
    configure_file("${pd_qtbase_ru}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/translations/qtbase_ru.qm" COPYONLY)

    set(pd_ts resources/i18n/planedraft_ru.ts)
    qt_add_lupdate(PlaneDraft TS_FILES ${pd_ts} OPTIONS -locations none
        SOURCES ui/main.cpp ui/main_window.cpp ui/desktop_chrome.cpp ui/layer_manager.cpp ui/property_inspector.cpp ui/ribbon.cpp
                ui/command_session.cpp render/viewport.cpp editor/edit_controller.cpp editor/drafting_commands.cpp ui/layout_commands.cpp)
    qt_add_lrelease(PlaneDraft TS_FILES ${pd_ts} QM_FILES_OUTPUT_VARIABLE pd_catalog)
    function(pd_embed_catalog target)
        qt_add_resources(${target} "${target}_catalog" PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${pd_catalog})
    endfunction()
    pd_embed_catalog(PlaneDraft)

    # Maintainers can reproduce the documentation PNGs in a desktop GL session.
    # Not built by default and never installed in the runtime package.
    add_executable(pd_capture_workspace EXCLUDE_FROM_ALL tools/capture_workspace.cpp)
    target_link_libraries(pd_capture_workspace PRIVATE pd_ui)
    pd_embed_catalog(pd_capture_workspace)
    add_dependencies(pd_capture_workspace PlaneDraft)
endif()

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_executable(pd_io_tests tests/io_test.cpp)
    target_link_libraries(pd_io_tests PRIVATE pd_io GTest::gtest_main)
    if(PD_ENABLE_LIBREDWG)
        add_executable(pd_converter_fixture tests/converter_fixture.cpp)
        if(MINGW)
            target_link_options(pd_converter_fixture PRIVATE -municode)
        endif()
        add_dependencies(pd_io_tests pd_converter_fixture)
        target_compile_definitions(pd_io_tests PRIVATE PD_TEST_WORKER="$<TARGET_FILE:pd_converter_fixture>")
        if((PD_DWG2DXF_EXECUTABLE AND PD_DXF2DWG_EXECUTABLE) OR TARGET dwg2dxf)
            target_compile_definitions(pd_io_tests PRIVATE PD_TEST_REAL_DWG)
        endif()
        # The pinned tarball also ships real AutoCAD-written samples for every DWG version.
        # They stay upstream GPLv3 test data: used from the source tree, never redistributed.
        if(EXISTS "${libredwg_SOURCE_DIR}/test/test-data/2018/Text.dwg")
            target_compile_definitions(pd_io_tests PRIVATE
                PD_TEST_DWG_SAMPLES="${libredwg_SOURCE_DIR}/test/test-data")
        endif()
    endif()
    if(PLANEDRAFT_TEST_EXCHANGE_AUDIT)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_executable(pd_sheet_fixture tests/sheet_fixture.cpp)
        target_link_libraries(pd_sheet_fixture PRIVATE pd_io)
        add_test(NAME sheet_audit COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/audit_sheet.py"
            "$<TARGET_FILE:pd_sheet_fixture>" "$<TARGET_FILE:pdconvert>")
    endif()
    add_test(NAME drawing_io COMMAND pd_io_tests)
    if(TARGET pd_dwg_tools)
        add_dependencies(pd_io_tests pd_dwg_tools)
    endif()
    add_test(NAME convert_example COMMAND pdconvert "${PROJECT_SOURCE_DIR}/examples/drawings/cover.dxf"
        "${PROJECT_BINARY_DIR}/cover.pdf")
    set_tests_properties(convert_example PROPERTIES PASS_REGULAR_EXPRESSION "8 entities, 3 layers, 0 skipped")
    add_executable(pd_core_tests tests/document_test.cpp tests/geometry_test.cpp tests/spatial_test.cpp)
    target_link_libraries(pd_core_tests PRIVATE pd_core GTest::gtest_main)
    add_test(NAME core COMMAND pd_core_tests)
    add_executable(pd_edit_tests tests/edit_test.cpp)
    target_link_libraries(pd_edit_tests PRIVATE pd_core GTest::gtest_main)
    add_test(NAME editing COMMAND pd_edit_tests)
    add_executable(pd_drafting_tests tests/drafting_test.cpp)
    target_link_libraries(pd_drafting_tests PRIVATE pd_core GTest::gtest_main)
    add_test(NAME drafting COMMAND pd_drafting_tests)
    add_executable(pd_view_tests tests/view_math_test.cpp tests/snap_scale_test.cpp)
    target_link_libraries(pd_view_tests PRIVATE pd_view_math GTest::gtest_main)
    add_test(NAME viewport_math COMMAND pd_view_tests)
    pd_module(short_table tests/short_table.c test-modules)
    pd_module(plugin_probe tests/plugin_probe.c test-modules)
    pd_module(legacy_edits tests/legacy_edits.c test-modules)
    pd_module(short_v2 tests/incompatible_v2.c test-modules)
    pd_module(wrong_version tests/incompatible_v2.c test-modules)
    target_compile_definitions(wrong_version PRIVATE PD_WRONG_VERSION)
    pd_module(failing_init tests/plugin_probe.c test-modules)
    target_compile_definitions(failing_init PRIVATE PD_FAIL_INIT)
    add_executable(pd_extension_tests tests/extensions_test.cpp)
    target_link_libraries(pd_extension_tests PRIVATE pd_plugins pd_scripting GTest::gtest_main)
    add_dependencies(pd_extension_tests short_v2 wrong_version legacy_edits plugin_probe failing_init frame_generator sample_line short_table)
    target_compile_definitions(pd_extension_tests PRIVATE PD_SAMPLE_PATH="$<TARGET_FILE:sample_line>"
        PD_PROBE_PATH="$<TARGET_FILE:plugin_probe>"
        PD_SHORT_V2_PATH="$<TARGET_FILE:short_v2>" PD_WRONG_VERSION_PATH="$<TARGET_FILE:wrong_version>"
        PD_LEGACY_EDITS_PATH="$<TARGET_FILE:legacy_edits>" PD_FAIL_PATH="$<TARGET_FILE:failing_init>" PD_SHORT_TABLE_PATH="$<TARGET_FILE:short_table>"
        PD_FRAME_PATH="$<TARGET_FILE:frame_generator>")
    add_test(NAME extensions COMMAND pd_extension_tests)
    if(PLANEDRAFT_BUILD_GUI)
        add_executable(pd_session_tests tests/session_test.cpp tests/editor_session_test.cpp)
        target_link_libraries(pd_session_tests PRIVATE pd_commands GTest::gtest)
        add_dependencies(pd_session_tests sample_line short_table frame_generator)
        target_compile_definitions(pd_session_tests PRIVATE PD_SAMPLE_PATH="$<TARGET_FILE:sample_line>"
            PD_SHORT_TABLE_PATH="$<TARGET_FILE:short_table>" PD_FRAME_PATH="$<TARGET_FILE:frame_generator>"
            PD_SCRIPT_PATH="${PROJECT_BINARY_DIR}/examples/scripts/pattern.pdl")
        pd_embed_catalog(pd_session_tests)
        add_test(NAME command_host COMMAND pd_session_tests)

        add_executable(pd_window_tests tests/window_test.cpp tests/editor_window_test.cpp tests/workspace_test.cpp
            tests/ribbon_test.cpp)
        target_link_libraries(pd_window_tests PRIVATE pd_ui GTest::gtest Qt6::Test)
        pd_embed_catalog(pd_window_tests)
        if(WIN32)
            pd_copy_qpa(pd_window_tests Qt6::QMinimalIntegrationPlugin)
        endif()
        add_test(NAME ui_language COMMAND pd_window_tests -platform minimal)

        add_executable(pd_gl_tests tests/gl_smoke_test.cpp)
        target_link_libraries(pd_gl_tests PRIVATE pd_ui GTest::gtest)
        pd_embed_catalog(pd_gl_tests)
        add_dependencies(pd_gl_tests frame_generator)
        target_compile_definitions(pd_gl_tests PRIVATE PD_FRAME_PATH="$<TARGET_FILE:frame_generator>")
        if(WIN32)
            # Both executables share bin/. Deploy qwindows once, not from concurrent link steps.
            add_dependencies(pd_gl_tests PlaneDraft)
        endif()
        if(PLANEDRAFT_TEST_GL)
            add_test(NAME viewport_fbo COMMAND pd_gl_tests)
        endif()
    endif()
endif()

# Do not package the build tree: it also contains test modules and developer tools.
include(packaging/install.cmake)
