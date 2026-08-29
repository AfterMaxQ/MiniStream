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
)
target_link_libraries(ministream_tests PRIVATE MiniStream::Core Catch2::Catch2WithMain)
ministream_set_warnings(ministream_tests)

if(WIN32)
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
