cmake_minimum_required(VERSION 3.14 FATAL_ERROR)
project(peak-cv-bridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

set(USE_STANDALONE_ASIO ON)
CPMAddPackage(
	NAME sws
	GITHUB_REPOSITORY JMUWRobotics/Simple-WebSocket-Server
	GIT_TAG master
	OPTIONS
		"USE_STANDALONE_ASIO On"
)
CPMAddPackage(
	NAME cxxopts
	GITHUB_REPOSITORY jarro2783/cxxopts
	VERSION 3.2.0
	OPTIONS
		"CXXOPTS_BUILD_EXAMPLES Off"
		"CXXOPTS_BUILD_TESTS Off"
)
CPMAddPackage(
	NAME fmt
	GITHUB_REPOSITORY fmtlib/fmt
	GIT_TAG 11.1.3
	OPTIONS
		"FMT_INSTALL Off"
)

find_package(OpenCV REQUIRED)
find_package(ids_peak REQUIRED)

include_directories(
	${OpenCV_INCLUDE_DIRS}
	${ids_peak_INCLUDE_DIRS}
)

add_executable(peakcvbridge-capture
	src/capture.cpp
)

add_executable(peakcvbridge-streamer
	src/server.cpp
	src/stream_server.cpp
)

add_library(peakcvbridge
	SHARED
	src/lib.cpp
)

target_link_libraries(peakcvbridge-streamer
	PRIVATE
	${OpenCV_LIBS}
	fmt::fmt
	simple-websocket-server
	peakcvbridge
)

target_link_libraries(peakcvbridge-capture
	PRIVATE
	${OpenCV_LIBS}
	ids_peak
	fmt::fmt
	cxxopts::cxxopts
	peakcvbridge
)

target_link_libraries(peakcvbridge
	PUBLIC
	${OpenCV_LIBS}
	ids_peak
	PRIVATE
	fmt::fmt
)

install(TARGETS peakcvbridge
	LIBRARY DESTINATION /usr/local/lib
)

install(FILES src/lib.hpp
	DESTINATION /usr/local/include
	RENAME peakcvbridge.hpp
)

if (EXISTS "/etc/systemd/system/")
	install(FILES systemd/peakcvbridge-streamer@.service
		DESTINATION /etc/systemd/system
	)
	if (NOT EXISTS "/etc/peakcvbridge-streamers/0.env")
		install(FILES systemd/example.env
			DESTINATION /etc/peakcvbridge-streamers
			RENAME 0.env
		)
	endif()
else()
	message(WARNING "Directory /etc/systemd/system/ does not exist. Not installing systemd service files.")
endif()

install(TARGETS peakcvbridge-streamer peakcvbridge-capture
	RUNTIME DESTINATION /usr/local/bin
)
