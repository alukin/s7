set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(s7studio
    src/main.cpp
    src/main_window.cpp
    src/main_window.hpp
    src/model_run_controller.cpp
    src/is_schema_runner.hpp
    src/model_run_controller.hpp
    src/local_runtime.hpp
    src/local_schema_runner.cpp
    src/local_schema_runner.hpp
    src/demo_schema.hpp
    src/diagram_document.hpp
    src/diagram_document.cpp
    src/scene_serializer.hpp
    src/scene_serializer.cpp
    src/diagram_format.hpp
    src/diagram_editor.hpp
    src/diagram_editor.cpp
)

target_include_directories(s7studio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/s7core/include
    ${PROJECT_SOURCE_DIR}/s7observe/include
)

target_link_libraries(s7studio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qschematic-static
    s7core
    s7observe
    Boost::json
    Threads::Threads
)

target_compile_definitions(s7studio PRIVATE
    S7STUDIO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(s7studio PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

add_test(NAME s7studio_smoke COMMAND s7studio --smoke)
