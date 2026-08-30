if(MINISTREAM_BUILD_UI)
  qt_add_executable(
    ministream
    MACOSX_BUNDLE
    "${PROJECT_SOURCE_DIR}/src/app/qt_main.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ui/role_controller.hpp"
    ui/client_controller.cpp
    ui/client_controller.hpp
    input/sdl_gamepad.mm
    input/sdl_gamepad.hpp
    video/videotoolbox_decoder.mm
    video/videotoolbox_decoder.hpp
    video/video_surface_bridge.mm
    video/video_surface_bridge.hpp
    audio/coreaudio_output.mm
    audio/coreaudio_output.hpp
  )
  target_link_libraries(
    ministream
    PRIVATE ministream_app MiniStream::Core Qt6::Quick Qt6::Qml Qt6::QuickControls2 SDL3::SDL3
      "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo"
      "-framework AudioToolbox" "-framework CoreAudio" "-framework AVFoundation"
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
