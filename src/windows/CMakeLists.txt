add_library(
  ministream_windows_platform
  STATIC
  platform/controlled_backend.cpp
  platform/remote_backend.cpp
  platform/host_capabilities.cpp
  video/dxgi_capture.cpp
  video/nvenc_encoder.cpp
  video/mf_decoder.cpp
  video/d3d11_video_surface.cpp
  audio/wasapi_loopback.cpp
  audio/wasapi_output.cpp
  input/virtual_gamepad.cpp
  input/vigem_gamepad.cpp
  input/desktop_key_windows.cpp
  input/remote_input_sink.cpp
  input/window_input_source.cpp
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
  PRIVATE d3d11 d3dcompiler dxgi dxguid ole32 advapi32 user32 mfplat mfreadwrite mfuuid xinput9_1_0
          ministream_vigemclient
)
ministream_set_warnings(ministream_windows_platform)

if(MINISTREAM_BUILD_UI)
  find_package(Qt6 ${MINISTREAM_QT_VERSION} REQUIRED COMPONENTS GuiPrivate)
  qt_add_executable(
    ministream
    WIN32
    "${PROJECT_SOURCE_DIR}/src/app/qt_main.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.hpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/video_surface_item.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/video_surface_item.hpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/relative_mouse_capture.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/relative_mouse_capture.hpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/windows_video_surface_bridge.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/windows_video_surface_bridge.hpp"
    "${PROJECT_SOURCE_DIR}/assets/icons/ministream.rc"
  )
  target_link_libraries(
    ministream
    PRIVATE ministream_app ministream_windows_platform Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::GuiPrivate
  )
  ministream_add_qml_ui(ministream)
endif()
if(MINISTREAM_BUILD_UI)
  ministream_set_warnings(ministream)
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
endif()
if(MINISTREAM_BUILD_UI)
  install(FILES "$<TARGET_FILE:ministream_sodium>" DESTINATION bin COMPONENT Runtime)
endif()
