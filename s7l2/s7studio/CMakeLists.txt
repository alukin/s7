find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
# Observation viewers: Qt Graphs (2D) and Qt Quick 3D in QQuickWidget. 6.9 is
# the first release with GraphsView.plotArea, which cursor-anchored zooming
# needs (src/viewers/qml/LinePlot.qml).
find_package(Qt6 6.9 REQUIRED COMPONENTS Qml Quick QuickWidgets Graphs Quick3D)

# Diagram model, scene, router and file format: shared by Studio and its tests.
add_library(s7studio_diagram STATIC
    src/diagram/items.cpp
    src/diagram/items.hpp
    src/diagram/element_ports.cpp
    src/diagram/element_ports.hpp
    src/diagram/net_model.cpp
    src/diagram/net_model.hpp
    src/diagram/wire_router.cpp
    src/diagram/wire_router.hpp
    src/diagram/diagram_scene.cpp
    src/diagram/diagram_scene.hpp
    src/diagram_document.cpp
    src/diagram_document.hpp
    src/diagram_format.hpp
    src/scene_serializer.cpp
    src/scene_serializer.hpp
    src/wire_type_labels.cpp
    src/wire_type_labels.hpp
    src/element_requirements.cpp
    src/element_requirements.hpp
    ${PROJECT_SOURCE_DIR}/s7runtime/src/plugin_loader.cpp
)
target_include_directories(s7studio_diagram PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/s7core/include
    ${PROJECT_SOURCE_DIR}/s7net/include
    ${PROJECT_SOURCE_DIR}/s7observe/include
    ${PROJECT_SOURCE_DIR}/s7runtime/include
    ${PROJECT_SOURCE_DIR}/s7transport/include
)
target_link_libraries(s7studio_diagram PUBLIC
    s7io
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qschematic-static
    s7core
    s7net
    s7observe
    Boost::json
    ${CMAKE_DL_LIBS}
)
set_target_properties(s7studio_diagram PROPERTIES AUTOMOC ON)
s7_enable_project_warnings(s7studio_diagram)

