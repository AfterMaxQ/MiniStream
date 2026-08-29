include(FetchContent)

set(MINISTREAM_QT_VERSION 6.11.2)
set(MINISTREAM_CATCH2_VERSION 3.15.3)
set(MINISTREAM_ASIO_VERSION 1.38.2)
set(MINISTREAM_SDL_VERSION 3.4.14)
set(MINISTREAM_OPUS_VERSION 1.5.2)
set(MINISTREAM_SODIUM_VERSION 1.0.20)
set(MINISTREAM_LEOPARD_COMMIT 6e5725ebdf9da4370b0bcc4f70fa8eb66f4e6198)
set(MINISTREAM_VIGEMCLIENT_COMMIT 9e91a124d179bf26a878a952153042ac871da243)

if(MINISTREAM_BUILD_TESTS)
  FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v${MINISTREAM_CATCH2_VERSION}.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(Catch2)
endif()

FetchContent_Declare(
  asio
  URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-38-2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR _ministream_no_add_subdirectory
)
FetchContent_MakeAvailable(asio)
add_library(ministream_asio INTERFACE)
target_include_directories(ministream_asio SYSTEM INTERFACE "${asio_SOURCE_DIR}/include")
target_compile_definitions(ministream_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
if(WIN32)
  target_compile_definitions(ministream_asio INTERFACE _WIN32_WINNT=0x0A00)
  target_link_libraries(ministream_asio INTERFACE ws2_32 mswsock)
endif()

FetchContent_Declare(
  SDL3
  URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-${MINISTREAM_SDL_VERSION}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
if(APPLE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
endif()

FetchContent_Declare(
  opus
  URL https://downloads.xiph.org/releases/opus/opus-${MINISTREAM_OPUS_VERSION}.tar.gz
  URL_HASH SHA256=65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(_ministream_saved_build_testing "${BUILD_TESTING}")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(opus)
if(_ministream_saved_build_testing STREQUAL "")
  unset(BUILD_TESTING CACHE)
else()
  set(BUILD_TESTING "${_ministream_saved_build_testing}" CACHE BOOL "" FORCE)
endif()
unset(_ministream_saved_build_testing)
if(TARGET Opus::opus)
  set(MINISTREAM_OPUS_TARGET Opus::opus)
elseif(TARGET opus)
  set(MINISTREAM_OPUS_TARGET opus)
else()
  message(FATAL_ERROR "Pinned Opus did not provide a CMake library target")
endif()

if(WIN32)
  FetchContent_Declare(
    vigemclient
    GIT_REPOSITORY https://github.com/nefarius/ViGEmClient.git
    GIT_TAG ${MINISTREAM_VIGEMCLIENT_COMMIT}
    GIT_SHALLOW FALSE
    SOURCE_SUBDIR _ministream_no_add_subdirectory
  )
  FetchContent_MakeAvailable(vigemclient)
  add_library(
    ministream_vigemclient
    STATIC
    "${vigemclient_SOURCE_DIR}/src/ViGEmClient.cpp"
  )
  target_include_directories(
    ministream_vigemclient
    PUBLIC "${vigemclient_SOURCE_DIR}/include"
  )
  target_compile_definitions(ministream_vigemclient PRIVATE _LIB)
  target_link_libraries(ministream_vigemclient PUBLIC setupapi)
  if(MSVC)
    target_compile_options(ministream_vigemclient PRIVATE /W0)
  endif()

  FetchContent_Declare(
    libsodium_prebuilt
    URL https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20-msvc.zip
    URL_HASH SHA256=2ff97f9e3f5b341bdc808e698057bea1ae454f99e29ff6f9b62e14d0eb1b1baa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _ministream_no_add_subdirectory
  )
  FetchContent_MakeAvailable(libsodium_prebuilt)
  set(_ministream_sodium_root "${libsodium_prebuilt_SOURCE_DIR}")
  add_library(ministream_sodium SHARED IMPORTED GLOBAL)
  set_target_properties(
    ministream_sodium
    PROPERTIES
      IMPORTED_CONFIGURATIONS "Debug;Release"
      IMPORTED_LOCATION_DEBUG "${_ministream_sodium_root}/x64/Debug/v143/dynamic/libsodium.dll"
      IMPORTED_IMPLIB_DEBUG "${_ministream_sodium_root}/x64/Debug/v143/dynamic/libsodium.lib"
      IMPORTED_LOCATION_RELEASE "${_ministream_sodium_root}/x64/Release/v143/dynamic/libsodium.dll"
      IMPORTED_IMPLIB_RELEASE "${_ministream_sodium_root}/x64/Release/v143/dynamic/libsodium.lib"
      MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
      MAP_IMPORTED_CONFIG_MINSIZEREL Release
      INTERFACE_INCLUDE_DIRECTORIES "${_ministream_sodium_root}/include"
  )
else()
  find_path(MINISTREAM_SODIUM_INCLUDE_DIR sodium.h REQUIRED)
  find_library(MINISTREAM_SODIUM_LIBRARY sodium REQUIRED)
  add_library(ministream_sodium UNKNOWN IMPORTED GLOBAL)
  set_target_properties(
    ministream_sodium
    PROPERTIES
      IMPORTED_LOCATION "${MINISTREAM_SODIUM_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${MINISTREAM_SODIUM_INCLUDE_DIR}"
  )
endif()

FetchContent_Declare(
  leopard
  GIT_REPOSITORY https://github.com/catid/leopard.git
  GIT_TAG ${MINISTREAM_LEOPARD_COMMIT}
  GIT_SHALLOW FALSE
  SOURCE_SUBDIR _ministream_no_add_subdirectory
)
FetchContent_MakeAvailable(leopard)

add_library(
  ministream_leopard
  STATIC
  "${leopard_SOURCE_DIR}/leopard.cpp"
  "${leopard_SOURCE_DIR}/LeopardCommon.cpp"
  "${leopard_SOURCE_DIR}/LeopardFF8.cpp"
  "${leopard_SOURCE_DIR}/LeopardFF16.cpp"
)
target_include_directories(ministream_leopard PUBLIC "${leopard_SOURCE_DIR}")
target_compile_definitions(ministream_leopard PRIVATE LEO_BUILDING)
if(MSVC)
  target_compile_options(ministream_leopard PRIVATE /W0)
else()
  target_compile_options(ministream_leopard PRIVATE -w)
endif()

if(MINISTREAM_BUILD_UI)
  find_package(
    Qt6 ${MINISTREAM_QT_VERSION}
    REQUIRED
    COMPONENTS Quick Qml QuickControls2
  )
endif()
