#include "core/base/result.hpp"
#include "core/protocol/value_types.hpp"
#include "core/video/codec_config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>

using namespace ministream;

TEST_CASE("Result exposes either a value or an error") {
  auto success = Result<int, std::string>::ok(42);
  auto failure = Result<int, std::string>::err("capture failed");

  REQUIRE(success.has_value());
  REQUIRE(success.value() == 42);
  REQUIRE_FALSE(failure.has_value());
  REQUIRE(failure.error() == "capture failed");
}

TEST_CASE("void Result carries an error without a dummy value") {
  auto success = Result<void, std::string>::ok();
  auto failure = Result<void, std::string>::err("not ready");

  REQUIRE(success.has_value());
  REQUIRE_FALSE(failure.has_value());
  REQUIRE(failure.error() == "not ready");
}

TEST_CASE("shared protocol types preserve the wire contract") {
  STATIC_REQUIRE(sizeof(SessionId) == sizeof(std::uint32_t));
  STATIC_REQUIRE(sizeof(ControlSeq) == sizeof(std::uint32_t));
  REQUIRE(kMaxDatagramBytes == 1200);

  CodecConfig config{VideoCodec::Hevc, 3840, 2160, 60, true, {}};
  REQUIRE(config.hdr10);
  REQUIRE(config.codec == VideoCodec::Hevc);
}
