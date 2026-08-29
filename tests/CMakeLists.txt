add_executable(
  ministream_tests
  smoke_test.cpp
  base/result_test.cpp
  protocol/wire_test.cpp
)
target_link_libraries(ministream_tests PRIVATE MiniStream::Core Catch2::Catch2WithMain)
ministream_set_warnings(ministream_tests)

include(Catch)
catch_discover_tests(ministream_tests)
