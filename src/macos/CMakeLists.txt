add_library(
  ministream_macos_platform
  STATIC
  platform/controlled_backend.mm
  platform/remote_backend.mm
  input/sdl_gamepad.mm
  input/accessibility_input.mm
  video/screencapturekit_capture.mm
  video/videotoolbox_encoder.mm
  video/videotoolbox_decoder.mm
  video/video_surface_bridge.mm
  audio/coreaudio_output.mm
  audio/coreaudio_capture.mm
)
target_include_directories(ministream_macos_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(
  ministream_macos_platform
  PUBLIC MiniStream::Core SDL3::SDL3
  PRIVATE "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo"
          "-framework AudioToolbox" "-framework CoreAudio" "-framework AVFoundation"
          "-framework CoreGraphics" "-framework ApplicationServices" "-framework IOSurface"
)
ministream_set_warnings(ministream_macos_platform)

if(MINISTREAM_BUILD_UI)
  qt_add_executable(
    ministream
    MACOSX_BUNDLE
    "${PROJECT_SOURCE_DIR}/src/app/qt_main.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.hpp"
  )
  target_link_libraries(
    ministream
    PRIVATE ministream_app ministream_macos_platform Qt6::Quick Qt6::Qml Qt6::QuickControls2
  )
  ministream_add_qml_ui(ministream)
else()
  add_executable(ministream_client MACOSX_BUNDLE main.mm)
  target_link_libraries(ministream_client PRIVATE MiniStream::Core SDL3::SDL3)
endif()

if(MINISTREAM_BUILD_UI)
  ministream_set_warnings(ministream)
else()
  ministream_set_warnings(ministream_client)
endif()

if(MINISTREAM_BUILD_UI)
  set_target_properties(
    ministream
    PROPERTIES
      MACOSX_BUNDLE_BUNDLE_NAME "MiniStream"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.aftermaxq.ministream"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
  install(TARGETS ministream BUNDLE DESTINATION . COMPONENT Runtime)
else()
  set_target_properties(
    ministream_client
    PROPERTIES
      MACOSX_BUNDLE_BUNDLE_NAME "MiniStream"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.aftermaxq.ministream"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
  install(TARGETS ministream_client BUNDLE DESTINATION . COMPONENT Runtime)
endif()
if(MINISTREAM_BUILD_UI)
  ministream_install_qt_runtime(ministream)
endif()
