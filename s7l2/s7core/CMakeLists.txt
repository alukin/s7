set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(s7core STATIC
    src/schema.cpp
    src/validation.cpp
    src/compiler.cpp
    src/runtime.cpp
    src/elements.cpp
)

target_include_directories(s7core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

