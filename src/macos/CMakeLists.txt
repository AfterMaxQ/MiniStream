add_library(
  ministream_macos_platform
  STATIC
  platform/controlled_backend.mm
  platform/remote_backend.mm
  input/sdl_gamepad.mm
  input/accessibility_input.mm
  video/cgdisplaystream_capture.mm
  video/videotoolbox_encoder.mm
  video/videotoolbox_decoder.mm
  video/video_surface_bridge.mm
  audio/coreaudio_output.mm
  audio/system_audio_capture.mm
)
if(NOT MINISTREAM_BUILD_UI)
  find_package(Qt6 ${MINISTREAM_QT_VERSION} REQUIRED COMPONENTS Core)
endif()
target_include_directories(ministream_macos_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(
  ministream_macos_platform
  PUBLIC MiniStream::Core SDL3::SDL3
  PRIVATE "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo"
          "-framework AudioToolbox" "-framework CoreAudio" "-framework AVFoundation"
          "-framework CoreGraphics" "-framework ApplicationServices" "-framework IOSurface"
          "-framework ScreenCaptureKit"
)
if(NOT MINISTREAM_BUILD_UI)
  target_link_libraries(ministream_macos_platform PUBLIC Qt6::Core)
endif()
set_target_properties(ministream_macos_platform PROPERTIES AUTOMOC ON)
ministream_set_warnings(ministream_macos_platform)

if(MINISTREAM_BUILD_UI)
  set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/src/app/ui/video_surface_item.mm"
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
  )
  qt_add_executable(
    ministream
    MACOSX_BUNDLE
    "${PROJECT_SOURCE_DIR}/src/app/qt_main.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.hpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/video_surface_item.mm"
    "${PROJECT_SOURCE_DIR}/src/app/ui/video_surface_item.hpp"
    "${PROJECT_SOURCE_DIR}/assets/icons/ministream.icns"
  )
  set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/assets/icons/ministream.icns"
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
  )
  target_link_libraries(
    ministream
    PRIVATE ministream_app ministream_macos_platform Qt6::Quick Qt6::Qml Qt6::QuickControls2
  )
  ministream_add_qml_ui(ministream)
endif()

if(MINISTREAM_BUILD_UI)
  ministream_set_warnings(ministream)
endif()

if(MINISTREAM_BUILD_UI)
  set_target_properties(
    ministream
    PROPERTIES
      MACOSX_BUNDLE_BUNDLE_NAME "MiniStream"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.aftermaxq.ministream"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_ICON_FILE "ministream.icns"
      MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
  )
  install(TARGETS ministream BUNDLE DESTINATION . COMPONENT Runtime)
endif()
if(MINISTREAM_BUILD_UI)
  ministream_install_qt_runtime(ministream)
endif()
