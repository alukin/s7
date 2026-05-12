# s7/s7ctl/CMakeLists.txt

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(s7ctl
	src/main.cpp
)

target_include_directories(s7ctl PRIVATE
	${CMAKE_SOURCE_DIR}/s7core/include
)


target_link_libraries(s7ctl PRIVATE
	Boost::json
	Boost::system
	Threads::Threads
)

install(TARGETS s7ctl DESTINATION bin)
