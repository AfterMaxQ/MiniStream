cmake_minimum_required(VERSION 3.30)

project(MiniStream VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINISTREAM_BUILD_TESTS "Build MiniStream tests" ON)
option(MINISTREAM_BUILD_TOOLS "Build MiniStream diagnostic tools" ON)
option(MINISTREAM_BUILD_UI "Build the Qt Quick desktop UI" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Dependencies)

add_subdirectory(src/core)

if(WIN32)
  add_subdirectory(src/windows)
elseif(APPLE)
  enable_language(OBJCXX)
  add_subdirectory(src/macos)
endif()

if(MINISTREAM_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MINISTREAM_BUILD_TOOLS)
  add_subdirectory(tools/netprobe)
endif()
