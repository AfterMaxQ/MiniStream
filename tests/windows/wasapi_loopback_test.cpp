#include "windows/audio/wasapi_loopback.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("WASAPI loopback initializes the default render endpoint", "[.hardware]") {
  WasapiLoopback capture;
  REQUIRE(capture.start());
}
