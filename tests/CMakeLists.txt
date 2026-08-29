add_executable(
  ministream_tests
  smoke_test.cpp
  base/result_test.cpp
  protocol/wire_test.cpp
  telemetry/window_stats_test.cpp
  concurrency/queue_test.cpp
  session/session_state_test.cpp
  transport/packetizer_test.cpp
  transport/reassembler_test.cpp
  transport/reliable_control_test.cpp
  transport/packet_scheduler_test.cpp
  time/clock_sync_test.cpp
  net/fault_injector_test.cpp
  adaptation/rate_controller_test.cpp
  fec/fec_codec_test.cpp
  security/pairing_test.cpp
  security/session_crypto_test.cpp
  session/handshake_test.cpp
  tools/netprobe_options_test.cpp
  "${PROJECT_SOURCE_DIR}/tools/netprobe/options.cpp"
  net/udp_endpoint_test.cpp
  audio/audio_packet_test.cpp
  audio/opus_codec_test.cpp
  audio/jitter_buffer_test.cpp
  audio/drift_controller_test.cpp
  input/gamepad_packet_test.cpp
  input/input_coalescer_test.cpp
  input/input_capture_test.cpp
  input/rumble_packet_test.cpp
  session/session_runtime_test.cpp
  telemetry/stream_aggregator_test.cpp
  config/stream_profile_test.cpp
  session/discovery_test.cpp
  security/pairing_wire_test.cpp
)
target_link_libraries(ministream_tests PRIVATE MiniStream::Core Catch2::Catch2WithMain)
target_include_directories(ministream_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools")
ministream_set_warnings(ministream_tests)

if(WIN32)
  target_sources(
    ministream_tests
    PRIVATE
      windows/host_capabilities_test.cpp
      windows/dxgi_capture_test.cpp
      windows/wasapi_loopback_test.cpp
  )
  target_link_libraries(ministream_tests PRIVATE ministream_windows_platform)
  add_custom_command(
    TARGET ministream_tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ministream_sodium>
            $<TARGET_FILE_DIR:ministream_tests>
  )
endif()

include(Catch)
catch_discover_tests(ministream_tests)
