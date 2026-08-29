if(MINISTREAM_BUILD_UI)
  qt_add_executable(
    ministream_client
    MACOSX_BUNDLE
    qt_main.mm
    ui/client_controller.cpp
    ui/client_controller.hpp
    input/sdl_gamepad.mm
    input/sdl_gamepad.hpp
  )
  target_link_libraries(
    ministream_client
    PRIVATE MiniStream::Core Qt6::Quick Qt6::Qml Qt6::QuickControls2 SDL3::SDL3
  )
  ministream_add_qml_ui(ministream_client)
else()
  add_executable(ministream_client MACOSX_BUNDLE main.mm)
  target_link_libraries(ministream_client PRIVATE MiniStream::Core SDL3::SDL3)
endif()
ministream_set_warnings(ministream_client)
set_target_properties(
  ministream_client
  PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "MiniStream"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.aftermaxq.ministream"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
install(TARGETS ministream_client BUNDLE DESTINATION . COMPONENT Runtime)
if(MINISTREAM_BUILD_UI)
  ministream_install_qt_runtime(ministream_client)
endif()
