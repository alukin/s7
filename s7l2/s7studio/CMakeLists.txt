
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt6 REQUIRED COMPONENTS PrintSupport)

file(GLOB_RECURSE S7_QCUSTOMPLOT_SOURCES
    "${PROJECT_SOURCE_DIR}/external/QCustomPlot/src/*.cpp")
file(GLOB_RECURSE S7_QCUSTOMPLOT_HEADERS
    "${PROJECT_SOURCE_DIR}/external/QCustomPlot/src/*.h")

add_library(s7_qcustomplot STATIC ${S7_QCUSTOMPLOT_SOURCES} ${S7_QCUSTOMPLOT_HEADERS})
target_include_directories(s7_qcustomplot PUBLIC
    ${PROJECT_SOURCE_DIR}/external/QCustomPlot/src)
target_link_libraries(s7_qcustomplot PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)
target_link_libraries(s7_qcustomplot PUBLIC Qt6::PrintSupport)
set_target_properties(s7_qcustomplot PROPERTIES AUTOMOC ON)

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

add_executable(s7studio
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
    src/main_window_builder.cpp
    src/main_window_builder.hpp
    src/scope_view_window.cpp
    src/scope_view_window.hpp
    src/phase_portrait_view_window.cpp
    src/phase_portrait_view_window.hpp
    src/model_run_controller.cpp
    src/is_schema_runner.hpp
    src/model_run_controller.hpp
    src/json_utils.hpp
    src/local_runtime_v2.hpp
    src/scope_sample_mailbox.hpp
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

target_include_directories(s7studio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/s7core/include
    ${PROJECT_SOURCE_DIR}/s7net/include
    ${PROJECT_SOURCE_DIR}/s7observe/include
    ${PROJECT_SOURCE_DIR}/s7runtime/include
    ${PROJECT_SOURCE_DIR}/s7transport/include
)

target_link_libraries(s7studio PRIVATE
    s7studio_diagram
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qschematic-static
    s7_qcustomplot
    s7core
    s7net
    s7observe
    Boost::json
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

target_compile_definitions(s7studio PRIVATE
    S7STUDIO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(s7studio PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if (TARGET s7noise)
    add_dependencies(s7studio s7noise)
endif()

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
add_executable(s7studio_nodes_panel_test
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
add_executable(s7studio_diagram_test tests/diagram_test.cpp)
target_link_libraries(s7studio_diagram_test PRIVATE s7studio_diagram ${CMAKE_DL_LIBS} Threads::Threads)
set_target_properties(s7studio_diagram_test PROPERTIES AUTOMOC ON)
add_test(NAME s7studio_diagram COMMAND s7studio_diagram_test)
set_tests_properties(s7studio_diagram PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if (TARGET s7noise)
    # Finding a required plugin on load (element_requirements).
    add_dependencies(s7studio_diagram_test s7noise)
    target_compile_definitions(s7studio_diagram_test PRIVATE S7_TEST_NOISE_PLUGIN_DIR="$<TARGET_FILE_DIR:s7noise>")
endif()
