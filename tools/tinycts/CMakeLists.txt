cmake_minimum_required(VERSION 3.13)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(tinycts CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Command line Current Time Service client and server for the desktop (Linux,
# macOS, Windows) based on the TinyCTS library. It needs
#   - Linux:   BlueZ and the development files of GLib (libglib2.0-dev)
#   - macOS:   the Xcode command line tools
#   - Windows: Visual Studio (C++/WinRT)
# Build: cmake -S tools/tinycts -B build-tinycts && cmake --build build-tinycts
if(NOT TARGET TinyCTS)
  set(TINYCTS_BUILD_TOOLS OFF)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/TinyCTS)
endif()

add_executable(tinycts main.cpp tinycts.cpp)
target_link_libraries(tinycts PRIVATE TinyCTS)
if(MSVC)
  target_compile_options(tinycts PRIVATE /W3)
else()
  target_compile_options(tinycts PRIVATE -Wall)
endif()
if(APPLE)
  # macOS kills a program that uses Bluetooth without a usage description:
  # embed the Info.plist in the executable
  target_link_options(tinycts PRIVATE
      "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
endif()

install(TARGETS tinycts RUNTIME DESTINATION bin)
