set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(s7core STATIC
    src/schema.cpp
    src/validation.cpp
    src/compiler.cpp
    src/runtime.cpp
    src/elements.cpp
    src/element_registry.cpp
    src/element_catalog.cpp
    src/builtin_element_registry.cpp
)

set_target_properties(s7core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(s7core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

