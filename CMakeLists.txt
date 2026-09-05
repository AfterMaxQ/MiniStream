cmake_minimum_required(VERSION 3.30)

project(MiniStream VERSION 0.2.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINISTREAM_BUILD_TESTS "Build MiniStream tests" ON)
option(MINISTREAM_BUILD_TOOLS "Build MiniStream diagnostic tools" ON)
option(MINISTREAM_BUILD_UI "Build the Qt Quick desktop UI" OFF)
option(MINISTREAM_ENABLE_PACKAGING "Configure native installer packaging" OFF)

if(APPLE)
  # Configure Objective-C++ before SDL/dependency checks, also with Ninja/CLT.
  enable_language(OBJCXX)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Dependencies)

if(MINISTREAM_BUILD_UI)
  qt_standard_project_setup(REQUIRES ${MINISTREAM_QT_VERSION})
  include(QtUi)
endif()

add_subdirectory(src/core)
add_subdirectory(src/app)

if(WIN32)
  add_subdirectory(src/windows)
elseif(APPLE)
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

if(MINISTREAM_ENABLE_PACKAGING)
  include(packaging/Packaging.cmake)
endif()
