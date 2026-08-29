add_executable(
  ministream_tests
  smoke_test.cpp
  base/result_test.cpp
  protocol/wire_test.cpp
  telemetry/window_stats_test.cpp
  concurrency/queue_test.cpp
  session/session_state_test.cpp
  transport/packetizer_test.cpp
)
target_link_libraries(ministream_tests PRIVATE MiniStream::Core Catch2::Catch2WithMain)
ministream_set_warnings(ministream_tests)

include(Catch)
catch_discover_tests(ministream_tests)
