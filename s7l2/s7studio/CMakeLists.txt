
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

add_executable(s7studio
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
    src/main_window_builder.cpp
    src/main_window_builder.hpp
    src/scope_view_window.cpp
    src/scope_view_window.hpp
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
    src/diagram_document.hpp
    src/diagram_document.cpp
    src/diagram_node_roles.hpp
    src/scene_serializer.hpp
    src/scene_serializer.cpp
    src/runtime_schema_extractor.hpp
    src/runtime_schema_extractor.cpp
    src/wire_type_labels.cpp
    src/deployment_support.cpp
    src/deployment_support.hpp
    src/wire_type_labels.hpp
    ${PROJECT_SOURCE_DIR}/s7runtime/src/plugin_loader.cpp
    src/diagram_format.hpp
    src/diagram_editor.hpp
    src/diagram_editor.cpp
    src/diagram_ruler.hpp
    src/diagram_ruler.cpp
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
