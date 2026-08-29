add_library(
  ministream_windows_platform
  STATIC
  platform/host_capabilities.cpp
  video/dxgi_capture.cpp
  audio/wasapi_loopback.cpp
  input/virtual_gamepad.cpp
  input/vigem_gamepad.cpp
)
target_include_directories(ministream_windows_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(
  ministream_windows_platform
  PUBLIC MiniStream::Core
  PRIVATE d3d11 dxgi ole32 advapi32 ministream_vigemclient
)
ministream_set_warnings(ministream_windows_platform)

add_executable(ministream_host WIN32 main.cpp)
target_link_libraries(ministream_host PRIVATE ministream_windows_platform)
ministream_set_warnings(ministream_host)

add_custom_command(
  TARGET ministream_host
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:ministream_sodium>
          $<TARGET_FILE_DIR:ministream_host>
)
