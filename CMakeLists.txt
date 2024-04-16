cmake_minimum_required(VERSION 3.5)
project(peak-cv-bridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O3 -mtune=native")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O3 -mtune=native")

find_package(OpenCV REQUIRED)
find_package(ids_peak REQUIRED)
find_package(websocketpp REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options)

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
	Boost::program_options
	${OpenCV_LIBS}
	peakcvbridge
)

target_link_libraries(peakcvbridge-capture
	PRIVATE
	Boost::program_options
	${OpenCV_LIBS}
	ids_peak
	peakcvbridge
)

target_link_libraries(peakcvbridge
	PUBLIC
	${OpenCV_LIBS}
	ids_peak
)

install(TARGETS peakcvbridge
	LIBRARY DESTINATION /usr/local/lib
)

install(FILES src/lib.hpp
	DESTINATION /usr/local/include
	RENAME peakcvbridge.hpp
)

if (EXISTS "/etc/systemd/system/")
	install(FILES systemd/peakcvbridge-streamer.service
		DESTINATION /etc/systemd/system
	)
else()
	message(WARNING "Directory /etc/systemd/system/ does not exist. Not installing systemd service files.")
endif()

install(TARGETS peakcvbridge-streamer peakcvbridge-capture
	RUNTIME DESTINATION /usr/local/bin
)
