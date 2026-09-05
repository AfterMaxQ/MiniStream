set(CPACK_PACKAGE_NAME "MiniStream")
set(CPACK_PACKAGE_VENDOR "AfterMaxQ")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Low-latency LAN game streaming")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "MiniStream")
set(CPACK_PACKAGE_FILE_NAME "MiniStream-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_SOURCE_DIR}/out/packages/${PROJECT_VERSION}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_VERBATIM_VARIABLES YES)

if(WIN32)
  set(CPACK_GENERATOR NSIS)
  set(CPACK_PACKAGE_FILE_NAME "MiniStream-${PROJECT_VERSION}-Windows-x64-Setup")
  set(CPACK_NSIS_PACKAGE_NAME "MiniStream")
  set(CPACK_NSIS_DISPLAY_NAME "MiniStream")
  set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/assets/icons/ministream.ico")
  set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/assets/icons/ministream.ico")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MODIFY_PATH OFF)
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "ministream.exe")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\ministream.exe")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA
      "CreateShortCut '$DESKTOP\\MiniStream.lnk' '$INSTDIR\\bin\\ministream.exe'")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\MiniStream.lnk'")
  file(READ "${PROJECT_SOURCE_DIR}/packaging/windows/ministream_firewall_install.nsh"
       _ministream_firewall_install)
  file(READ "${PROJECT_SOURCE_DIR}/packaging/windows/ministream_firewall_uninstall.nsh"
       _ministream_firewall_uninstall)
  set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "${_ministream_firewall_install}")
  set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "${_ministream_firewall_uninstall}")

  set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
  include(InstallRequiredSystemLibraries)

  set(_ministream_driver_directory "${CMAKE_BINARY_DIR}/packaging")
  set(_ministream_driver_setup
      "${_ministream_driver_directory}/ViGEmBus_1.22.0_x64_x86_arm64.exe")
  file(MAKE_DIRECTORY "${_ministream_driver_directory}")
  if(NOT EXISTS "${_ministream_driver_setup}")
    file(
      DOWNLOAD
      "https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe"
      "${_ministream_driver_setup}"
      EXPECTED_HASH SHA256=89220a7865076b342892f98865f3499fb7c4cfd673159e89d352c360fd014c6a
      SHOW_PROGRESS
      TLS_VERIFY ON
    )
  endif()
  install(
    FILES "${_ministream_driver_setup}"
    DESTINATION drivers
    COMPONENT Runtime
  )
  set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
      "${_ministream_firewall_install}\n\
       ReadRegStr $0 HKLM 'SYSTEM\\CurrentControlSet\\Services\\ViGEmBus' 'ImagePath'\n\
       StrCmp $0 '' 0 MiniStreamDriverDone\n\
       MessageBox MB_YESNO|MB_ICONQUESTION 'MiniStream needs a virtual controller driver. Install ViGEmBus now?' IDNO MiniStreamDriverDone\n\
       ExecWait '\"$INSTDIR\\drivers\\ViGEmBus_1.22.0_x64_x86_arm64.exe\" /exenoui /qn /norestart' $1\n\
       MiniStreamDriverDone:")
elseif(APPLE)
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_PACKAGE_FILE_NAME "MiniStream-${PROJECT_VERSION}-macOS-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_DMG_VOLUME_NAME "MiniStream")
  set(CPACK_DMG_FORMAT UDZO)
  get_filename_component(_ministream_sodium_name "${MINISTREAM_SODIUM_LIBRARY}" NAME)
  install(
    FILES "${MINISTREAM_SODIUM_LIBRARY}"
    DESTINATION "MiniStream.app/Contents/Frameworks"
    RENAME "${_ministream_sodium_name}"
    COMPONENT Runtime
  )
  install(CODE "
    set(_ministream_binary \"\${CMAKE_INSTALL_PREFIX}/MiniStream.app/Contents/MacOS/MiniStream\")
    set(_ministream_sodium \"\${CMAKE_INSTALL_PREFIX}/MiniStream.app/Contents/Frameworks/${_ministream_sodium_name}\")
    execute_process(COMMAND install_name_tool -id \"@rpath/${_ministream_sodium_name}\" \"\${_ministream_sodium}\")
    execute_process(COMMAND install_name_tool -add_rpath \"@loader_path/../Frameworks\" \"\${_ministream_binary}\")
    execute_process(COMMAND install_name_tool -change \"${MINISTREAM_SODIUM_LIBRARY}\" \"@rpath/${_ministream_sodium_name}\" \"\${_ministream_binary}\")
  ")
endif()

include(CPack)
