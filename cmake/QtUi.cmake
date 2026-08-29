function(ministream_add_qml_ui target)
  set(_ui_root "${PROJECT_SOURCE_DIR}/ui")
  set(_qml_files
      "${_ui_root}/HostMain.qml"
      "${_ui_root}/ClientMain.qml"
      "${_ui_root}/theme/Tokens.qml"
      "${_ui_root}/components/AppButton.qml"
      "${_ui_root}/components/StatusRow.qml"
      "${_ui_root}/components/SectionHeader.qml"
      "${_ui_root}/pages/HostHomePage.qml"
      "${_ui_root}/pages/ClientHomePage.qml"
      "${_ui_root}/pages/PairingPage.qml")

  foreach(_qml IN LISTS _qml_files)
    get_filename_component(_alias "${_qml}" NAME)
    set_source_files_properties("${_qml}" PROPERTIES QT_RESOURCE_ALIAS "${_alias}")
  endforeach()
  set_source_files_properties(
    "${_ui_root}/theme/Tokens.qml"
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
  )

  qt_add_qml_module(
    ${target}
    URI MiniStream
    VERSION 1.0
    QML_FILES ${_qml_files}
  )
endfunction()

function(ministream_install_qt_runtime target)
  qt_generate_deploy_qml_app_script(
    TARGET ${target}
    OUTPUT_SCRIPT _deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
  )
  install(SCRIPT "${_deploy_script}" COMPONENT Runtime)
endfunction()
