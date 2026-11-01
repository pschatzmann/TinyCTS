cmake_minimum_required(VERSION 3.10)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(tinycts-tests CXX)
  enable_testing()
endif()

# Tests of the library on the desktop. They need nothing but a compiler: the
# BLE classes (NimBLE) are not part of them.
#   cmake -S tests -B build-tests && cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

function(tinycts_test name)
  add_executable(${name} ${name}.cpp)
  target_include_directories(${name} PRIVATE ${LIB_SRC} ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

tinycts_test(test_types)
tinycts_test(test_timesources)
tinycts_test(test_namespace)
tinycts_test(test_serverbase)
tinycts_test(test_clientbase)