# Observation viewers (docs/STUDIO_OBSERVATION_VIEWERS.md): the viewer
# registry, manager and shared plot/3D components, and the Scope, Phase
# Portrait and 3D Phase Trajectory windows. Shared by Studio and its tests.
add_library(s7studio_observation STATIC
    src/observation/observation_source.cpp
    src/observation/observation_source.hpp
    src/observation/run_metadata.cpp src/observation/run_metadata.hpp
)
target_include_directories(s7studio_observation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(s7studio_observation PUBLIC Qt6::Core)
s7_enable_project_warnings(s7studio_observation)

add_custom_target(s7studio_build_identity
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generated/s7studio_build_identity.hpp"
        "-DVERSION=${CMAKE_PROJECT_VERSION}"
        "-DCOMPILER=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_identity.cmake"
    VERBATIM)
add_library(s7studio_run_provenance STATIC src/run_provenance.cpp src/run_provenance.hpp)
add_dependencies(s7studio_run_provenance s7studio_build_identity)
target_include_directories(s7studio_run_provenance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(s7studio_run_provenance PUBLIC s7studio_observation s7core s7net Qt6::Core Boost::json)
s7_enable_project_warnings(s7studio_run_provenance)
target_link_libraries(s7studio_diagram PUBLIC s7studio_run_provenance)

add_library(s7studio_results STATIC
    src/scope_recording.cpp src/scope_recording.hpp
    src/observation/scope_results.cpp src/observation/scope_results.hpp
)
target_include_directories(s7studio_results PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(s7studio_results PUBLIC s7studio_observation Qt6::Core)
s7_enable_project_warnings(s7studio_results)

add_library(s7studio_viewers STATIC
    src/viewers/observation_viewer.hpp
    src/viewers/observation_viewer.cpp
    src/viewers/viewer_registry.cpp
    src/viewers/viewer_registry.hpp
    src/viewers/viewer_manager.cpp
    src/viewers/viewer_manager.hpp
    src/observation/step_aligner.hpp
    src/viewers/line_plot_view.cpp
    src/viewers/line_plot_view.hpp
    src/viewers/quick_support.cpp
    src/viewers/quick_support.hpp
    src/viewers/trajectory_geometry.cpp
    src/viewers/trajectory_geometry.hpp
    src/scope_sample_mailbox.hpp
    src/scope_view_window.cpp
    src/scope_view_window.hpp
    src/phase_portrait_view_window.cpp
    src/phase_portrait_view_window.hpp
    src/phase_trajectory_3d_view_window.cpp
    src/phase_trajectory_3d_view_window.hpp
)
qt_add_resources(s7studio_viewers s7studio_viewers_qml
    PREFIX /s7studio/viewers
    BASE src/viewers/qml
    FILES
        src/viewers/qml/LinePlot.qml
        src/viewers/qml/Trajectory3D.qml
)
target_include_directories(s7studio_viewers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(s7studio_viewers PUBLIC
    s7studio_observation s7studio_results
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Graphs
    Qt6::Quick3D
)
set_target_properties(s7studio_viewers PROPERTIES AUTOMOC ON)
s7_enable_project_warnings(s7studio_viewers)

add_library(s7studio_app OBJECT
    src/schema_tabs.cpp src/schema_tabs.hpp
    src/main_window.cpp
    src/main_window.hpp
    src/main_window_builder.cpp
    src/main_window_builder.hpp
    src/model_run_controller.cpp
    src/is_schema_runner.hpp
    src/model_run_controller.hpp
    src/json_utils.hpp
    src/local_runtime_v2.hpp
    src/local_schema_runner.cpp
    src/local_schema_runner.hpp
    src/element_picker_panel.cpp
    src/element_picker_panel.hpp
    src/headless_nodes_panel.cpp
    src/headless_nodes_panel.hpp
    resources/icons.qrc
    resources/demo_startup.s7diagram
    src/deployment_support.cpp
    src/deployment_support.hpp
    src/diagram_editor.hpp
    src/diagram_editor.cpp
    src/diagram_ruler.hpp
    src/diagram_ruler.cpp
    src/tool_windows.hpp
    src/tool_windows.cpp
    src/tool_panels.hpp
    src/tool_panels.cpp
    src/ui_icons.hpp
    src/ui_icons.cpp
)

target_include_directories(s7studio_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/s7core/include
    ${PROJECT_SOURCE_DIR}/s7net/include
    ${PROJECT_SOURCE_DIR}/s7observe/include
    ${PROJECT_SOURCE_DIR}/s7runtime/include
    ${PROJECT_SOURCE_DIR}/s7transport/include
)

target_link_libraries(s7studio_app PUBLIC
    s7studio_diagram
    s7studio_viewers
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qschematic-static
    s7core
    s7net
    s7observe
    Boost::json
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

target_compile_definitions(s7studio_app PUBLIC
    S7STUDIO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(s7studio_app PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if (TARGET s7noise)
    add_dependencies(s7studio_app s7noise)
endif()

s7_enable_project_warnings(s7studio_app)

add_executable(s7studio src/main.cpp)
target_link_libraries(s7studio PRIVATE s7studio_app)

add_test(NAME s7studio_smoke COMMAND s7studio --smoke)
add_test(NAME s7studio_wire_types
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/wire_types_test.sh $<TARGET_FILE:s7studio> ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_test(NAME s7studio_deployment_diagram
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/deployment_diagram_test.sh $<TARGET_FILE:s7studio> ${CMAKE_CURRENT_SOURCE_DIR}/tests)
set_tests_properties(s7studio_wire_types s7studio_deployment_diagram PROPERTIES ENVIRONMENT "${S7_SCRIPT_TEST_ENV}")

add_test(NAME s7studio_edit_check
    COMMAND s7studio --edit-check ${CMAKE_CURRENT_SOURCE_DIR}/tests/wire_types_ok.json)
set_tests_properties(s7studio_edit_check PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Trust shown by the Headless Nodes panel, against fake nodes (honest,
# impostor, plain) in the test process.
s7_add_test_executable(s7studio_nodes_panel_test
    tests/nodes_panel_test.cpp
    src/headless_nodes_panel.cpp
    src/headless_nodes_panel.hpp
)
target_include_directories(s7studio_nodes_panel_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/s7transport/include)
target_link_libraries(s7studio_nodes_panel_test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets s7transport Boost::json Threads::Threads)
target_compile_definitions(s7studio_nodes_panel_test PRIVATE S7_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/s7runtime/examples/certs")
set_target_properties(s7studio_nodes_panel_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_nodes_panel_trust COMMAND s7studio_nodes_panel_test)

foreach(layout_case wire_types_ok layout_loops)
    add_test(NAME s7studio_layout_${layout_case}
        COMMAND s7studio --layout-check ${CMAKE_CURRENT_SOURCE_DIR}/tests/${layout_case}.json)
    set_tests_properties(s7studio_layout_${layout_case} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# Diagram editing: connection rules, wire trees, routing, undo, file round trips.
s7_add_test_executable(s7studio_diagram_test tests/diagram_test.cpp)
target_link_libraries(s7studio_diagram_test PRIVATE s7studio_diagram ${CMAKE_DL_LIBS} Threads::Threads)
set_target_properties(s7studio_diagram_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_diagram COMMAND s7studio_diagram_test)
set_tests_properties(s7studio_diagram PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if (TARGET s7noise)
    # Finding a required plugin on load (element_requirements).
    add_dependencies(s7studio_diagram_test s7noise)
    target_compile_definitions(s7studio_diagram_test PRIVATE S7_TEST_NOISE_PLUGIN_DIR="$<TARGET_FILE_DIR:s7noise>")
endif()

s7_add_test_executable(s7studio_scope_view_test tests/scope_view_test.cpp)
target_link_libraries(s7studio_scope_view_test PRIVATE s7studio_viewers)
set_target_properties(s7studio_scope_view_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_scope_view COMMAND s7studio_scope_view_test)
set_tests_properties(s7studio_scope_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME s7studio_scope_run COMMAND s7studio --scope-run-check)
set_tests_properties(s7studio_scope_run PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

s7_add_test_executable(s7studio_coupled_pendulum_test
    tests/coupled_pendulum_test.cpp
    src/local_schema_runner.cpp
    src/local_schema_runner.hpp
)
target_include_directories(s7studio_coupled_pendulum_test PRIVATE ${PROJECT_SOURCE_DIR}/s7tests)
target_link_libraries(s7studio_coupled_pendulum_test PRIVATE s7studio_diagram s7studio_observation Threads::Threads)
target_compile_definitions(s7studio_coupled_pendulum_test PRIVATE
    S7_PENDULUM_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples/double_pendulum")
set_target_properties(s7studio_coupled_pendulum_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_coupled_pendulum COMMAND s7studio_coupled_pendulum_test)
set_tests_properties(s7studio_coupled_pendulum PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)

s7_add_test_executable(s7studio_nonlinear_pendulum_test
    tests/nonlinear_pendulum_test.cpp
    src/local_schema_runner.cpp src/local_schema_runner.hpp
)
target_include_directories(s7studio_nonlinear_pendulum_test PRIVATE ${PROJECT_SOURCE_DIR}/s7tests)
target_link_libraries(s7studio_nonlinear_pendulum_test PRIVATE s7studio_diagram s7studio_viewers Threads::Threads)
target_compile_definitions(s7studio_nonlinear_pendulum_test PRIVATE
    S7_SINGLE_PENDULUM_DIR="${PROJECT_SOURCE_DIR}/examples/pendulum"
    S7_SINGLE_PENDULUM_STUDIO_REPORT_DIR="${CMAKE_CURRENT_BINARY_DIR}/nonlinear_pendulum")
set_target_properties(s7studio_nonlinear_pendulum_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_nonlinear_pendulum COMMAND s7studio_nonlinear_pendulum_test)
set_tests_properties(s7studio_nonlinear_pendulum PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)

s7_add_test_executable(s7studio_scope_lifecycle_test
    tests/scope_lifecycle_test.cpp
    src/local_schema_runner.cpp src/local_schema_runner.hpp
)
target_include_directories(s7studio_scope_lifecycle_test PRIVATE ${PROJECT_SOURCE_DIR}/s7tests)
target_link_libraries(s7studio_scope_lifecycle_test PRIVATE s7studio_diagram s7studio_viewers Threads::Threads)
target_compile_definitions(s7studio_scope_lifecycle_test PRIVATE
    S7_SINGLE_PENDULUM_DIR="${PROJECT_SOURCE_DIR}/examples/pendulum")
set_target_properties(s7studio_scope_lifecycle_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_scope_lifecycle COMMAND s7studio_scope_lifecycle_test)
set_tests_properties(s7studio_scope_lifecycle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

add_test(NAME s7studio_recent_diagrams COMMAND s7studio --recent-diagrams-check)
set_tests_properties(s7studio_recent_diagrams PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

add_test(NAME s7studio_phase_portrait COMMAND s7studio --phase-portrait-check)
set_tests_properties(s7studio_phase_portrait PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

add_test(NAME s7studio_phase_trajectory_3d COMMAND s7studio --phase-trajectory-3d-check)
set_tests_properties(s7studio_phase_trajectory_3d PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

# Viewer registry, manager routing and lifecycle, step alignment, Qt Graphs
# plot state and the Qt Quick 3D trajectory geometry.
s7_add_test_executable(s7studio_viewers_test tests/viewers_test.cpp)
target_link_libraries(s7studio_viewers_test PRIVATE s7studio_viewers)
set_target_properties(s7studio_viewers_test PROPERTIES AUTOMOC ON)
s7_enable_project_warnings(s7studio_viewers_test)
add_test(NAME s7studio_viewers COMMAND s7studio_viewers_test)
set_tests_properties(s7studio_viewers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

s7_add_test_executable(s7studio_schema_elements_test tests/schema_elements_test.cpp)
target_link_libraries(s7studio_schema_elements_test PRIVATE s7studio_app)
add_test(NAME s7studio_schema_elements COMMAND s7studio_schema_elements_test)
set_tests_properties(s7studio_schema_elements PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

s7_add_test_executable(s7studio_atomic_file_test tests/atomic_file_test.cpp)
target_include_directories(s7studio_atomic_file_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(s7studio_atomic_file_test PRIVATE Qt6::Core)
add_test(NAME s7studio_atomic_file COMMAND s7studio_atomic_file_test)

s7_add_test_executable(s7studio_document_safety_test tests/document_safety_test.cpp)
target_link_libraries(s7studio_document_safety_test PRIVATE s7studio_app)
add_test(NAME s7studio_document_safety COMMAND s7studio_document_safety_test)
set_tests_properties(s7studio_document_safety PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

# Real GUI interaction/rendering gates are native-Linux tests. Cross/Wine and
# existing offscreen tests keep their own separately documented coverage.
set(s7_gui_tests_default OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    set(s7_gui_tests_default ON)
endif()
option(S7_BUILD_GUI_REGRESSION_TESTS "Build native Linux Qt Test/Quick Test GUI suite" ${s7_gui_tests_default})
if(S7_BUILD_GUI_REGRESSION_TESTS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "S7 GUI regression wrapper currently supports native Linux only")
    endif()
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test QuickTest)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    s7_add_test_executable(s7studio_gui_capture tests/gui/capture.cpp)
    target_link_libraries(s7studio_gui_capture PRIVATE Qt6::Gui)
    s7_add_test_executable(s7studio_gui_workflows_test tests/gui/workflows_test.cpp)
    target_link_libraries(s7studio_gui_workflows_test PRIVATE s7studio_app Qt6::Test)
    target_compile_definitions(s7studio_gui_workflows_test PRIVATE S7_GUI_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
    set_target_properties(s7studio_gui_workflows_test PROPERTIES AUTOMOC ON)
    s7_add_test_executable(s7studio_gui_quick_test tests/gui/quick_test.cpp)
    target_link_libraries(s7studio_gui_quick_test PRIVATE s7studio_viewers Qt6::Test Qt6::QuickTest)
    target_compile_definitions(s7studio_gui_quick_test PRIVATE S7_GUI_QML_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/gui/qml")
    set_target_properties(s7studio_gui_quick_test PROPERTIES AUTOMOC ON)
    foreach(suite workflows lineplot trajectory)
        set(gui_args)
        if(suite STREQUAL "workflows")
            set(gui_target s7studio_gui_workflows_test)
        else()
            set(gui_target s7studio_gui_quick_test)
            list(APPEND gui_args -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/gui/qml/tst_${suite}.qml")
        endif()
        set(graphics_args)
        if(suite STREQUAL "trajectory")
            list(APPEND graphics_args --graphics)
        endif()
        add_test(NAME s7studio_gui_${suite}
            COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/run_gui_test.py"
                --artifacts "${CMAKE_CURRENT_BINARY_DIR}/gui-artifacts/${suite}"
                --capture $<TARGET_FILE:s7studio_gui_capture> ${graphics_args}
                -- $<TARGET_FILE:${gui_target}> ${gui_args})
        set_tests_properties(s7studio_gui_${suite} PROPERTIES
            LABELS "gui;qt-regression" SKIP_RETURN_CODE 77 TIMEOUT 180)
    endforeach()
    set_property(TEST s7studio_gui_trajectory APPEND PROPERTY LABELS "rendering;quick3d")
endif()
if(S7_BUILD_GUI_REGRESSION_TESTS)
    add_test(NAME s7studio_gui_failure_artifacts
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_gui_artifacts.py"
            "${PROJECT_SOURCE_DIR}/tools/run_gui_test.py"
            $<TARGET_FILE:s7studio_gui_capture> $<TARGET_FILE:s7studio_gui_workflows_test>
            "${CMAKE_CURRENT_BINARY_DIR}/gui-artifacts/injected-failure")
    set_tests_properties(s7studio_gui_failure_artifacts PROPERTIES
        LABELS "gui;qt-regression" SKIP_RETURN_CODE 77 TIMEOUT 180)
endif()

find_package(Qt6 REQUIRED COMPONENTS Test)
s7_add_test_executable(s7studio_run_lifecycle_test
    tests/run_lifecycle_test.cpp src/local_schema_runner.cpp src/local_schema_runner.hpp src/model_run_controller.cpp)
target_link_libraries(s7studio_run_lifecycle_test PRIVATE s7studio_diagram s7studio_observation Qt6::Test Threads::Threads)
set_target_properties(s7studio_run_lifecycle_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_run_lifecycle COMMAND s7studio_run_lifecycle_test)
set_tests_properties(s7studio_run_lifecycle PROPERTIES TIMEOUT 60)

s7_add_test_executable(s7studio_window_lifecycle_test tests/window_lifecycle_test.cpp)
target_link_libraries(s7studio_window_lifecycle_test PRIVATE s7studio_app Qt6::Test)
set_target_properties(s7studio_window_lifecycle_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_window_lifecycle COMMAND s7studio_window_lifecycle_test)
set_tests_properties(s7studio_window_lifecycle PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/lifecycle-cache;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/lifecycle-config" TIMEOUT 60)

s7_add_test_executable(s7studio_observation_source_test tests/observation_source_test.cpp)
target_link_libraries(s7studio_observation_source_test PRIVATE s7studio_observation Qt6::Test)
set_target_properties(s7studio_observation_source_test PROPERTIES AUTOMOC ON)
s7_enable_project_warnings(s7studio_observation_source_test)
add_test(NAME s7studio_observation_source COMMAND s7studio_observation_source_test)
set_tests_properties(s7studio_observation_source PROPERTIES TIMEOUT 60)

s7_add_test_executable(s7studio_observation_capture_test
    tests/observation_capture_test.cpp src/local_schema_runner.cpp src/local_schema_runner.hpp)
target_link_libraries(s7studio_observation_capture_test PRIVATE s7studio_diagram s7studio_results Qt6::Test Threads::Threads)
set_target_properties(s7studio_observation_capture_test PROPERTIES AUTOMOC ON)
s7_enable_project_warnings(s7studio_observation_capture_test)
add_test(NAME s7studio_observation_capture COMMAND s7studio_observation_capture_test)
set_tests_properties(s7studio_observation_capture PROPERTIES TIMEOUT 60)

s7_add_test_executable(s7studio_model_time_test
    tests/model_time_test.cpp src/local_schema_runner.cpp src/local_schema_runner.hpp src/model_run_controller.cpp)
target_include_directories(s7studio_model_time_test PRIVATE ${PROJECT_SOURCE_DIR}/s7tests)
target_compile_definitions(s7studio_model_time_test PRIVATE S7_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(s7studio_model_time_test PRIVATE s7studio_diagram s7studio_observation Qt6::Test Threads::Threads)
set_target_properties(s7studio_model_time_test PROPERTIES AUTOMOC ON)
s7_enable_project_warnings(s7studio_model_time_test)
add_test(NAME s7studio_model_time COMMAND s7studio_model_time_test)
set_tests_properties(s7studio_model_time PROPERTIES TIMEOUT 60)

s7_add_test_executable(s7studio_run_provenance_test
    tests/run_provenance_test.cpp src/local_schema_runner.cpp src/local_schema_runner.hpp)
target_include_directories(s7studio_run_provenance_test PRIVATE ${PROJECT_SOURCE_DIR}/s7tests)
target_compile_definitions(s7studio_run_provenance_test PRIVATE S7_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(s7studio_run_provenance_test PRIVATE s7studio_diagram s7studio_results s7noise Qt6::Test Threads::Threads)
set_target_properties(s7studio_run_provenance_test PROPERTIES AUTOMOC ON)
s7_enable_project_warnings(s7studio_run_provenance_test)
add_test(NAME s7studio_run_provenance COMMAND s7studio_run_provenance_test)
set_tests_properties(s7studio_run_provenance PROPERTIES TIMEOUT 60)
