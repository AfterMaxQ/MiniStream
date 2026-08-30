#include "core/video/codec_config_wire.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("codec config wire carries codec dimensions and parameter sets") {
  CodecConfig config{VideoCodec::Hevc, 3840, 2160, 60, true,
                     {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}}};
  const auto bytes = encode_codec_config(config);
  const auto decoded = decode_codec_config(bytes);
  REQUIRE(decoded.has_value());
  REQUIRE(*decoded == config);
}
