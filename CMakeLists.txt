cmake_minimum_required(VERSION 3.13)
project(TinyCTS)

# The library: header-only on the microcontrollers and on Linux. The Bluetooth
# classes (TinyCTSClient, TinyCTSServer) use
#   - Linux:   BlueZ over D-Bus (GIO, header-only)
#   - macOS:   CoreBluetooth (src/TinyCTS/desktop/TinyCTSCoreBluetooth.mm)
#   - Windows: WinRT (src/TinyCTS/desktop/TinyCTSWinRT.cpp, MSVC)
# The sources of macOS and Windows are added to the targets that use TinyCTS.
# On the microcontrollers they use NimBLE-Arduino, ArduinoBLE or the BLE library
# of the Pico W core: that is the business of the Arduino IDE.
add_library(TinyCTS INTERFACE)
target_include_directories(TinyCTS INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
set(TINYCTS_HAS_BLE FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(GIO gio-2.0)
  endif()
  if(GIO_FOUND)
    target_include_directories(TinyCTS INTERFACE ${GIO_INCLUDE_DIRS})
    target_link_directories(TinyCTS INTERFACE ${GIO_LIBRARY_DIRS})
    target_link_libraries(TinyCTS INTERFACE ${GIO_LIBRARIES})
    set(TINYCTS_HAS_BLE TRUE)
  endif()
elseif(APPLE)
  enable_language(OBJCXX)
  target_sources(TinyCTS INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/TinyCTS/desktop/TinyCTSCoreBluetooth.mm)
  target_compile_options(TinyCTS INTERFACE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_link_libraries(TinyCTS INTERFACE "-framework CoreBluetooth" "-framework Foundation")
  set(TINYCTS_HAS_BLE TRUE)
elseif(WIN32)
  target_sources(TinyCTS INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/TinyCTS/desktop/TinyCTSWinRT.cpp)
  target_compile_features(TinyCTS INTERFACE cxx_std_17)
  target_compile_options(TinyCTS INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
  target_link_libraries(TinyCTS INTERFACE windowsapp)
  set(TINYCTS_HAS_BLE TRUE)
endif()
if(NOT TINYCTS_HAS_BLE)
  # only the encoding of the characteristics and the time sources
  target_compile_definitions(TinyCTS INTERFACE TINY_CTS_NO_BLE)
endif()

option(TINYCTS_BUILD_TOOLS "Build the command line tool tinycts" ${TINYCTS_HAS_BLE})
option(TINYCTS_BUILD_TESTS "Build the tests (run them with ctest)" OFF)

if(TINYCTS_BUILD_TOOLS)
  add_subdirectory(tools/tinycts)
endif()
if(TINYCTS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
