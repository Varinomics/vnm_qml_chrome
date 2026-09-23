cmake_minimum_required(VERSION 3.21)

project(vnm_qml_chrome LANGUAGES CXX VERSION 1.10.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(vnm_qml_chrome_minimum_qt_version 6.11.1)

find_package(Qt6 ${vnm_qml_chrome_minimum_qt_version} REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickLayouts
    QuickShapes
    QuickControls2
    ShaderTools)

if(PROJECT_IS_TOP_LEVEL)
    set(VNM_QML_CHROME_BUILD_TESTS_DEFAULT ON)
    set(VNM_QML_CHROME_BUILD_EXAMPLES_DEFAULT ON)
else()
    set(VNM_QML_CHROME_BUILD_TESTS_DEFAULT OFF)
    set(VNM_QML_CHROME_BUILD_EXAMPLES_DEFAULT OFF)
endif()

option(
    VNM_QML_CHROME_BUILD_TESTS
    "Build vnm_qml_chrome standalone contract tests"
    ${VNM_QML_CHROME_BUILD_TESTS_DEFAULT})
option(
    VNM_QML_CHROME_BUILD_EXAMPLES
    "Build vnm_qml_chrome standalone examples"
    ${VNM_QML_CHROME_BUILD_EXAMPLES_DEFAULT})

add_library(vnm_qml_chrome STATIC)
add_library(vnm_qml_chrome::vnm_qml_chrome ALIAS vnm_qml_chrome)

target_sources(vnm_qml_chrome PRIVATE
    include/vnm_qml_chrome/vnm_chrome_geometry.h
    include/vnm_qml_chrome/vnm_qml_chrome_runtime.h
    include/vnm_qml_chrome/vnm_native_window_frame.h
    include/vnm_qml_chrome/vnm_system_window.h
    src/vnm_device_pixel_geometry_item.h
    src/vnm_chrome_geometry.cpp
    src/vnm_device_pixel_geometry_item.cpp
    src/vnm_native_window_frame.cpp
    src/vnm_qml_chrome_runtime.cpp
    src/vnm_system_window.cpp
    vnm_qml_chrome.qrc
)

# Consumers need a feature minimum that remains available after this target is
# exported or provided by an embedding parent project.
set_target_properties(vnm_qml_chrome PROPERTIES
    VNM_QML_CHROME_VERSION "${PROJECT_VERSION}"
    EXPORT_PROPERTIES VNM_QML_CHROME_VERSION)

# Keep shader resources inside the archive that the runtime initializes. Qt's
# static-target helper otherwise exports a separate resource initializer target.
add_library(vnm_qml_chrome_shader_resources OBJECT)
target_link_libraries(vnm_qml_chrome_shader_resources PRIVATE Qt6::Core)
qt_add_shaders(vnm_qml_chrome_shader_resources vnm_qml_chrome_icon_shaders
    PREFIX "/vnm_qml_chrome/shaders"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    FILES shaders/monochrome_icon.frag
)
target_sources(vnm_qml_chrome PRIVATE
    $<TARGET_OBJECTS:vnm_qml_chrome_shader_resources>)

target_include_directories(vnm_qml_chrome
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(vnm_qml_chrome
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickLayouts
        Qt6::QuickShapes
        Qt6::QuickControls2
)

if(WIN32)
    target_compile_definitions(vnm_qml_chrome PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(vnm_qml_chrome PRIVATE dwmapi gdi32 user32)
endif()

if(VNM_QML_CHROME_BUILD_TESTS)
    include(CTest)
    find_package(Qt6 ${vnm_qml_chrome_minimum_qt_version} REQUIRED COMPONENTS Test)
    find_package(
        Qt6Gui
        ${vnm_qml_chrome_minimum_qt_version}
        REQUIRED
        COMPONENTS QOffscreenIntegrationPlugin)

    if(WIN32)
        set(vnm_qml_chrome_qt_runtime_path
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>$ENV{PATH}")
    elseif(APPLE)
        set(vnm_qml_chrome_qt_runtime_path
            "DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:Qt6::Core>:$ENV{DYLD_LIBRARY_PATH}")
    else()
        set(vnm_qml_chrome_qt_runtime_path
            "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:Qt6::Core>:$ENV{LD_LIBRARY_PATH}")
    endif()

    set(vnm_qml_chrome_qt_test_environment
        "QT_QPA_PLATFORM=offscreen"
        "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>"
        "QT_QUICK_CONTROLS_STYLE=Basic"
    )

    add_executable(vnm_chrome_contract_tests
        examples/solid_window/solid_window.qrc
        tests/vnm_chrome_contract_tests.cpp
    )

    target_link_libraries(vnm_chrome_contract_tests PRIVATE
        vnm_qml_chrome::vnm_qml_chrome
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickLayouts
        Qt6::QuickShapes
        Qt6::QuickControls2
        Qt6::Test
    )

    add_executable(vnm_chrome_device_pixel_geometry_tests
        tests/vnm_device_pixel_geometry_tests.cpp
    )
    target_include_directories(vnm_chrome_device_pixel_geometry_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(vnm_chrome_device_pixel_geometry_tests PRIVATE
        vnm_qml_chrome::vnm_qml_chrome
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickShapes
        Qt6::QuickControls2
        Qt6::Test
    )

    if(BUILD_TESTING)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)

        add_test(
            NAME vnm_chrome_contract_tests
            COMMAND ${CMAKE_COMMAND} -E env
                ${vnm_qml_chrome_qt_test_environment}
                "${vnm_qml_chrome_qt_runtime_path}"
                "$<TARGET_FILE:vnm_chrome_contract_tests>"
        )

        add_test(
            NAME vnm_chrome_device_pixel_geometry_tests
            COMMAND ${CMAKE_COMMAND} -E env
                ${vnm_qml_chrome_qt_test_environment}
                "QT_SCALE_FACTOR=1.25"
                "QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough"
                "${vnm_qml_chrome_qt_runtime_path}"
                "$<TARGET_FILE:vnm_chrome_device_pixel_geometry_tests>"
        )

        add_test(
            NAME vnm_chrome_snapped_rectangle_resource_recovery_tests
            COMMAND ${CMAKE_COMMAND} -E env
                "QT_QUICK_CONTROLS_STYLE=Basic"
                "${vnm_qml_chrome_qt_runtime_path}"
                "$<TARGET_FILE:vnm_chrome_device_pixel_geometry_tests>"
                snapped_rectangle_survives_scene_graph_recreation_without_hover
        )
        set_property(TEST vnm_chrome_snapped_rectangle_resource_recovery_tests
            APPEND PROPERTY LABELS real-platform)

        add_test(
            NAME vnm_mark_eye_provenance
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_font_awesome_eye_provenance.py"
        )
    endif()
endif()

if(VNM_QML_CHROME_BUILD_EXAMPLES)
    add_executable(vnm_qml_chrome_solid_window_example
        examples/solid_window/main.cpp
        examples/solid_window/solid_window.qrc
    )

    target_link_libraries(vnm_qml_chrome_solid_window_example PRIVATE
        vnm_qml_chrome::vnm_qml_chrome
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
    )
endif()
