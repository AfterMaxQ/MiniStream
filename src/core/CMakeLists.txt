add_library(
  ministream_core
  STATIC
  protocol/wire.cpp
  telemetry/window_stats.cpp
  transport/packetizer.cpp
  transport/reassembler.cpp
  transport/reliable_control.cpp
  transport/packet_scheduler.cpp
  time/clock_sync.cpp
  net/fault_injector.cpp
  adaptation/rate_controller.cpp
  fec/fec_codec.cpp
  security/pairing.cpp
  security/session_crypto.cpp
  session/handshake.cpp
  net/udp_endpoint.cpp
)
add_library(MiniStream::Core ALIAS ministream_core)

target_include_directories(
  ministream_core
  PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

target_compile_features(ministream_core PUBLIC cxx_std_20)
target_link_libraries(
  ministream_core
  PRIVATE ministream_leopard ministream_sodium
  PUBLIC ministream_asio
)
ministream_set_warnings(ministream_core)
