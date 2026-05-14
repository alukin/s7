# s7/s7ui/CMakeLists.txt

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)

# Build vendored QSchematic as a static library for the UI shell.
set(QSCHEMATIC_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(QSCHEMATIC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(QSCHEMATIC_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(QSCHEMATIC_DEPENDENCY_GPDS_DOWNLOAD ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/QSchematic ${CMAKE_BINARY_DIR}/external/QSchematic)

if (TARGET qschematic-static)
    set_target_properties(qschematic-static PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
    )
endif()

if (TARGET gpds-static)
    set_target_properties(gpds-static PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
    )
endif()

# QCustomPlot ships as split sources in this repository.
file(GLOB_RECURSE S7UI_QCUSTOMPLOT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/external/QCustomPlot/src/*.cpp
)

add_library(s7ui_qcustomplot STATIC
    ${S7UI_QCUSTOMPLOT_SOURCES}
    ${CMAKE_SOURCE_DIR}/external/QCustomPlot/src/qcp.h
    ${CMAKE_SOURCE_DIR}/external/QCustomPlot/src/global.h
)
target_include_directories(s7ui_qcustomplot PUBLIC
    ${CMAKE_SOURCE_DIR}/external/QCustomPlot/src
)
target_link_libraries(s7ui_qcustomplot PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
)
set_target_properties(s7ui_qcustomplot PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

add_executable(s7ui
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
    src/i_schema_runner.hpp
    src/scene_roles.hpp
    src/render/print_render_service.cpp
    src/render/print_render_service.hpp
    src/node_template.hpp
    src/node_factory.cpp
    src/node_factory.hpp
    src/scene_serializer.cpp
    src/scene_serializer.hpp
    src/model_run_controller.cpp
    src/model_run_controller.hpp
    src/sheet_presenter.cpp
    src/sheet_presenter.hpp
    src/palette_service.cpp
    src/palette_service.hpp
    src/schema_document_controller.cpp
    src/schema_document_controller.hpp
    src/schema_file_service.cpp
    src/schema_file_service.hpp
    src/schema_workflow_controller.cpp
    src/schema_workflow_controller.hpp
    src/visual_catalog.cpp
    src/visual_catalog.hpp
    src/schema_compiler.cpp
    src/schema_compiler.hpp
    src/schema_session.cpp
    src/schema_session.hpp
    src/s7ui_json_dto.cpp
    src/s7ui_json_dto.hpp
    src/oscilloscope_widget.cpp
    src/oscilloscope_widget.hpp
    src/elements/basic_node_item.cpp
    src/elements/basic_node_item.hpp
    src/elements/s7_wire_item.cpp
    src/elements/s7_wire_item.hpp
    src/elements/visual_sink_preview_widget.cpp
    src/elements/visual_sink_preview_widget.hpp
    src/elements/visual_sink_detail_window.cpp
    src/elements/visual_sink_detail_window.hpp
    src/elements/sink_node_item.cpp
    src/elements/sink_node_item.hpp
    src/remote_nodes_widget.cpp
    src/remote_nodes_widget.hpp
    src/dialogs/properties_dialog.cpp
    src/schema_runner.cpp
)

target_include_directories(s7ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/s7core/include
)

target_link_libraries(s7ui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    s7core
    s7io
    qschematic::qschematic-static
    s7ui_qcustomplot
)

target_compile_definitions(s7ui PRIVATE
    S7UI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

set_target_properties(s7ui PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS s7ui DESTINATION bin)

if(BUILD_TESTING)
    # --- ModelRunController (no Qt required) ---
    add_executable(s7ui_model_run_controller_test
        src/tests/model_run_controller_test.cpp
    )
    target_include_directories(s7ui_model_run_controller_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/s7core/include
    )
    target_link_libraries(s7ui_model_run_controller_test PRIVATE
        Qt6::Core
    )
    set_target_properties(s7ui_model_run_controller_test PROPERTIES
        AUTOMOC ON
        CXX_STANDARD 23
    )
    add_test(NAME s7ui_model_run_controller_test COMMAND s7ui_model_run_controller_test)

    # --- SheetPresenter static helpers (no Qt runtime display required) ---
    add_executable(s7ui_sheet_presenter_test
        src/tests/sheet_presenter_test.cpp
        src/sheet_presenter.cpp
        src/sheet_presenter.hpp
        src/render/print_render_service.cpp
        src/render/print_render_service.hpp
        src/scene_roles.hpp
    )
    target_include_directories(s7ui_sheet_presenter_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/s7core/include
    )
    target_link_libraries(s7ui_sheet_presenter_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport
        qschematic::qschematic-static
        s7ui_qcustomplot
    )
    set_target_properties(s7ui_sheet_presenter_test PROPERTIES
        AUTOMOC ON
        CXX_STANDARD 23
    )
    add_test(NAME s7ui_sheet_presenter_test COMMAND s7ui_sheet_presenter_test)

    # --- Qt print/export helper helpers ---
    add_executable(s7ui_print_render_service_test
        src/tests/print_render_service_test.cpp
        src/render/print_render_service.cpp
        src/render/print_render_service.hpp
    )
    target_include_directories(s7ui_print_render_service_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/s7core/include
    )
    target_link_libraries(s7ui_print_render_service_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport
        s7ui_qcustomplot
    )
    set_target_properties(s7ui_print_render_service_test PROPERTIES
        AUTOMOC ON
        CXX_STANDARD 23
    )
    add_test(NAME s7ui_print_render_service_test COMMAND s7ui_print_render_service_test)
endif()

