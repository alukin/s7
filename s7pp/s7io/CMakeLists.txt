# s7io/CMakeLists.txt
# I/O element library for S7.
# Currently contains: s7fileio (file-based sinks and sources)
# Future additions:   s7memio (shared-memory), s7netio (network/RSocket)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(s7io STATIC
    src/s7fileio.cpp
    src/s7fileio_registry.cpp
    src/s7fileio_plugin.cpp
    src/s7memio.cpp
    src/s7memio_registry.cpp
    src/s7memio_plugin.cpp
    src/s7netio.cpp
    src/s7netio_registry.cpp
    src/s7netio_plugin.cpp
    include/s7fileio.hpp
    include/s7fileio_plugin.hpp
    include/s7memio.hpp
    include/s7netio.hpp
)

target_include_directories(s7io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7io PUBLIC s7core)

install(TARGETS s7io
    EXPORT s7ioTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT s7ioTargets
    FILE s7ioConfig.cmake
    NAMESPACE s7io::
    DESTINATION lib/cmake/s7io
)
