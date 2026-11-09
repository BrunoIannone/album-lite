cmake_minimum_required(VERSION 3.21)
project(album_lite_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

# Qt-free layout core (port of ../layout.js).
add_library(album_layout INTERFACE)
target_include_directories(album_layout INTERFACE src)

enable_testing()
add_executable(test_layout tests/test_layout.cpp)
target_link_libraries(test_layout PRIVATE album_layout)
if(NOT MSVC)
  target_compile_options(test_layout PRIVATE -Wall -Wextra)
endif()
add_test(NAME layout COMMAND test_layout)

# Everything else needs Qt 6:  cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
find_package(Qt6 COMPONENTS Widgets Concurrent Test QUIET)
if(NOT Qt6_FOUND)
  message(STATUS "Qt 6 not found: building only the layout core and its tests")
  return()
endif()

add_library(album_core STATIC
  src/album.cpp
  src/imagecache.cpp
  src/exporter.cpp)
target_link_libraries(album_core PUBLIC album_layout Qt6::Widgets Qt6::Concurrent)
if(NOT MSVC)
  target_compile_options(album_core PRIVATE -Wall -Wextra)
endif()

add_library(album_ui STATIC
  src/sheetsview.cpp
  src/trayview.cpp
  src/outlineview.cpp
  src/mainwindow.cpp)
target_link_libraries(album_ui PUBLIC album_core)
if(NOT MSVC)
  target_compile_options(album_ui PRIVATE -Wall -Wextra)
endif()

add_executable(album-lite src/main.cpp)
target_link_libraries(album-lite PRIVATE album_ui)

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE album_core)
add_test(NAME core COMMAND test_core)

add_executable(smoke_ui tests/smoke_ui.cpp)
target_link_libraries(smoke_ui PRIVATE album_ui Qt6::Test)
add_test(NAME ui_smoke COMMAND smoke_ui)
