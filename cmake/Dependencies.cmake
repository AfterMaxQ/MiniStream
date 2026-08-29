include(FetchContent)

set(MINISTREAM_QT_VERSION 6.11.2)
set(MINISTREAM_CATCH2_VERSION 3.15.3)
set(MINISTREAM_ASIO_VERSION 1.38.2)
set(MINISTREAM_SDL_VERSION 3.4.14)
set(MINISTREAM_OPUS_VERSION 1.5.2)
set(MINISTREAM_SODIUM_VERSION 1.0.20)
set(MINISTREAM_LEOPARD_COMMIT 6e5725ebdf9da4370b0bcc4f70fa8eb66f4e6198)

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
)

FetchContent_Declare(
  SDL3
  URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-${MINISTREAM_SDL_VERSION}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  opus
  URL https://github.com/xiph/opus/archive/refs/tags/v${MINISTREAM_OPUS_VERSION}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  libsodium
  URL https://github.com/jedisct1/libsodium/archive/refs/tags/${MINISTREAM_SODIUM_VERSION}-RELEASE.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

if(MINISTREAM_BUILD_UI)
  find_package(
    Qt6 ${MINISTREAM_QT_VERSION}
    REQUIRED
    COMPONENTS Quick Qml QuickControls2
  )
endif()
