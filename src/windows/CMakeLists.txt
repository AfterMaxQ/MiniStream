add_library(
  ministream_windows_platform
  STATIC
  platform/controlled_backend.cpp
  platform/host_capabilities.cpp
  video/dxgi_capture.cpp
  video/nvenc_encoder.cpp
  audio/wasapi_loopback.cpp
  input/virtual_gamepad.cpp
  input/vigem_gamepad.cpp
  input/remote_input_sink.cpp
)
target_include_directories(ministream_windows_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
if(MINISTREAM_HAVE_NVENC_SDK)
  target_include_directories(ministream_windows_platform PRIVATE
    "${MINISTREAM_NVENC_SDK_ROOT}/Interface")
  target_compile_definitions(ministream_windows_platform PRIVATE MINISTREAM_HAVE_NVENC_SDK=1)
else()
  target_compile_definitions(ministream_windows_platform PRIVATE MINISTREAM_HAVE_NVENC_SDK=0)
endif()
target_link_libraries(
  ministream_windows_platform
  PUBLIC MiniStream::Core
  PRIVATE d3d11 dxgi ole32 advapi32 user32 ministream_vigemclient
)
ministream_set_warnings(ministream_windows_platform)

if(MINISTREAM_BUILD_UI)
  qt_add_executable(
    ministream
    WIN32
    "${PROJECT_SOURCE_DIR}/src/app/qt_main.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.hpp"
    platform/controlled_backend.cpp
    platform/controlled_backend.hpp
    ui/host_controller.cpp
    ui/host_controller.hpp
  )
  target_link_libraries(
    ministream
    PRIVATE ministream_app ministream_windows_platform Qt6::Quick Qt6::Qml Qt6::QuickControls2
  )
  ministream_add_qml_ui(ministream)
else()
  add_executable(ministream_host WIN32 main.cpp)
  target_link_libraries(ministream_host PRIVATE ministream_windows_platform)
endif()
if(MINISTREAM_BUILD_UI)
  ministream_set_warnings(ministream)
else()
  ministream_set_warnings(ministream_host)
endif()

if(MINISTREAM_BUILD_UI)
  add_custom_command(
    TARGET ministream
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ministream_sodium>
            $<TARGET_FILE_DIR:ministream>
  )
  install(TARGETS ministream RUNTIME DESTINATION bin COMPONENT Runtime)
  ministream_install_qt_runtime(ministream)
else()
  add_custom_command(
    TARGET ministream_host
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ministream_sodium>
            $<TARGET_FILE_DIR:ministream_host>
  )
  install(TARGETS ministream_host RUNTIME DESTINATION bin COMPONENT Runtime)
endif()
install(FILES "$<TARGET_FILE:ministream_sodium>" DESTINATION bin COMPONENT Runtime)
